#pragma once
// Il nastro: 10 minuti di magnetizzazione registrata, agganciati alla timeline del DAW.
// - campioni a 24 bit compattati (3 byte): -120 dB di risoluzione, metà memoria del float;
// - "passate": ogni registrazione è una passata; la mappa delle passate disegna il collage;
// - annulla l'ultima passata: copia-su-scrittura a blocchi in una riserva di 120 s.
// Thread: il thread audio usa tryLock() per blocco; le operazioni pesanti (annulla, cancella,
// carica, esporta) prendono il lock dal thread dei messaggi.

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <memory>
#include <mutex>
#include <vector>

namespace c60
{
class TapeMedium
{
public:
    static constexpr double lengthSeconds = 600.0;     // 10 minuti
    static constexpr double undoSeconds   = 120.0;     // riserva per "annulla ultima passata"
    static constexpr double cellSeconds   = 0.1;       // risoluzione della mappa del collage
    static constexpr float  fullScale     = 8.0f;      // ampiezza rappresentabile (headroom della saturazione)
    static constexpr int    blockLen      = 4096;      // granularità della copia-su-scrittura

    // Non real-time. Il contenuto esistente viene ricampionato se cambia il sample rate.
    void allocate (double sampleRate, int channels)
    {
        channels = std::clamp (channels, 1, 2);
        const int64_t newLen = (int64_t) std::ceil (lengthSeconds * sampleRate);
        if (newLen == len && channels == numCh && ! data.empty()) return;

        std::vector<uint8_t> old;
        const int64_t oldLen = len, oldUsed = usedEnd.load();
        const int oldCh = numCh;
        old.swap (data);

        fs = sampleRate; len = newLen; numCh = channels;
        data.assign ((size_t) (len * numCh * 3), 0);

        if (! old.empty() && oldUsed > 0)    // ricampiona (lineare: il nastro è comunque ben sotto Nyquist)
        {
            const double ratio = (double) oldLen / (double) len;
            const int64_t newUsed = std::min (len, (int64_t) std::ceil (oldUsed / ratio));
            for (int64_t p = 0; p < newUsed; ++p)
                for (int c = 0; c < numCh; ++c)
                {
                    const double src = p * ratio;
                    const int64_t i0 = (int64_t) src;
                    const float t = (float) (src - (double) i0);
                    const int sc = std::min (c, oldCh - 1);
                    const float a = decode (&old[(size_t) ((i0 * oldCh + sc) * 3)]);
                    const float b = i0 + 1 < oldLen ? decode (&old[(size_t) (((i0 + 1) * oldCh + sc) * 3)]) : a;
                    encode (&data[(size_t) ((p * numCh + c) * 3)], a + t * (b - a));
                }
            usedEnd.store (newUsed);
        }
        else
            usedEnd.store (0);

        const int64_t numBlocks = (len + blockLen - 1) / blockLen;
        blockToPool.assign ((size_t) numBlocks, -1);
        poolCapacity = (int) std::ceil (undoSeconds * fs / blockLen);
        pool.assign ((size_t) poolCapacity * (size_t) blockLen * (size_t) numCh * 3, 0);
        dirtyBlocks.assign ((size_t) poolCapacity, 0);
        poolUsed = 0;
        undoAvailable.store (false);

        const int newCells = (int) std::ceil (lengthSeconds / cellSeconds);
        if (newCells != numCells)
        {
            numCells = newCells;
            passMap = std::make_unique<std::atomic<uint16_t>[]> ((size_t) numCells);
            for (int i = 0; i < numCells; ++i) passMap[(size_t) i].store (0);
            passMapUndo.assign ((size_t) numCells, 0);
        }
    }

    int64_t length() const      { return len; }
    int     channels() const    { return numCh; }
    double  sampleRate() const  { return fs; }
    int64_t used() const        { return usedEnd.load(); }
    int     cells() const       { return numCells; }
    uint16_t passAt (int cell) const { return passMap[(size_t) cell].load (std::memory_order_relaxed); }
    uint16_t currentPass() const     { return passId.load(); }
    bool    canUndo() const     { return undoAvailable.load(); }

