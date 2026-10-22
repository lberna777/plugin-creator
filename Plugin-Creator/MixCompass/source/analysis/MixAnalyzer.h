#pragma once
// Analizzatore completo di un segnale (il tuo mix o il riferimento): loudness, true peak, crest,
// spettro 1/6 d'ottava e stereo per banda. Produce uno Snapshot POD, copiabile senza allocare,
// che è tutto ciò che UI e confronto devono sapere. Header senza JUCE.
#include "Loudness.h"
#include "Spectrum.h"
#include "TruePeak.h"

namespace mc
{
struct Zone
{
    const char* name;      // come lo dice un fonico
    double lo, hi;         // Hz
    const char* in;        // "Nel sub", "Negli alti"… (apre la frase: niente accordi di genere/numero)
};

// Le zone in parole (PLUGIN_SPEC §2). Centri banda in [lo, hi).
inline constexpr std::array<Zone, 6> zones {{
    { "sub",      20.0,    60.0, "Nel sub" },
    { "bassi",    60.0,   120.0, "Nei bassi" },
    { "low-mid", 120.0,   400.0, "Nel low-mid" },
    { "medi",    400.0,  2000.0, "Nei medi" },
    { "presenza",2000.0, 6000.0, "Nella presenza" },
    { "aria",    6000.0, 20000.0, "Nell'aria" },
}};

// Gruppi per la lettura stereo: bassi (devono essere mono), medi, alti.
inline constexpr std::array<Zone, 3> stereoGroups {{
    { "bassi",  20.0,   120.0, "Nei bassi" },
    { "medi",  120.0,  4000.0, "Nei medi" },
    { "alti", 4000.0, 20000.0, "Negli alti" },
}};

struct Snapshot
{
    static constexpr double silence = -200.0;   // al posto di -inf: confronti e JSON restano sani

    bool   valid = false;           // abbastanza audio per giudicare
    bool   stereo = true;
    double seconds = 0;             // secondi di audio sopra il gate contati
    double integrated = silence, shortTerm = silence, momentary = silence;
    double truePeakDb = silence, samplePeakDb = silence, rmsDb = silence;
    double crestDb = 0, plr = 0;    // plr = true peak - integrated

    std::array<double, SpectrumAnalyzer::numBands> bandDb {};      // lungo termine, dBFS
    std::array<double, SpectrumAnalyzer::numBands> liveBandDb {};  // ~1 s, dBFS
    std::array<double, zones.size()> zoneDb {};                    // livello della zona, dBFS
    std::array<double, stereoGroups.size()> correlation {};        // -1..+1
    std::array<double, stereoGroups.size()> sideToMidDb {};        // dB
};

inline double clampDb (double v) noexcept { return std::isfinite (v) ? std::max (v, Snapshot::silence) : Snapshot::silence; }
inline double powerDb (double p) noexcept { return p > 1e-20 ? 10.0 * std::log10 (p) : Snapshot::silence; }

class MixAnalyzer
{
public:
    // Quanti secondi sopra il gate servono prima di dare giudizi.
    static constexpr double minSeconds = 5.0;

    void prepare (double sampleRate, int numChannels)
    {
        sr = sampleRate;
        channels = numChannels >= 2 ? 2 : 1;
        loudness.prepare (sampleRate, channels);
        truePeak.prepare (channels);
        spectrum.prepare (sampleRate, channels);
        chunkLength = std::max (1, (int) std::lround (sampleRate * 0.1));
        reset();
    }

    void reset() noexcept
    {
        loudness.reset();
        truePeak.reset();
        spectrum.reset();
        chunkAcc = 0;
        chunkCount = 0;
        rmsAcc = 0;
        rmsChunks = 0;
    }

    void process (const float* const* data, int numSamples) noexcept
    {
        loudness.process (data, numSamples);
        truePeak.process (data, numSamples);
        spectrum.push (data, numSamples);

        for (int i = 0; i < numSamples; ++i)
        {
            double s = 0;
            for (int ch = 0; ch < channels; ++ch) s += (double) data[ch][i] * data[ch][i];
            chunkAcc += s / channels;
            if (++chunkCount == chunkLength)
            {
                const double ms = chunkAcc / chunkLength;
                if (ms > 1e-6) { rmsAcc += ms; ++rmsChunks; }   // gate -60 dBFS come lo spettro
                chunkAcc = 0;
                chunkCount = 0;
            }
        }
    }

    void fill (Snapshot& s) const noexcept
    {
        s.stereo       = channels == 2;
        s.seconds      = loudness.getGatedSeconds();
        s.integrated   = clampDb (loudness.getIntegrated());
        s.shortTerm    = clampDb (loudness.getShortTerm());
        s.momentary    = clampDb (loudness.getMomentary());
        s.truePeakDb   = clampDb (TruePeakMeter::toDb (truePeak.getTruePeak()));
        s.samplePeakDb = clampDb (TruePeakMeter::toDb (truePeak.getSamplePeak()));
        s.rmsDb        = rmsChunks > 0 ? powerDb (rmsAcc / (double) rmsChunks) : Snapshot::silence;
        s.crestDb      = s.rmsDb > Snapshot::silence ? s.samplePeakDb - s.rmsDb : 0.0;
        s.plr          = s.integrated > Snapshot::silence ? s.truePeakDb - s.integrated : 0.0;

        for (int b = 0; b < SpectrumAnalyzer::numBands; ++b)
        {
            s.bandDb[(size_t) b]     = powerDb (spectrum.band (b).level());
            s.liveBandDb[(size_t) b] = powerDb (spectrum.liveBand (b).level());
        }
        for (size_t z = 0; z < zones.size(); ++z)
            s.zoneDb[z] = powerDb (spectrum.range (zones[z].lo, zones[z].hi).level());
        for (size_t g = 0; g < stereoGroups.size(); ++g)
        {
            const auto st = spectrum.range (stereoGroups[g].lo, stereoGroups[g].hi);
            s.correlation[g] = st.correlation();
            s.sideToMidDb[g] = st.sideToMidDb();
        }
        s.valid = s.seconds >= minSeconds && spectrum.getFrames() > 0;
    }

private:
    double sr = 48000;
    int channels = 2;
    LoudnessMeter loudness;
    TruePeakMeter truePeak;
    SpectrumAnalyzer spectrum;
    int chunkLength = 4800, chunkCount = 0;
    double chunkAcc = 0, rmsAcc = 0;
    long long rmsChunks = 0;
};
} // namespace mc
