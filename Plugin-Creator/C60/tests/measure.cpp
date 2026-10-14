// Banco di misura offline del vero processor (sovracampionamento incluso).
// Uso: C60Measure            → tutte le misure, exit code 1 se un controllo fallisce.

#include <JuceHeader.h>
#include "PluginProcessor.h"
#include <cstdio>
#include <random>

namespace
{
int failures = 0;
void check (bool ok, const char* what)
{
    std::printf ("  [%s] %s\n", ok ? " OK " : "FAIL", what);
    if (! ok) ++failures;
}

struct Rig
{
    std::unique_ptr<C60AudioProcessor> p;
    int numCh;
    double fs;

    Rig (double sampleRate, int channels, int block, std::map<juce::String, float> params = {})
        : p (std::make_unique<C60AudioProcessor>()), numCh (channels), fs (sampleRate)
    {
        auto set = channels == 1 ? juce::AudioChannelSet::mono() : juce::AudioChannelSet::stereo();
        juce::AudioProcessor::BusesLayout l; l.inputBuses.add (set); l.outputBuses.add (set);
        p->setBusesLayout (l);
        for (auto& [id, v] : params) setParam (id, v);
        p->setRateAndBufferSizeDetails (sampleRate, block);
        p->prepareToPlay (sampleRate, block);
    }
    void setParam (const juce::String& id, float plain)
    {
        auto* prm = p->apvts.getParameter (id);
        prm->setValueNotifyingHost (prm->convertTo0to1 (plain));
    }
    // elabora un segnale intero a blocchi (dimensione fissa o casuale)
    void run (juce::AudioBuffer<float>& io, int block, bool randomBlocks = false)
    {
        std::mt19937 rng (7);
        juce::MidiBuffer midi;
        for (int pos = 0; pos < io.getNumSamples();)
        {
            int n = randomBlocks ? 1 + (int) (rng() % (unsigned) block) : block;
            n = std::min (n, io.getNumSamples() - pos);
            juce::AudioBuffer<float> view (io.getArrayOfWritePointers(), io.getNumChannels(), pos, n);
            p->processBlock (view, midi);
            pos += n;
        }
    }
};

double db (double x) { return 20.0 * std::log10 (std::max (x, 1e-12)); }

juce::AudioBuffer<float> sine (int ch, int len, double fs, double f, double amp)
{
    juce::AudioBuffer<float> b (ch, len);
    for (int c = 0; c < ch; ++c)
        for (int n = 0; n < len; ++n) b.setSample (c, n, (float) (amp * std::sin (2 * juce::MathConstants<double>::pi * f * n / fs)));
    return b;
}

// ampiezza della fondamentale e THD+N su una finestra intera di periodi
void analyse (const float* y, int start, int len, double fs, double f, double& amp, double& thd)
{
    double re = 0, im = 0, pw = 0, mean = 0;
    for (int n = start; n < start + len; ++n) mean += y[n];
    mean /= len;
    for (int n = start; n < start + len; ++n)
    {
        const double v = y[n] - mean, ph = 2 * juce::MathConstants<double>::pi * f * n / fs;
        pw += v * v; re += v * std::cos (ph); im += v * std::sin (ph);
    }
    amp = 2 * std::sqrt (re * re + im * im) / len;
    const double p1 = amp * amp / 2;
    thd = 100 * std::sqrt (std::max (0.0, pw / len - p1) / p1);
}

const std::map<juce::String, float> clean { { "wow", 0 }, { "flutter", 0 }, { "hiss", 0 }, { "age", 0 } };

std::map<juce::String, float> with (std::map<juce::String, float> base, std::map<juce::String, float> extra)
{
    for (auto& [k, v] : extra) base[k] = v;
    return base;
}
} // namespace