    std::mutex& lock() { return mtx; }

    //==========================================================================
    // Thread audio (dentro il lock)
    inline float read (int ch, int64_t pos) const
    {
        if (pos < 0 || pos >= len) return 0.0f;
        return decode (&data[(size_t) ((pos * numCh + std::min (ch, numCh - 1)) * 3)]);
    }

    // lettura frazionaria (Hermite) per il pitch
    inline float readFrac (int ch, double pos) const
    {
        const double fl = std::floor (pos);
        const int64_t i = (int64_t) fl;
        const float t = (float) (pos - fl);
        const float xm1 = read (ch, i - 1), x0 = read (ch, i), x1 = read (ch, i + 1), x2 = read (ch, i + 2);
        const float c1 = 0.5f * (x1 - xm1);
        const float c2 = xm1 - 2.5f * x0 + 2.0f * x1 - 0.5f * x2;
        const float c3 = 0.5f * (x2 - xm1) + 1.5f * (x0 - x1);
        return ((c3 * t + c2) * t + c1) * t + x0;
    }

    // Nuova passata: salva la mappa per l'annulla e svuota la riserva.
    void beginPass()
    {
        for (int i = 0; i < poolUsed; ++i) blockToPool[(size_t) dirtyBlocks[(size_t) i]] = -1;
        poolUsed = 0;
        for (int i = 0; i < numCells; ++i) passMapUndo[(size_t) i] = passMap[(size_t) i].load (std::memory_order_relaxed);
        usedBeforePass = usedEnd.load();
        uint16_t next = (uint16_t) (passId.load() + 1);
        if (next == 0) next = 1;
        passId.store (next);
        undoAvailable.store (true);
    }

    // Testina di cancellazione: tape *= (1 − amount·(1 − residuo))
    inline void erase (int64_t pos, float amount, float residue)
    {
        if (pos < 0 || pos >= len || amount <= 0.0f) return;
        touch (pos);
        const float k = 1.0f - amount * (1.0f - residue);
        for (int c = 0; c < numCh; ++c)
        {
            uint8_t* p = at (pos, c);
            encode (p, decode (p) * k);
        }
    }

    // Testina di registrazione: il bias cancella in parte il vecchio, poi si somma il nuovo.
    inline void record (int64_t pos, int ch, float value, float amount, float biasErase)
    {
        if (pos < 0 || pos >= len || amount <= 0.0f) return;
        if (ch == 0) touch (pos);
        uint8_t* p = at (pos, std::min (ch, numCh - 1));
        encode (p, decode (p) * (1.0f - amount * (1.0f - biasErase)) + amount * value);
        if (ch == 0)
        {
            passMap[(size_t) std::min ((int64_t) numCells - 1, (int64_t) (pos / (cellSeconds * fs)))].store (passId.load(), std::memory_order_relaxed);
            if (pos + 1 > usedEnd.load (std::memory_order_relaxed)) usedEnd.store (pos + 1, std::memory_order_relaxed);
        }
    }

    //==========================================================================
    // Thread dei messaggi (prendere lock() prima)
    bool undoLastPass()
    {
        if (! undoAvailable.load()) return false;
        for (int i = 0; i < poolUsed; ++i)
        {
            const int b = dirtyBlocks[(size_t) i];
            const int64_t start = (int64_t) b * blockLen;
            const int64_t n = std::min ((int64_t) blockLen, len - start);
            std::memcpy (at (start, 0), &pool[(size_t) i * (size_t) blockLen * (size_t) numCh * 3], (size_t) (n * numCh * 3));
            blockToPool[(size_t) b] = -1;
        }
        poolUsed = 0;
        for (int i = 0; i < numCells; ++i) passMap[(size_t) i].store (passMapUndo[(size_t) i]);
        usedEnd.store (usedBeforePass);
        undoAvailable.store (false);
        return true;
    }

