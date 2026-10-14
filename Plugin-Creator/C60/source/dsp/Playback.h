#pragma once
// Catena di riproduzione: trasporto (wow/flutter, azimut) → fruscio → dropout → basse (head bump)
// → perdite di testina → bias → de-enfasi → decodifica NR.
// Usata dal plugin in tempo reale e dall'esportazione WAV (istanza separata, offline).

#include "TapeDSP.h"
#include "NoiseReduction.h"

namespace c60
{
struct FrameControl
{
    double wow = 0, flutter = 0;     // 0..1
    double hissGain = 0;             // lineare, già calibrato
    double age = 0;                  // 0..1
    double azStaticSeconds = 0;      // errore di azimut fisso (deck + trimmer)
    bool   autoAzimuth = false;      // NAAC: azzera la parte casuale e quella del deck
    bool   moving = true;            // nastro in movimento (fruscio e dropout solo se scorre)
};

class PlaybackChain
{
public:
    static constexpr int maxCh = 2;
    static constexpr double azimuthAgeSeconds = 25e-6;   // deriva d'azimut a usura massima

    void prepare (double sampleRate, int channels, int centerSamples)
    {
        fs = sampleRate;
        numCh = std::clamp (channels, 1, maxCh);
        center = (double) centerSamples;
        for (int c = 0; c < maxCh; ++c)
        {
            transport[c].prepare (2 * centerSamples + 8);
            hp[c].reset(); bump[c].reset(); dip[c].reset(); lp[c].reset(); biasShelf[c].reset(); de[c].reset();
            dropLp[c] = 0;
            nr[c].prepare (fs);
        }
        dropLpCoeff = 1.0 - std::exp (-2 * pi * 2500.0 / fs);
        drops.prepare (fs);
        azDrift.prepare (fs, 0.15, 0xBEEFu);
        wf.prepare (fs, speedFactor);
        type = speed = -1;
    }

    void setFormula (int tapeType, int speedIdx, double emphasisBoostDb)
    {
        tapeType = std::clamp (tapeType, 0, 2);
        speedIdx = std::clamp (speedIdx, 0, 1);
        if (tapeType == type && speedIdx == speed) return;
        const bool speedChanged = speedIdx != speed;
        type = tapeType; speed = speedIdx;
        speedFactor = speedIdx == 1 ? 2.0 : 1.0;
        const auto& f = tapeFormula (type);
        const double tauPole = f.emphasisTau / std::pow (10.0, emphasisBoostDb / 20.0);
        for (int c = 0; c < maxCh; ++c)
        {
            de[c].set (fs, tauPole, f.emphasisTau);
            setLowEnd (hp[c], bump[c], dip[c], fs, speedFactor);
        }
        if (speedChanged) wf.prepare (fs, speedFactor);
    }

    void setNoiseReduction (int mode)
    {
        for (int c = 0; c < maxCh; ++c) nr[c].setMode (mode);
    }

    // ogni 32 campioni: usura (taglio degli acuti) e bias (scaffale)
    void setChunk (double age, double bias, double ageHfLoss, double biasShelfDb, double biasShelfHz)
    {
        const auto& f = tapeFormula (type);
        const double fc = std::min (f.hfCorner * (speedFactor > 1 ? 1.35 : 1.0) * (1.0 - ageHfLoss * age), 0.45 * fs);
        for (int c = 0; c < maxCh; ++c)
        {
            lp[c].setLowPass (fs, fc, 0.8);
            biasShelf[c].setHighShelf (fs, std::min (biasShelfHz, 0.4 * fs), -biasShelfDb * bias);
        }
    }

    // frame: in = magnetizzazione letta dal nastro (o dal vivo), out = uscita della testina di riproduzione
    inline void process (float* frame, const FrameControl& k)
    {
        const double maxMod = center - 4.0;
        const double mod = std::clamp (wf.next (k.wow, k.flutter), -maxMod, maxMod);
        const double drop = k.moving ? drops.next (k.age) : 0.0;
        const double drift = azDrift.next();
        double az = 0;
        if (numCh > 1)
        {
            az = k.azStaticSeconds;
            if (! k.autoAzimuth) az += k.age * azimuthAgeSeconds * (0.5 + 0.5 * drift);
            az *= fs;
        }

        for (int c = 0; c < numCh; ++c)
        {
            transport[c].push (frame[c]);
            double v = transport[c].read (center + mod + (c == 0 ? 0.5 : -0.5) * az);

            if (k.moving) v += k.hissGain * hissRng[c].gauss();

            const double d = drop * drops.channelSkew (c);
            dropLp[c] += dropLpCoeff * (v - dropLp[c]);
            v = ((1.0 - d) * v + d * dropLp[c]) * (1.0 - 0.5 * d);

            v = hp[c].process (v);
            v = bump[c].process (v);
            v = dip[c].process (v);
            v = lp[c].process (v);
            v = biasShelf[c].process (v);
            v = de[c].process (v);
            frame[c] = (float) nr[c].decode (v);
        }
    }

private:
    double fs = 48000, center = 240, speedFactor = 1, dropLpCoeff = 0.3;
    int numCh = 2, type = -1, speed = -1;
    FracDelay transport[maxCh];
    Biquad hp[maxCh], bump[maxCh], dip[maxCh], lp[maxCh], biasShelf[maxCh];
    FirstOrderShelf de[maxCh];
    NoiseReduction nr[maxCh];
    double dropLp[maxCh] {};
    WowFlutter wf;
    Dropouts drops;
    SmoothRandom azDrift;
    Rng hissRng[maxCh] { Rng (0xA5A5A5u), Rng (0x5A5A5Au) };
};

} // namespace c60