int main()
{
    juce::ScopedJuceInitialiser_GUI init;
    const double fs = 48000;

    //--------------------------------------------------------------------------
    std::printf ("\n== null-test (mix 0 e bypass): uscita == ingresso ritardato della latenza ==\n");
    for (auto params : { std::map<juce::String, float> { { "mix", 0 } }, std::map<juce::String, float> { { "bypass", 1 } } })
    {
        Rig r (fs, 2, 512, params);
        const int lat = r.p->getLatencySamples(), len = 48000;
        juce::AudioBuffer<float> in (2, len);
        juce::Random rnd (3);
        for (int c = 0; c < 2; ++c) for (int n = 0; n < len; ++n) in.setSample (c, n, rnd.nextFloat() * 2 - 1);
        juce::AudioBuffer<float> io (in);
        r.run (io, 512, true);
        double maxDiff = 0;
        for (int c = 0; c < 2; ++c) for (int n = lat; n < len; ++n) maxDiff = std::max (maxDiff, (double) std::abs (io.getSample (c, n) - in.getSample (c, n - lat)));
        std::printf ("  %s: latenza %d campioni, diff max %.1f dBFS\n", params.begin()->first.toRawUTF8(), lat, db (maxDiff));
        check (db (maxDiff) < -120, "diff < -120 dBFS");
    }

    //--------------------------------------------------------------------------
    std::printf ("\n== risposta in frequenza a piccolo segnale (-40 dBFS, niente wow/hiss/age) ==\n");
    const double freqs[] = { 20, 40, 62, 100, 150, 300, 1000, 3000, 6000, 10000, 12500, 15000, 18000, 20000 };
    std::printf ("  %-18s", "");
    for (double f : freqs) std::printf ("%7.0f", f);
    std::printf ("\n");
    for (int type = 0; type < 3; ++type)
        for (int speed = 0; speed < 2; ++speed)
        {
            std::printf ("  Type %-3s %-8s", type == 0 ? "I" : type == 1 ? "II" : "IV", speed ? "9.5" : "4.76");
            for (double f : freqs)
            {
                Rig r (fs, 1, 512, with (clean, { { "tape", (float) type }, { "speed", (float) speed } }));
                const int len = 48000;
                auto io = sine (1, len, fs, f, 0.01);
                r.run (io, 512);
                double amp, thd;
                const int win = (int) (std::floor (0.5 * f) / f * fs);
                analyse (io.getReadPointer (0), len - win, win, fs, f, amp, thd);
                std::printf ("%7.1f", db (amp / 0.01));
            }
            std::printf ("\n");
        }

    //--------------------------------------------------------------------------
    std::printf ("\n== saturazione: THD a 1 kHz e compressione degli acuti (10 kHz) ==\n");
    for (int type = 0; type < 3; ++type)
    {
        std::printf ("  Type %s\n", type == 0 ? "I" : type == 1 ? "II" : "IV");
        for (double lvl : { -30.0, -18.0, -12.0, -6.0, 0.0 })
        {
            double res[2][2];
            int k = 0;
            for (double f : { 1000.0, 10000.0 })
            {
                Rig r (fs, 1, 512, with (clean, { { "tape", (float) type } }));
                const int len = 24000;
                const double a = std::pow (10, lvl / 20);
                auto io = sine (1, len, fs, f, a);
                r.run (io, 512);
                analyse (io.getReadPointer (0), len - 4800, 4800, fs, f, res[k][0], res[k][1]);
                res[k][0] = db (res[k][0] / a);
                ++k;
            }
            std::printf ("    in %5.0f dBFS → 1k: gain %+5.1f dB THD %5.2f %%   10k: gain %+5.1f dB\n", lvl, res[0][0], res[0][1], res[1][0]);
        }
    }

    //--------------------------------------------------------------------------
    std::printf ("\n== fruscio (ingresso muto, age 0) ==\n");
    for (float h : { 0.f, 10.f, 30.f, 60.f, 100.f })
        for (int type : { 0, 1 })
        {
            Rig r (fs, 2, 512, { { "hiss", h }, { "age", 0 }, { "tape", (float) type } });
            juce::AudioBuffer<float> io (2, 96000);
            io.clear();
            r.run (io, 512);
            double acc = 0;
            for (int n = 48000; n < 96000; ++n) acc += io.getSample (0, n) * io.getSample (0, n);
            double corr = 0, a2 = 0, b2 = 0;
            for (int n = 48000; n < 96000; ++n) { corr += io.getSample (0, n) * io.getSample (1, n); a2 += io.getSample (0, n) * io.getSample (0, n); b2 += io.getSample (1, n) * io.getSample (1, n); }
            std::printf ("  hiss %3.0f Type %s: %6.1f dBFS RMS  (correlazione L/R %.2f)\n", h, type ? "II" : "I", db (std::sqrt (acc / 48000)), a2 > 0 ? corr / std::sqrt (a2 * b2) : 0.0);
        }

    //--------------------------------------------------------------------------
    std::printf ("\n== wow & flutter: deviazione di frequenza su un tono a 3150 Hz (8 s) ==\n");
    for (auto wfp : { std::pair<float, float> { 20, 20 }, { 50, 50 }, { 100, 0 }, { 0, 100 }, { 100, 100 } })
        for (int speed : { 0, 1 })
        {
            if (speed == 1 && wfp.first != 100) continue;
            Rig r (fs, 1, 512, { { "wow", wfp.first }, { "flutter", wfp.second }, { "hiss", 0 }, { "age", 0 }, { "speed", (float) speed } });
            const int len = 8 * 48000;
            auto io = sine (1, len, fs, 3150, 0.1);
            r.run (io, 512);
            // frequenza istantanea dagli zero-crossing (interpolati), mediata su ~10 periodi
            std::vector<double> zc;
            const float* y = io.getReadPointer (0);
            for (int n = 48000; n < len; ++n)
                if (y[n - 1] < 0 && y[n] >= 0) zc.push_back (n - 1 + y[n - 1] / (y[n - 1] - y[n]));
            double peak = 0, acc = 0; int cnt = 0;
            for (size_t i = 10; i < zc.size(); ++i)
            {
                const double fInst = 10.0 * fs / (zc[i] - zc[i - 10]);
                const double dev = (fInst - 3150.0) / 3150.0 * 100.0;
                peak = std::max (peak, std::abs (dev)); acc += dev * dev; ++cnt;
            }
            std::printf ("  wow %3.0f flutter %3.0f @ %s cm/s: picco %.3f %%  RMS %.3f %%\n", wfp.first, wfp.second, speed ? "9.5" : "4.76", peak, std::sqrt (acc / cnt));
        }

    //--------------------------------------------------------------------------
    std::printf ("\n== robustezza: sample rate, mono/stereo, blocchi casuali, parametri estremi ==\n");
    bool allFinite = true;
    double worstPeak = 0;
    for (double sr : { 44100.0, 48000.0, 96000.0, 192000.0 })
        for (int ch : { 1, 2 })
            for (auto extreme : { std::map<juce::String, float> {},
                                  std::map<juce::String, float> { { "input", 18 }, { "age", 100 }, { "bias", -100 }, { "wow", 100 }, { "flutter", 100 }, { "hiss", 100 }, { "tape", 0 } },
                                  std::map<juce::String, float> { { "input", -18 }, { "bias", 100 }, { "tape", 2 }, { "speed", 1 }, { "autogain", 0 }, { "output", 18 } } })
            {
                Rig r (sr, ch, 4096, extreme);
                const int len = (int) sr * 2;
                juce::AudioBuffer<float> io (ch, len);
                juce::Random rnd (11);
                for (int c = 0; c < ch; ++c)
                    for (int n = 0; n < len; ++n)   // rumore + kick sintetico ogni 0,5 s
                        io.setSample (c, n, 0.3f * (rnd.nextFloat() * 2 - 1) + ((n % (int) (sr / 2)) < 2000 ? 0.9f * std::sin (n * 0.01f) : 0.f));
                // automazione a metà: cambio tipo, velocità e bias durante l'elaborazione
                r.run (io, 4096, true);
                r.setParam ("tape", 1); r.setParam ("speed", 0); r.setParam ("bias", 50);
                r.run (io, 4096, true);
                for (int c = 0; c < ch; ++c)
                    for (int n = 0; n < len; ++n)
                    {
                        const float v = io.getSample (c, n);
                        if (! std::isfinite (v)) allFinite = false;
                        worstPeak = std::max (worstPeak, (double) std::abs (v));
                    }
            }
    std::printf ("  picco massimo osservato: %.1f dBFS\n", db (worstPeak));
    check (allFinite, "nessun NaN/Inf (44.1/48/96/192 kHz, mono/stereo, blocchi 1..4096)");

    //--------------------------------------------------------------------------
    std::printf ("\n== CPU ==\n");
    {
        Rig r (48000, 2, 512);
        const int len = 48000 * 10;
        auto io = sine (2, len, 48000, 440, 0.3);
        const auto t0 = juce::Time::getHighResolutionTicks();
        r.run (io, 512);
        const double secs = juce::Time::highResolutionTicksToSeconds (juce::Time::getHighResolutionTicks() - t0);
        std::printf ("  10 s stereo a 48 kHz elaborati in %.3f s → %.2f %% di un core\n", secs, secs / 10.0 * 100);
    }

    std::printf ("\n%s (%d controlli falliti)\n", failures == 0 ? "TUTTO OK" : "CI SONO ERRORI", failures);
    return failures == 0 ? 0 : 1;
}