    void eraseAll()
    {
        std::fill (data.begin(), data.end(), (uint8_t) 0);
        for (int i = 0; i < numCells; ++i) passMap[(size_t) i].store (0);
        for (auto& b : blockToPool) b = -1;
        poolUsed = 0;
        usedEnd.store (0);
        undoAvailable.store (false);
    }

    // copia [start, start+n) in buffer float non interleaved (per salvataggio/esportazione)
    void copyOut (int64_t start, int64_t n, float* const* dest, int destCh) const
    {
        for (int64_t i = 0; i < n; ++i)
            for (int c = 0; c < destCh; ++c)
                dest[c][i] = read (c, start + i);
    }

    // carica audio float (stesso sample rate del nastro) a partire da 0
    void load (const float* const* src, int srcCh, int64_t n, const uint16_t* cellsMap, int cellsCount)
    {
        eraseAll();
        n = std::min (n, len);
        for (int64_t i = 0; i < n; ++i)
            for (int c = 0; c < numCh; ++c)
                encode (at (i, c), src[std::min (c, srcCh - 1)][i]);
        usedEnd.store (n);
        uint16_t maxId = 0;
        for (int i = 0; i < std::min (cellsCount, numCells); ++i)
        {
            passMap[(size_t) i].store (cellsMap[i]);
            maxId = std::max (maxId, cellsMap[i]);
        }
        passId.store (maxId);
    }

private:
    inline uint8_t* at (int64_t pos, int ch) { return &data[(size_t) ((pos * numCh + ch) * 3)]; }

    static inline float decode (const uint8_t* p)
    {
        int32_t v = (int32_t) ((uint32_t) p[0] | ((uint32_t) p[1] << 8) | ((uint32_t) p[2] << 16));
        if (v & 0x800000) v -= 0x1000000;
        return (float) v * (fullScale / 8388607.0f);
    }
    static inline void encode (uint8_t* p, float x)
    {
        const float s = std::clamp (x / fullScale, -1.0f, 1.0f) * 8388607.0f;
        const int32_t v = (int32_t) std::lrint (s);
        p[0] = (uint8_t) (v & 0xFF);
        p[1] = (uint8_t) ((v >> 8) & 0xFF);
        p[2] = (uint8_t) ((v >> 16) & 0xFF);
    }

    // copia-su-scrittura del blocco prima della prima modifica in questa passata
    inline void touch (int64_t pos)
    {
        const int64_t b = pos / blockLen;
        if (blockToPool[(size_t) b] >= 0) return;
        if (poolUsed >= poolCapacity) { undoAvailable.store (false, std::memory_order_relaxed); return; }
        const int64_t start = b * blockLen;
        const int64_t n = std::min ((int64_t) blockLen, len - start);
        std::memcpy (&pool[(size_t) poolUsed * (size_t) blockLen * (size_t) numCh * 3], at (start, 0), (size_t) (n * numCh * 3));
        blockToPool[(size_t) b] = poolUsed;
        dirtyBlocks[(size_t) poolUsed] = (int) b;
        ++poolUsed;
    }

    std::mutex mtx;
    std::vector<uint8_t> data, pool;
    std::vector<int> blockToPool, dirtyBlocks;
    int poolCapacity = 0, poolUsed = 0;
    std::unique_ptr<std::atomic<uint16_t>[]> passMap;
    std::vector<uint16_t> passMapUndo;
    int numCells = 0;
    std::atomic<uint16_t> passId { 0 };
    std::atomic<int64_t> usedEnd { 0 };
    int64_t usedBeforePass = 0;
    std::atomic<bool> undoAvailable { false };
    double fs = 48000;
    int64_t len = 0;
    int numCh = 2;
};

} // namespace c60
