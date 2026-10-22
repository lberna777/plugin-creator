// Validazione della libreria di analisi con segnali di cui si conosce la risposta giusta.
// Nessuna dipendenza da JUCE: c++ -std=c++17 -O2 -I source tests/analysis_test.cpp
// Esce con codice != 0 se un solo controllo fallisce.
#include "analysis/Compare.h"
#include <cstdio>
#include <memory>
#include <random>

namespace
{
int failures = 0, checks = 0;

void check (bool ok, const char* what, double got, double expected, double tol)
{
    ++checks;
    if (! ok) ++failures;
    std::printf ("  [%s] %-58s got %9.3f  expected %9.3f ±%.2f\n", ok ? " OK " : "FAIL", what, got, expected, tol);
}
void near (const char* what, double got, double expected, double tol)
{
    check (std::abs (got - expected) <= tol, what, got, expected, tol);
}

constexpr double pi = 3.14159265358979323846;

// Genera a blocchi e passa a un consumatore: niente buffer giganti, blocchi di lunghezza variabile.
template <typename Gen, typename Sink>
void run (double sr, double seconds, int channels, Gen&& gen, Sink&& sink)
{
    const long long total = (long long) std::llround (sr * seconds);
    std::vector<float> L (4096), R (4096);
    std::mt19937 rng (7);
    long long n = 0;
    while (n < total)
    {
        const int len = (int) std::min<long long> (total - n, 1 + (long long) (rng() % 4096));
        for (int i = 0; i < len; ++i)
        {
            float l, r;
            gen (n + i, l, r);
            L[(size_t) i] = l;
            R[(size_t) i] = r;
        }
        const float* data[2] = { L.data(), R.data() };
        sink (data, len);
        (void) channels;
        n += len;
    }
}

double dbToAmp (double db) { return std::pow (10.0, db / 20.0); }

// --- EBU Tech 3341 (sintetizzati: stessi segnali dei file ufficiali) ---------------------------
void testEbu (double sr)
{
    std::printf ("\nEBU Tech 3341 @ %.0f Hz\n", sr);

    auto sine = [sr] (double dbfs) {
        return [sr, a = dbToAmp (dbfs)] (long long n, float& l, float& r) {
            l = r = (float) (a * std::sin (2.0 * pi * 1000.0 * n / sr));
        };
    };

    for (double level : { -23.0, -33.0 })
    {
        auto m = std::make_unique<mc::LoudnessMeter>();
        m->prepare (sr, 2);
        run (sr, 20.0, 2, sine (level), [&] (const float* const* d, int len) { m->process (d, len); });
        char what[96];
        std::snprintf (what, sizeof (what), "caso 1/2: sinusoide 1 kHz %.0f dBFS → integrated", level);
        near (what, m->getIntegrated(), level, 0.1);
        std::snprintf (what, sizeof (what), "caso 1/2: sinusoide 1 kHz %.0f dBFS → short-term", level);
        near (what, m->getShortTerm(), level, 0.1);
        std::snprintf (what, sizeof (what), "caso 1/2: sinusoide 1 kHz %.0f dBFS → momentary", level);
        near (what, m->getMomentary(), level, 0.1);
    }

    // sequenze per il gating: (livello, durata)
    struct Seg { double db, s; };
    auto sequence = [&] (std::vector<Seg> segs, const char* what) {
        auto m = std::make_unique<mc::LoudnessMeter>();
        m->prepare (sr, 2);
        for (auto seg : segs)
            run (sr, seg.s, 2, sine (seg.db), [&] (const float* const* d, int len) { m->process (d, len); });
        near (what, m->getIntegrated(), -23.0, 0.1);
    };
    sequence ({ { -36, 10 }, { -23, 60 }, { -36, 10 } }, "caso 3: -36/-23/-36 dBFS (gate relativo)");
    sequence ({ { -72, 10 }, { -36, 10 }, { -23, 60 }, { -36, 10 }, { -72, 10 } },
              "caso 4: con -72 dBFS (gate assoluto)");
    sequence ({ { -26, 20 }, { -20, 20.1 }, { -26, 20 } }, "caso 5: -26/-20/-26 dBFS");
}

// --- True peak ---------------------------------------------------------------------------------
void testTruePeak (double sr)
{
    std::printf ("\nTrue peak @ %.0f Hz\n", sr);
    // sinusoide a fs/4 con fase 45°: i campioni cadono a ±0.707, la forma d'onda vera arriva a 1.0
    mc::TruePeakMeter tp;
    tp.prepare (2);
    run (sr, 1.0, 2, [] (long long n, float& l, float& r) {
        l = r = (float) std::sin (pi * 0.5 * n + pi * 0.25);
    }, [&] (const float* const* d, int len) { tp.process (d, len); });
    near ("fs/4, fase 45°: sample peak (dBFS)", mc::TruePeakMeter::toDb (tp.getSamplePeak()), -3.01, 0.05);
    near ("fs/4, fase 45°: true peak (dBTP)", mc::TruePeakMeter::toDb (tp.getTruePeak()), 0.0, 0.3);

    // sinusoide a 1 kHz, -6 dBFS: true peak = sample peak = -6
    mc::TruePeakMeter tp2;
    tp2.prepare (1);
    run (sr, 1.0, 1, [sr] (long long n, float& l, float& r) {
        l = r = (float) (0.5 * std::sin (2.0 * pi * 997.0 * n / sr));
    }, [&] (const float* const* d, int len) { tp2.process (d, len); });
    near ("1 kHz a -6 dBFS: true peak (dBTP)", mc::TruePeakMeter::toDb (tp2.getTruePeak()), -6.02, 0.1);
}

// --- Spettro ------------------------------------------------------------------------------------
// rumore rosa (Paul Kellet, versione "refined") da rumore bianco
struct Pink
{
    double b0 = 0, b1 = 0, b2 = 0, b3 = 0, b4 = 0, b5 = 0, b6 = 0;
    double next (double w)
    {
        b0 = 0.99886 * b0 + w * 0.0555179;  b1 = 0.99332 * b1 + w * 0.0750759;
        b2 = 0.96900 * b2 + w * 0.1538520;  b3 = 0.86650 * b3 + w * 0.3104856;
        b4 = 0.55000 * b4 + w * 0.5329522;  b5 = -0.7616 * b5 - w * 0.0168980;
        const double p = b0 + b1 + b2 + b3 + b4 + b5 + b6 + w * 0.5362;
        b6 = w * 0.115926;
        return p * 0.11;
    }
};

void testSpectrum (double sr)
{
    std::printf ("\nSpettro e stereo @ %.0f Hz\n", sr);

    {   // rumore rosa: potenza per banda a 1/6 d'ottava costante
        mc::SpectrumAnalyzer sa;
        sa.prepare (sr, 2);
        std::mt19937 rng (1);
        std::normal_distribution<double> nd (0.0, 1.0);
        Pink pl, pr;
        run (sr, 60.0, 2, [&] (long long, float& l, float& r) {
            l = (float) pl.next (nd (rng));
            r = (float) pr.next (nd (rng));
        }, [&] (const float* const* d, int len) { sa.push (d, len); });

        // riferimento: media in dB delle bande 100 Hz – 10 kHz
        double ref = 0; int cnt = 0;
        for (int b = 0; b < mc::SpectrumAnalyzer::numBands; ++b)
        {
            const double c = mc::SpectrumAnalyzer::bandCentre (b);
            if (c >= 100 && c <= 10000) { ref += mc::powerDb (sa.band (b).level()); ++cnt; }
        }
        ref /= cnt;
        double worst = 0, worstF = 0;
        for (int b = 0; b < mc::SpectrumAnalyzer::numBands; ++b)
        {
            const double c = mc::SpectrumAnalyzer::bandCentre (b);
            if (c < 40 || c > 16000) continue;
            const double dev = mc::powerDb (sa.band (b).level()) - ref;
            if (std::abs (dev) > std::abs (worst)) { worst = dev; worstF = c; }
        }
        char what[96];
        std::snprintf (what, sizeof (what), "rumore rosa piatto 40 Hz–16 kHz (peggiore a %.0f Hz)", worstF);
        near (what, worst, 0.0, 1.0);
        near ("rumore rosa indipendente L/R: correlazione bassi", sa.range (20, 120).correlation(), 0.0, 0.15);
        near ("rumore rosa indipendente L/R: correlazione alti", sa.range (4000, 20000).correlation(), 0.0, 0.05);
    }

    {   // sinusoide: tutta l'energia nella banda giusta, livello corretto
        mc::SpectrumAnalyzer sa;
        sa.prepare (sr, 2);
        const double f = 1000.0, amp = 0.5;
        run (sr, 10.0, 2, [&] (long long n, float& l, float& r) {
            l = r = (float) (amp * std::sin (2.0 * pi * f * n / sr));
        }, [&] (const float* const* d, int len) { sa.push (d, len); });
        int best = 0;
        for (int b = 1; b < mc::SpectrumAnalyzer::numBands; ++b)
            if (sa.band (b).level() > sa.band (best).level()) best = b;
        near ("sinusoide 1 kHz → centro della banda più forte (Hz)", mc::SpectrumAnalyzer::bandCentre (best), 1015.9, 1.0);
        // mean-square di una sinusoide = amp²/2 → -9.03 dBFS per amp 0.5; somma su tutte le bande
        double tot = 0;
        for (int b = 0; b < mc::SpectrumAnalyzer::numBands; ++b) tot += sa.band (b).level();
        near ("sinusoide 0.5: livello totale bande (dBFS, Parseval)", mc::powerDb (tot), -9.03, 0.2);
        near ("L = R → correlazione", sa.range (400, 2000).correlation(), 1.0, 0.001);
        check (sa.range (400, 2000).sideToMidDb() <= -59.0, "L = R → Side/Mid (dB, mono)", sa.range (400, 2000).sideToMidDb(), -60.0, 1.0);
    }

    {   // L = -R → correlazione -1
        mc::SpectrumAnalyzer sa;
        sa.prepare (sr, 2);
        std::mt19937 rng (3);
        std::normal_distribution<double> nd (0.0, 0.1);
        run (sr, 10.0, 2, [&] (long long, float& l, float& r) { l = (float) nd (rng); r = -l; },
             [&] (const float* const* d, int len) { sa.push (d, len); });
        near ("L = -R → correlazione", sa.range (20, 20000).correlation(), -1.0, 0.001);
    }

    {   // silenzio: nessun frame contato
        mc::SpectrumAnalyzer sa;
        sa.prepare (sr, 2);
        run (sr, 5.0, 2, [] (long long, float& l, float& r) { l = r = 0.0f; },
             [&] (const float* const* d, int len) { sa.push (d, len); });
        near ("silenzio → frame contati", (double) sa.getFrames(), 0.0, 0.0);
    }
}

// --- Confronto: frasi --------------------------------------------------------------------------
void testCompare()
{
    std::printf ("\nConfronto (frasi)\n");
    const double sr = 48000;
    std::mt19937 rng (5);
    std::normal_distribution<double> nd (0.0, 1.0);

    // "riferimento": rumore rosa; "mix": stesso rosa con +4 dB tra 120 e 400 Hz (filtro peaking)
    // e bassi decorrelati. Il confronto deve citare il low-mid e i bassi non mono.
    auto ref = std::make_unique<mc::MixAnalyzer>();
    auto mix = std::make_unique<mc::MixAnalyzer>();
    ref->prepare (sr, 2);
    mix->prepare (sr, 2);

    // entrambi: centro rosa + un po' di rosa indipendente per lato (un mix vero non è mai mono)
    Pink p1, p2, pL3, pR3, sL1, sR1, sL2, sR2;
    run (sr, 20.0, 2, [&] (long long, float& l, float& r) {
        const double c = p1.next (nd (rng)) * 0.5;
        l = (float) (c + 0.12 * sL1.next (nd (rng)));
        r = (float) (c + 0.12 * sR1.next (nd (rng)));
    }, [&] (const float* const* d, int len) { ref->process (d, len); });

    // peaking +6 dB a 220 Hz, Q 0.7 (RBJ)
    mc::Biquad pk;
    {
        const double A = std::pow (10.0, 6.0 / 40.0), w0 = 2 * pi * 220 / sr, alpha = std::sin (w0) / (2 * 0.7);
        const double a0 = 1 + alpha / A;
        pk.b0 = (1 + alpha * A) / a0; pk.b1 = -2 * std::cos (w0) / a0; pk.b2 = (1 - alpha * A) / a0;
        pk.a1 = -2 * std::cos (w0) / a0; pk.a2 = (1 - alpha / A) / a0;
    }
    mc::Biquad lpL, lpR;   // passa-basso ~120 Hz per il rumore decorrelato nei bassi
    {
        const double w0 = 2 * pi * 100 / sr, alpha = std::sin (w0) / (2 * 0.707), a0 = 1 + alpha;
        for (auto* f : { &lpL, &lpR })
        {
            f->b0 = (1 - std::cos (w0)) / 2 / a0; f->b1 = (1 - std::cos (w0)) / a0; f->b2 = f->b0;
            f->a1 = -2 * std::cos (w0) / a0; f->a2 = (1 - alpha) / a0;
        }
    }
    run (sr, 20.0, 2, [&] (long long, float& l, float& r) {
        const double c = pk.process (p2.next (nd (rng))) * 0.5;
        // bassi decorrelati: stesso livello del centro sotto i 100 Hz, ma diversi fra L e R
        const double dl = lpL.process (pL3.next (nd (rng))) * 0.5, dr = lpR.process (pR3.next (nd (rng))) * 0.5;
        l = (float) (c + dl + 0.12 * sL2.next (nd (rng)));
        r = (float) (c + dr + 0.12 * sR2.next (nd (rng)));
    }, [&] (const float* const* d, int len) { mix->process (d, len); });

    mc::Snapshot sm, sr2;
    mix->fill (sm);
    ref->fill (sr2);
    const auto f = mc::compare (sm, &sr2);
    std::printf ("    correlazione bassi: mix %.2f, riferimento %.2f\n", sm.correlation[0], sr2.correlation[0]);
    bool lowMid = false, mono = false;
    for (auto& x : f)
    {
        std::printf ("    · %s\n", x.text.c_str());
        lowMid |= x.text.find ("low-mid") != std::string::npos && x.text.find ("+") != std::string::npos;
        mono   |= x.text.find ("non sono mono") != std::string::npos || x.text.find ("FUORI FASE") != std::string::npos;
    }
    check (lowMid, "la frase sul low-mid in eccesso c'è", lowMid, 1, 0);
    check (mono, "la frase sui bassi non mono c'è", mono, 1, 0);
    check (f.size() <= 5, "al massimo 5 frasi", (double) f.size(), 5, 0);

    // stesso segnale contro sé stesso → nessuna differenza di zona
    const auto same = mc::compare (sr2, &sr2);
    bool noZone = true;
    for (auto& x : same) noZone &= x.text.find (" dB rispetto al riferimento") == std::string::npos;
    check (noZone, "riferimento contro sé stesso: nessuna frase di zona", noZone, 1, 0);
}
} // namespace

int main()
{
    for (double sr : { 44100.0, 48000.0, 96000.0 })
    {
        testEbu (sr);
        testTruePeak (sr);
        testSpectrum (sr);
    }
    testCompare();
    std::printf ("\n%d/%d controlli passati\n", checks - failures, checks);
    return failures == 0 ? 0 : 1;
}
