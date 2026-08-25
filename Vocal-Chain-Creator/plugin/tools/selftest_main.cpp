/*  vocalforge_selftest — le verifiche di CHECKLIST.md che si possono fare senza un DAW.

    Istanzia il processore vero, gli fa girare dentro del segnale e controlla:
      · null-test con tutti i moduli spenti (deve restituire l'ingresso)
      · nessun NaN/Inf in uscita, in mono e in stereo, a 44.1/48/96 kHz
      · nessun clipping interno con ingresso a -6 dBFS
      · cambio di prompt mentre l'audio gira: nessun click
      · le mandate sono parallele (spegnendole la voce non cambia)
      · stato salvato e riletto: prompt e parametri tornano uguali
*/
#include "../source/PluginProcessor.h"
#include <iostream>
#include <random>
#include <atomic>
#include <cstdlib>
#include <cstring>

/*  Contatore di allocazioni globale: serve al controllo "il thread audio non alloca".
    Sostituire operator new nel binario di test e' il modo standard di misurarlo. */
namespace vfalloc { std::atomic<int> count { 0 }; std::atomic<bool> armed { false }; }

void* operator new (std::size_t size)
{
    if (vfalloc::armed.load()) vfalloc::count.fetch_add (1);
    return std::malloc (size);
}
void operator delete (void* pointer) noexcept { std::free (pointer); }
void operator delete (void* pointer, std::size_t) noexcept { std::free (pointer); }

namespace
{
    int failures = 0;

    void check (bool condition, const juce::String& what, const juce::String& detail = {})
    {
        std::cout << (condition ? "  ok    " : "  FAIL  ") << what;
        if (! condition && detail.isNotEmpty()) std::cout << "  (" << detail << ")";
        std::cout << std::endl;
        if (! condition) ++failures;
    }

    void fillVoiceLike (juce::AudioBuffer<float>& buffer, double sampleRate, float peak)
    {
        std::mt19937 generator (1234);
        std::uniform_real_distribution<float> noise (-1.0f, 1.0f);
        const auto numSamples = buffer.getNumSamples();

        for (int sample = 0; sample < numSamples; ++sample)
        {
            const auto t = static_cast<double> (sample) / sampleRate;
            // fondamentale + armoniche + un po' di sibilante + rumore di stanza
            auto value = 0.6 * std::sin (juce::MathConstants<double>::twoPi * 140.0 * t)
                       + 0.25 * std::sin (juce::MathConstants<double>::twoPi * 420.0 * t)
                       + 0.12 * std::sin (juce::MathConstants<double>::twoPi * 6800.0 * t)
                       + 0.02 * noise (generator);
            value *= 0.5 + 0.5 * std::sin (juce::MathConstants<double>::twoPi * 2.5 * t);   // sillabe
            for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
                buffer.setSample (ch, sample, static_cast<float> (value) * peak);
        }
    }

    /*  fillVoiceLike genera ~10 ms per blocco: la modulazione di sillaba (2.5 Hz) non fa in tempo
        ad arrivare al massimo, quindi il picco reale del blocco è parecchio sotto il valore chiesto.
        Per misurare il gain staging serve invece un livello d'ingresso ESATTO — qui il blocco viene
        normalizzato al picco voluto, in dBFS. */
    void fillAtPeak (juce::AudioBuffer<float>& buffer, double sampleRate, float peakDbfs)
    {
        fillVoiceLike (buffer, sampleRate, 1.0f);
        const auto current = buffer.getMagnitude (0, buffer.getNumSamples());
        if (current > 0.0f)
            buffer.applyGain (juce::Decibels::decibelsToGain (peakDbfs) / current);
    }

    /*  Voce CON sibilanti vere: sopra la vocale, ogni 400 ms, una 's' di 100 ms fatta di banda alta.
        fillVoiceLike ha una riga fissa a 6.8 kHz 18 dB sotto la fondamentale — cioè un segnale su cui
        nessun de-esser onesto interviene. Le soglie del de-esser vanno tarate su questo: la banda alta
        della 's' arriva a ~6 dB sotto il picco della voce, che è quello che fa una ripresa ravvicinata
        su cardioide (knowledge/ROOM_MIC_PROFILE.md). */
    void fillVoiceWithSibilants (juce::AudioBuffer<float>& buffer, double sampleRate, float peak)
    {
        std::mt19937 generator (4321);
        std::uniform_real_distribution<float> noise (-1.0f, 1.0f);

        for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
        {
            const auto t = static_cast<double> (sample) / sampleRate;
            auto vowel = 0.6 * std::sin (juce::MathConstants<double>::twoPi * 140.0 * t)
                       + 0.25 * std::sin (juce::MathConstants<double>::twoPi * 420.0 * t)
                       + 0.02 * noise (generator);
            vowel *= 0.5 + 0.5 * std::sin (juce::MathConstants<double>::twoPi * 2.5 * t);

            const auto inWord = std::fmod (t, 0.4) < 0.1;      // la 's' che apre la sillaba
            const auto sibilant = inWord
                ? 0.45 * (0.6 * std::sin (juce::MathConstants<double>::twoPi * 6800.0 * t)
                          + 0.5 * std::sin (juce::MathConstants<double>::twoPi * 9000.0 * t)
                          + 0.35 * noise (generator))
                : 0.0;

            const auto value = static_cast<float> (vowel + sibilant) * peak;
            for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
                buffer.setSample (ch, sample, value);
        }
    }

    /*  Una parola sola, poi silenzio: serve a misurare il ducking delle mandate. Le mandate devono
        stare SOTTO la parola e tornare su nel vuoto fra una parola e l'altra. */
    void fillWordThenGap (juce::AudioBuffer<float>& buffer, double sampleRate, float peakDbfs, double wordSeconds)
    {
        fillVoiceWithSibilants (buffer, sampleRate, 1.0f);
        const auto current = buffer.getMagnitude (0, buffer.getNumSamples());
        if (current > 0.0f)
            buffer.applyGain (juce::Decibels::decibelsToGain (peakDbfs) / current);

        const auto wordEnd = juce::jmin (buffer.getNumSamples(), static_cast<int> (wordSeconds * sampleRate));
        for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
            buffer.clear (ch, wordEnd, buffer.getNumSamples() - wordEnd);
    }

    void fillAtPeakSibilant (juce::AudioBuffer<float>& buffer, double sampleRate, float peakDbfs)
    {
        fillVoiceWithSibilants (buffer, sampleRate, 1.0f);
        const auto current = buffer.getMagnitude (0, buffer.getNumSamples());
        if (current > 0.0f)
            buffer.applyGain (juce::Decibels::decibelsToGain (peakDbfs) / current);
    }

    /*  Livello di UNA riga spettrale, in dB, con il Goertzel. Serve al secondo giro di taratura:
        le soglie del de-esser e il satTilt vanno misurati sulla banda che toccano, non dedotti
        dai numeri del preset. Le frequenze usate (140 · 420 · 6800 · 200 · 8000 Hz) cadono tutte
        su un bin intero con finestra 12000 a 48 kHz, quindi non serve finestratura. */
    float lineDb (const float* data, int numSamples, double frequency, double sampleRate)
    {
        const auto w = 2.0 * juce::MathConstants<double>::pi * frequency / sampleRate;
        const auto coefficient = 2.0 * std::cos (w);
        double s1 = 0.0, s2 = 0.0;
        for (int i = 0; i < numSamples; ++i)
        {
            const auto s0 = data[i] + coefficient * s1 - s2;
            s2 = s1; s1 = s0;
        }
        const auto real = s1 - s2 * std::cos (w), imaginary = s2 * std::sin (w);
        const auto magnitude = 2.0 * std::sqrt (real * real + imaginary * imaginary) / numSamples;
        return juce::Decibels::gainToDecibels (static_cast<float> (magnitude), -140.0f);
    }

    float rmsDb (const juce::AudioBuffer<float>& buffer, int start, int count)
    {
        return juce::Decibels::gainToDecibels (buffer.getRMSLevel (0, start, count), -140.0f);
    }

    const char* const kArtists[] { "sfera ebbasta", "shiva", "tony boy", "glockyy", "gue pequeno", "capo plaza" };

    bool isFinite (const juce::AudioBuffer<float>& buffer)
    {
        for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
        {
            const auto* data = buffer.getReadPointer (ch);
            for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
                if (! std::isfinite (data[sample])) return false;
        }
        return true;
    }

    float maxJump (const juce::AudioBuffer<float>& buffer)
    {
        float worst = 0.0f;
        for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
        {
            const auto* data = buffer.getReadPointer (ch);
            for (int sample = 1; sample < buffer.getNumSamples(); ++sample)
                worst = juce::jmax (worst, std::abs (data[sample] - data[sample - 1]));
        }
        return worst;
    }

    void setAllModules (VocalForgeProcessor& processor, bool on)
    {
        for (auto* id : { "gateOn", "hpfOn", "roomOn", "eqSubOn", "ds1On", "c1On", "c2On",
                          "satOn", "eqToneOn", "ds2On", "limOn", "revOn", "dlyOn", "fxOn", "airOn" })
            if (auto* parameter = processor.apvts.getParameter (id))
                parameter->setValueNotifyingHost (on ? 1.0f : 0.0f);

        for (auto* id : { "inTrim", "outGain", "satTilt" })
            if (auto* parameter = processor.apvts.getParameter (id))
                parameter->setValueNotifyingHost (parameter->convertTo0to1 (0.0f));
        if (auto* mix = processor.apvts.getParameter ("mix"))
            mix->setValueNotifyingHost (mix->convertTo0to1 (100.0f));
        // le mandate interne devono essere accese: un test precedente puo' aver lasciato "logic"
        if (auto* mode = processor.apvts.getParameter ("sendsMode"))
            mode->setValueNotifyingHost (0.0f);
    }
}

int main()
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    std::cout << "VOCAL FORGE — self test" << std::endl;

    VocalForgeProcessor processor;
    check (processor.rulesEngine.isLoaded(), "regole imbarcate caricate (v" + processor.getRulesVersion() + ")");

    juce::MidiBuffer midi;
    const int blockSize = 512;

    // come fa un host vero: prima comunica rate e block size, poi prepara
    auto prepare = [&processor, blockSize] (double sampleRate)
    {
        processor.setRateAndBufferSizeDetails (sampleRate, blockSize);
        processor.prepareToPlay (sampleRate, blockSize);
    };

    // ---- null test: tutto spento, l'uscita deve essere l'ingresso
    {
        const double sampleRate = 48000.0;
        prepare (sampleRate);
        setAllModules (processor, false);

        juce::AudioBuffer<float> buffer (2, blockSize), reference (2, blockSize);
        fillVoiceLike (buffer, sampleRate, 0.5f);
        for (int ch = 0; ch < 2; ++ch) reference.copyFrom (ch, 0, buffer, ch, 0, blockSize);

        for (int i = 0; i < 8; ++i)      // qualche blocco per far assestare gli smoothing
        {
            fillVoiceLike (buffer, sampleRate, 0.5f);
            for (int ch = 0; ch < 2; ++ch) reference.copyFrom (ch, 0, buffer, ch, 0, blockSize);
            processor.processBlock (buffer, midi);
        }

        float worstDiff = 0.0f;
        for (int ch = 0; ch < 2; ++ch)
            for (int sample = 0; sample < blockSize; ++sample)
                worstDiff = juce::jmax (worstDiff, std::abs (buffer.getSample (ch, sample)
                                                            - reference.getSample (ch, sample)));
        const auto diffDb = juce::Decibels::gainToDecibels (worstDiff, -160.0f);
        check (diffDb < -120.0f, "null-test a moduli spenti < -120 dBFS", juce::String (diffDb, 1) + " dBFS");
    }

    // ---- catena generata: stabilità a più sample rate, mono e stereo
    for (double sampleRate : { 44100.0, 48000.0, 96000.0 })
        for (int channels : { 1, 2 })
        {
            prepare (sampleRate);
            processor.applyPrompt ("voce trap aggressiva ma non stridula", vf::RulesEngine::defaultProfileId());

            juce::AudioBuffer<float> buffer (juce::jmax (2, channels), blockSize);
            float worstPeak = 0.0f;
            bool finite = true;
            for (int i = 0; i < 20; ++i)
            {
                fillVoiceLike (buffer, sampleRate, 0.5f);         // -6 dBFS
                processor.processBlock (buffer, midi);
                finite = finite && isFinite (buffer);
                worstPeak = juce::jmax (worstPeak, buffer.getMagnitude (0, blockSize));
            }
            const juce::String label (juce::String (static_cast<int> (sampleRate)) + " Hz, "
                                      + juce::String (channels) + " ch");
            check (finite, "nessun NaN/Inf — " + label);
            check (worstPeak <= 1.0f, "nessun clipping con ingresso a -6 dBFS — " + label,
                   juce::String (juce::Decibels::gainToDecibels (worstPeak), 2) + " dBFS");
        }

    // ---- cambio di prompt mentre l'audio gira: i target sono smussati, non deve schioccare
    {
        const double sampleRate = 48000.0;
        prepare (sampleRate);
        processor.applyPrompt ("cantautore acustico, voce naturale", vf::RulesEngine::defaultProfileId());

        auto worstJumpWith = [&] (bool changePrompt)
        {
            prepare (sampleRate);
            processor.applyPrompt ("cantautore acustico, voce naturale", vf::RulesEngine::defaultProfileId());
            juce::AudioBuffer<float> buffer (2, blockSize);
            float worst = 0.0f;
            for (int i = 0; i < 12; ++i)
            {
                fillVoiceLike (buffer, sampleRate, 0.4f);
                if (i == 6 && changePrompt)
                    processor.applyPrompt ("metal, voce urlata molto aggressiva", vf::RulesEngine::defaultProfileId());
                processor.processBlock (buffer, midi);
                if (i >= 6) worst = juce::jmax (worst, maxJump (buffer));
            }
            return worst;
        };

        const auto baseline = worstJumpWith (false);
        const auto changed = worstJumpWith (true);
        // il riferimento e' la catena stessa: il cambio di preset non deve saltare piu' del normale
        check (changed < baseline * 3.0f + 0.02f, "cambio di prompt in corsa senza click",
               "salto normale " + juce::String (baseline, 4) + ", col cambio " + juce::String (changed, 4));
    }

    // ---- le mandate sono parallele: spegnendole la voce resta quella
    {
        const double sampleRate = 48000.0;
        prepare (sampleRate);
        processor.applyPrompt ("pop moderno, voce brillante", vf::RulesEngine::defaultProfileId());

        auto renderFirstBlock = [&] (bool sendsOn)
        {
            prepare (sampleRate);
            for (auto* id : { "revOn", "dlyOn", "fxOn" })
                if (auto* parameter = processor.apvts.getParameter (id))
                    parameter->setValueNotifyingHost (sendsOn ? 1.0f : 0.0f);

            juce::AudioBuffer<float> buffer (2, blockSize);
            fillVoiceLike (buffer, sampleRate, 0.4f);
            processor.processBlock (buffer, midi);
            return buffer;
        };

        const auto withSends = renderFirstBlock (true);
        const auto without   = renderFirstBlock (false);
        float worstDiff = 0.0f;
        for (int sample = 0; sample < blockSize; ++sample)
            worstDiff = juce::jmax (worstDiff, std::abs (withSends.getSample (0, sample)
                                                         - without.getSample (0, sample)));
        // il primo blocco è ancora quasi asciutto: le code arrivano dopo, non in serie
        check (worstDiff < 0.2f, "le mandate sono parallele, non in serie",
               "differenza " + juce::String (worstDiff, 3));
    }

    // ---- profili artista: catena completa e mandate imposte
    {
        prepare (48000.0);
        for (auto* artist : { "sfera ebbasta", "shiva", "tony boy", "glockyy", "gue pequeno", "capo plaza" })
        {
            processor.applyPrompt (juce::String (artist), vf::RulesEngine::defaultProfileId());
            const auto& preset = processor.getLastPreset();

            const bool hasReverb = preset.sendOfGroup ("reverb") != nullptr;
            const bool hasDelay  = preset.sendOfGroup ("delay")  != nullptr;
            const bool satOn     = preset.find ("sat") != nullptr && preset.find ("sat")->enabled;
            check (preset.artist.isNotEmpty() && hasReverb && hasDelay && satOn,
                   juce::String (artist) + ": catena completa (saturazione + riverbero + delay)");

            juce::AudioBuffer<float> buffer (2, blockSize);
            bool finite = true;
            float peak = 0.0f;
            for (int i = 0; i < 12; ++i)
            {
                fillVoiceLike (buffer, 48000.0, 0.5f);
                processor.processBlock (buffer, midi);
                finite = finite && isFinite (buffer);
                peak = juce::jmax (peak, buffer.getMagnitude (0, blockSize));
            }
            check (finite && peak <= 1.0f, juce::String (artist) + ": suona pulito, senza clipping",
                   juce::String (juce::Decibels::gainToDecibels (peak), 2) + " dBFS");
        }
    }

    // ---- gain staging misurato sul DSP vero, a gain d'ingresso NORMALE e BASSO.
    //      È la contro-prova di tools/tests/test_gain_staging.py: là si misura sul preset,
    //      qui si misura la riduzione che i compressori fanno davvero sul segnale.
    //      Il caso a -18 dBFS è quello lamentato dall'utente ("saturata anche con poco gain"):
    //      se i makeup fossero slegati dalla riduzione, la catena spingerebbe lo stesso.
    {
        const double sampleRate = 48000.0;
        const float maxStageGrDb = 6.0f;      // CLAUDE.md: nessuno stadio oltre 6 dB
        const float maxLimiterGrDb = 3.0f;    // CHAIN_ARCHITECTURE: il limiter è un tetto, non un effetto

        struct Level { const char* name; float peakDbfs; };
        for (auto level : { Level { "-6 dBFS (nominale)", -6.0f }, Level { "-18 dBFS (gain basso)", -18.0f } })
        {
            for (auto* artist : { "sfera ebbasta", "shiva", "tony boy", "glockyy", "gue pequeno", "capo plaza" })
            {
                prepare (sampleRate);
                processor.applyPrompt (juce::String (artist), vf::RulesEngine::defaultProfileId());

                juce::AudioBuffer<float> buffer (2, blockSize);
                float gr1 = 0.0f, gr2 = 0.0f, grLim = 0.0f, outPeak = 0.0f;
                for (int i = 0; i < 24; ++i)
                {
                    fillAtPeak (buffer, sampleRate, level.peakDbfs);
                    processor.processBlock (buffer, midi);
                    if (i < 4) continue;                       // lascia assestare envelope e smoothing
                    gr1    = juce::jmax (gr1,    processor.getMeters().comp1Gr.load());
                    gr2    = juce::jmax (gr2,    processor.getMeters().comp2Gr.load());
                    grLim  = juce::jmax (grLim,  processor.getMeters().limGr.load());
                    outPeak = juce::jmax (outPeak, buffer.getMagnitude (0, blockSize));
                }

                const juce::String detail ("comp1 " + juce::String (gr1, 2) + " dB, comp2 "
                                           + juce::String (gr2, 2) + " dB, limiter " + juce::String (grLim, 2)
                                           + " dB, picco " + juce::String (juce::Decibels::gainToDecibels (outPeak), 1)
                                           + " dBFS");
                check (gr1 <= maxStageGrDb && gr2 <= maxStageGrDb,
                       juce::String (artist) + " @ " + level.name + ": nessuno stadio oltre 6 dB", detail);
                check (grLim <= maxLimiterGrDb,
                       juce::String (artist) + " @ " + level.name + ": il limiter resta un tetto", detail);
                check (outPeak <= 1.0f,
                       juce::String (artist) + " @ " + level.name + ": nessuno stadio clippa", detail);
                std::cout << "        " << detail << std::endl;
            }
        }
    }


    /*  ---- SECONDO GIRO DI TARATURA (knowledge/TUNING.md, sezione "dopo le correzioni del DSP").

        Il de-esser adesso e' un crossover vero: `ds1Thresh`/`ds2Thresh` e i `range` erano stati
        scelti quando il modulo era un allpass che abbassava il CORPO della voce e alzava le esse.
        Qui la banda sibilante si misura davvero, con i due de-esser accesi e spenti sullo stesso
        segnale: la differenza a 6.8 kHz e' quello che tolgono, quella a 420 Hz e' il lisp che non
        devono fare.  */
    {
        const double sampleRate = 48000.0;
        const int    span   = 24000;      // un solo blocco continuo: il Goertzel vuole fase continua
        const int    window = 12000;      // 420 e 6800 Hz cadono su un bin intero a 48 kHz
        const int    offset = span - window;

        const float minRelativeCutDb = 2.0f;    // sotto questo il de-esser non sta togliendo niente
        const float maxRelativeCutDb = 8.0f;    // oltre questo la 's' sparisce e la voce blesa
        const float maxBodyCutDb     = 0.7f;    // il corpo della voce non si tocca: e' il lisp

        struct Bands { float body = 0.0f, sibilant = 0.0f, ds1Gr = 0.0f, ds2Gr = 0.0f; };

        auto run = [&] (const char* artist, float peakDbfs, bool deEssersOn)
        {
            processor.setRateAndBufferSizeDetails (sampleRate, span);
            processor.prepareToPlay (sampleRate, span);
            processor.applyPrompt (juce::String (artist), vf::RulesEngine::defaultProfileId());
            if (! deEssersOn)
                for (auto* id : { "ds1On", "ds2On" })
                    if (auto* p = processor.apvts.getParameter (id)) p->setValueNotifyingHost (0.0f);

            juce::AudioBuffer<float> buffer (2, span);
            for (int i = 0; i < 2; ++i)          // il primo giro assesta smoothing e inviluppi
            {
                fillAtPeakSibilant (buffer, sampleRate, peakDbfs);
                processor.processBlock (buffer, midi);
            }
            Bands bands;
            bands.body     = lineDb (buffer.getReadPointer (0) + offset, window,  420.0, sampleRate);
            bands.sibilant = lineDb (buffer.getReadPointer (0) + offset, window, 6800.0, sampleRate);
            bands.ds1Gr    = processor.getMeters().ds1Gr.load();
            bands.ds2Gr    = processor.getMeters().ds2Gr.load();
            return bands;
        };

        struct Level { const char* name; float peakDbfs; };
        for (auto level : { Level { "-6 dBFS (nominale)", -6.0f }, Level { "-18 dBFS (gain basso)", -18.0f } })
        {
            for (auto* artist : kArtists)
            {
                const auto without = run (artist, level.peakDbfs, false);
                const auto with    = run (artist, level.peakDbfs, true);
                const auto sibilantCut = without.sibilant - with.sibilant;    // positivo = tolto alle esse
                const auto bodyCut     = without.body     - with.body;        // positivo = tolto al corpo: lisp
                const auto relativeCut = sibilantCut - bodyCut;               // la de-essatura vera

                const juce::String detail ("banda alta " + juce::String (-sibilantCut, 2) + " dB, corpo "
                                           + juce::String (-bodyCut, 2) + " dB, relativo "
                                           + juce::String (-relativeCut, 2) + " dB, GR ds1 "
                                           + juce::String (with.ds1Gr, 2) + " ds2 " + juce::String (with.ds2Gr, 2));
                std::cout << "        " << artist << " @ " << level.name << ": " << detail << std::endl;

                if (level.peakDbfs <= -18.0f) continue;   // a gain basso le esse stanno sotto soglia: giusto cosi'
                check (relativeCut >= minRelativeCutDb,
                       juce::String (artist) + ": il de-esser toglie davvero alle sibilanti", detail);
                check (relativeCut <= maxRelativeCutDb,
                       juce::String (artist) + ": il de-esser non spegne le consonanti", detail);
                check (bodyCut <= maxBodyCutDb,
                       juce::String (artist) + ": il corpo della voce non viene abbassato (niente lisp)", detail);
            }
        }
    }

    /*  ---- satTilt: adesso e' un tilt spettrale vero (due shelf speculari a 700 Hz) e agisce anche
        a drive 0. Va misurato quanto inclina, perche' si somma all'EQ tonale che gia' fa brillantezza. */
    {
        const double sampleRate = 48000.0;
        auto set = [&processor] (const char* id, float value)
        {
            if (auto* p = processor.apvts.getParameter (id)) p->setValueNotifyingHost (p->convertTo0to1 (value));
        };

        auto responseDb = [&] (float tilt, float frequency)
        {
            prepare (sampleRate);
            setAllModules (processor, false);
            if (auto* p = processor.apvts.getParameter ("satOn")) p->setValueNotifyingHost (1.0f);
            set ("satDrive", 0.0f); set ("satTilt", tilt);

            juce::AudioBuffer<float> buffer (2, blockSize);
            float inSum = 0.0f, outSum = 0.0f;
            for (int block = 0; block < 12; ++block)
            {
                for (int sample = 0; sample < blockSize; ++sample)
                {
                    const auto phase = juce::MathConstants<double>::twoPi * frequency
                                       * (block * blockSize + sample) / sampleRate;
                    const auto value = 0.25f * static_cast<float> (std::sin (phase));
                    buffer.setSample (0, sample, value);
                    buffer.setSample (1, sample, value);
                }
                if (block >= 6) for (int i = 0; i < blockSize; ++i) inSum += std::abs (buffer.getSample (0, i));
                processor.processBlock (buffer, midi);
                if (block >= 6) for (int i = 0; i < blockSize; ++i) outSum += std::abs (buffer.getSample (0, i));
            }
            return juce::Decibels::gainToDecibels (outSum / juce::jmax (1.0e-9f, inSum));
        };

        /*  Il valore e' META' dell'inclinazione: shelf bassa a -satTilt, shelf alta a +satTilt.
            1.5 dB e' il massimo che le regole possono produrre (brightness e' limitata a 1.0);
            3.0 dB era il massimo di prima, ed e' qui per far vedere quanto inclinava davvero. */
        for (auto tilt : { 3.0f, 1.5f })
            std::cout << "        satTilt " << juce::String (tilt, 1) << " dB -> inclinazione misurata "
                      << juce::String (responseDb (tilt, 8000.0f) - responseDb (tilt, 200.0f), 2)
                      << " dB fra 200 Hz e 8 kHz" << std::endl;

        const float worstTilt = 1.5f;
        const auto span = responseDb (worstTilt, 8000.0f) - responseDb (worstTilt, 200.0f);
        check (span <= 3.5f, "satTilt: l'inclinazione resta un colore, non una seconda EQ",
               juce::String (span, 2) + " dB di dislivello a satTilt " + juce::String (worstTilt, 1));
    }


    /*  ---- DUCKING delle mandate. Prima della correzione B5 il detector non veniva aggiornato per
        delay e doubler: i `duck_db` delle regole erano numeri scelti al buio. Adesso hanno effetto su
        tutti e tre i bus, e vanno misurati: quanto scende la mandata SOTTO la parola, e quanto risale
        nel vuoto fra le parole. Il wet si isola per sottrazione (stessa catena, mandate spente). */
    {
        const double sampleRate = 48000.0;
        const int span      = 38400;      // 800 ms in un blocco solo: il ducking ha 180 ms di rilascio
        const int wordStart = 2400,  wordCount = 12000;    //  50 → 300 ms: la parola
        const int gapStart  = 21600, gapCount  = 14400;    // 450 → 750 ms: il vuoto dopo la parola

        auto render = [&] (const char* artist, bool sendsOn, float duckOverride, const char* onlyBus)
        {
            processor.setRateAndBufferSizeDetails (sampleRate, span);
            processor.prepareToPlay (sampleRate, span);
            processor.applyPrompt (juce::String (artist), vf::RulesEngine::defaultProfileId());

            for (auto* id : { "revOn", "dlyOn", "fxOn" })
                if (auto* p = processor.apvts.getParameter (id))
                {
                    const auto wanted = sendsOn && (onlyBus == nullptr || juce::String (id) == onlyBus);
                    p->setValueNotifyingHost (wanted ? p->getValue() : 0.0f);
                }
            if (duckOverride >= 0.0f)
                for (auto* id : { "revDuck", "dlyDuck", "fxDuck" })
                    if (auto* p = processor.apvts.getParameter (id))
                        p->setValueNotifyingHost (p->convertTo0to1 (duckOverride));

            juce::AudioBuffer<float> buffer (2, span);
            buffer.clear();
            processor.processBlock (buffer, midi);          // un blocco muto: assesta gli smoothing
            fillWordThenGap (buffer, sampleRate, -6.0f, 0.3);
            processor.processBlock (buffer, midi);
            return buffer;
        };

        auto wetOf = [] (const juce::AudioBuffer<float>& withSends, const juce::AudioBuffer<float>& dryOnly)
        {
            juce::AudioBuffer<float> wet (1, withSends.getNumSamples());
            for (int i = 0; i < withSends.getNumSamples(); ++i)
                wet.setSample (0, i, withSends.getSample (0, i) - dryOnly.getSample (0, i));
            return wet;
        };

        std::cout << "  --- ducking: quanto scende ogni mandata sotto la parola, e quanto risale dopo" << std::endl;
        struct Bus { const char* id; const char* label; };
        for (auto* artist : kArtists)
            for (auto bus : { Bus { "revOn", "riverbero" }, Bus { "dlyOn", "delay" }, Bus { "fxOn", "doubler" } })
            {
                const auto dry    = render (artist, false, -1.0f, bus.id);
                const auto ducked = render (artist, true,  -1.0f, bus.id);
                const auto flat   = render (artist, true,   0.0f, bus.id);

                const auto wetDucked = wetOf (ducked, dry);
                const auto wetFlat   = wetOf (flat,   dry);
                const auto wetLevel  = rmsDb (wetDucked, wordStart, wordCount);
                if (wetLevel < -100.0f) continue;                  // il profilo non usa questo bus

                const auto underWord = rmsDb (wetFlat, wordStart, wordCount) - wetLevel;
                const auto inGap     = rmsDb (wetFlat, gapStart,  gapCount)
                                     - rmsDb (wetDucked, gapStart, gapCount);

                const juce::String detail (juce::String (bus.label) + ": sotto la parola -"
                                           + juce::String (underWord, 2) + " dB, nel vuoto -"
                                           + juce::String (inGap, 2) + " dB");
                std::cout << "        " << artist << " " << detail << std::endl;

                if (juce::String (bus.id) == "fxOn")
                {
                    // Il doubler allarga la voce MENTRE parla: duckarlo come il riverbero lo cancella
                    // proprio dove serve. Prima della correzione B5 il suo duck non aveva effetto e
                    // il numero nelle regole (8 dB) non se ne era mai accorto.
                    check (underWord <= 3.0f, juce::String (artist) + ": il ducking non cancella il doubler sulla parola", detail);
                }
                else
                {
                    check (underWord >= 3.0f, juce::String (artist) + ": " + bus.label + " scende davvero sotto la parola", detail);
                    check (inGap <= underWord - 1.5f, juce::String (artist) + ": " + bus.label + " risale fra le parole", detail);
                }
            }
    }

    // ---- blocchi di lunghezza variabile: il contratto e' "<= maximumBlockSize", non "="
    {
        const double sampleRate = 48000.0;
        prepare (sampleRate);
        processor.applyPrompt ("voce tipo sfera ebbasta", vf::RulesEngine::defaultProfileId());

        std::mt19937 generator (99);
        std::uniform_int_distribution<int> lengths (1, blockSize);
        bool finite = true;
        for (int i = 0; i < 60; ++i)
        {
            const auto count = (i < 3) ? 64 : lengths (generator);      // il caso che faceva scrivere fuori
            juce::AudioBuffer<float> buffer (2, count);
            fillVoiceLike (buffer, sampleRate, 0.4f);
            processor.processBlock (buffer, midi);
            finite = finite && isFinite (buffer);
        }
        check (finite, "blocchi piu' corti di quello dichiarato: nessuna scrittura fuori, nessun NaN");
    }

    // ---- il de-esser deve togliere alle sibilanti, non al corpo della voce
    {
        const double sampleRate = 48000.0;
        auto set = [&processor] (const char* id, float value)
        {
            if (auto* p = processor.apvts.getParameter (id)) p->setValueNotifyingHost (p->convertTo0to1 (value));
        };

        auto responseDb = [&] (float frequency)
        {
            prepare (sampleRate);
            setAllModules (processor, false);
            if (auto* p = processor.apvts.getParameter ("ds1On")) p->setValueNotifyingHost (1.0f);
            set ("ds1Freq", 6650.0f); set ("ds1Thresh", -20.0f); set ("ds1Range", 6.0f); set ("ds1Mode", 0.0f);

            juce::AudioBuffer<float> buffer (2, blockSize);
            float inSum = 0.0f, outSum = 0.0f;
            for (int block = 0; block < 12; ++block)
            {
                for (int sample = 0; sample < blockSize; ++sample)
                {
                    const auto phase = juce::MathConstants<double>::twoPi * frequency
                                       * (block * blockSize + sample) / sampleRate;
                    const auto value = 0.25f * static_cast<float> (std::sin (phase));
                    buffer.setSample (0, sample, value);
                    buffer.setSample (1, sample, value);
                }
                if (block >= 6) for (int i = 0; i < blockSize; ++i) inSum += std::abs (buffer.getSample (0, i));
                processor.processBlock (buffer, midi);
                if (block >= 6) for (int i = 0; i < blockSize; ++i) outSum += std::abs (buffer.getSample (0, i));
            }
            return juce::Decibels::gainToDecibels (outSum / juce::jmax (1.0e-9f, inSum));
        };

        const auto lowDb = responseDb (200.0f);
        const auto highDb = responseDb (9000.0f);
        check (highDb < lowDb - 2.0f, "de-esser: attenua le sibilanti piu' del corpo della voce",
               "200 Hz " + juce::String (lowDb, 2) + " dB, 9 kHz " + juce::String (highDb, 2) + " dB");
        check (lowDb > -1.0f, "de-esser: il corpo della voce resta intatto",
               juce::String (lowDb, 2) + " dB a 200 Hz");
    }

    // ---- nessuna allocazione sul thread audio
    {
        const double sampleRate = 48000.0;
        prepare (sampleRate);
        processor.applyPrompt ("voce tipo sfera ebbasta", vf::RulesEngine::defaultProfileId());

        juce::AudioBuffer<float> buffer (2, blockSize);
        fillVoiceLike (buffer, sampleRate, 0.4f);
        for (int i = 0; i < 4; ++i) processor.processBlock (buffer, midi);   // assestamento

        vfalloc::count.store (0);
        vfalloc::armed.store (true);
        for (int i = 0; i < 10; ++i) processor.processBlock (buffer, midi);
        vfalloc::armed.store (false);
        const auto steady = vfalloc::count.load();
        check (steady == 0, "processBlock non alloca (regime)",
               juce::String (steady) + " allocazioni in 10 blocchi");

        if (auto* p = processor.apvts.getParameter ("c1Thresh"))
            p->setValueNotifyingHost (p->convertTo0to1 (-18.0f));
        vfalloc::count.store (0);
        vfalloc::armed.store (true);
        processor.processBlock (buffer, midi);
        vfalloc::armed.store (false);
        const auto moved = vfalloc::count.load();
        check (moved == 0, "processBlock non alloca dopo un movimento di automazione",
               juce::String (moved) + " allocazioni");
    }

    // ---- il limiter e' un tetto: niente puo' stargli dopo
    {
        const double sampleRate = 48000.0;
        prepare (sampleRate);
        setAllModules (processor, false);
        auto set = [&processor] (const char* id, float value)
        {
            if (auto* p = processor.apvts.getParameter (id)) p->setValueNotifyingHost (p->convertTo0to1 (value));
        };
        if (auto* p = processor.apvts.getParameter ("limOn")) p->setValueNotifyingHost (1.0f);
        if (auto* p = processor.apvts.getParameter ("revOn")) p->setValueNotifyingHost (1.0f);
        set ("limCeiling", -1.0f); set ("outGain", 12.0f); set ("revSend", -6.0f);

        juce::AudioBuffer<float> buffer (2, blockSize);
        float peak = 0.0f;
        for (int i = 0; i < 24; ++i)
        {
            fillVoiceLike (buffer, sampleRate, 0.7f);
            processor.processBlock (buffer, midi);
            if (i >= 4) peak = juce::jmax (peak, buffer.getMagnitude (0, blockSize));
        }
        const auto peakDb = juce::Decibels::gainToDecibels (peak);
        check (peakDb <= -0.5f, "il ceiling tiene anche con output gain e mandate accese",
               juce::String (peakDb, 2) + " dBFS con ceiling -1.0");
    }

    // ---- il ducking delle mandate funziona anche senza riverbero
    {
        const double sampleRate = 48000.0;
        auto delayLevel = [&] (float duckDb)
        {
            prepare (sampleRate);
            setAllModules (processor, false);
            auto set = [&processor] (const char* id, float value)
            {
                if (auto* p = processor.apvts.getParameter (id)) p->setValueNotifyingHost (p->convertTo0to1 (value));
            };
            if (auto* p = processor.apvts.getParameter ("dlyOn")) p->setValueNotifyingHost (1.0f);
            if (auto* p = processor.apvts.getParameter ("revOn")) p->setValueNotifyingHost (0.0f);
            if (auto* p = processor.apvts.getParameter ("dlySync")) p->setValueNotifyingHost (0.0f);
            set ("dlyTime", 60.0f);          // corto: l'eco deve rientrare nella finestra di misura
            set ("dlySend", -10.0f); set ("dlyDuck", duckDb); set ("dlyFeedback", 30.0f);

            juce::AudioBuffer<float> buffer (2, blockSize), reference (2, blockSize);
            float sum = 0.0f;
            for (int i = 0; i < 20; ++i)
            {
                fillVoiceLike (buffer, sampleRate, 0.4f);
                for (int ch = 0; ch < 2; ++ch) reference.copyFrom (ch, 0, buffer, ch, 0, blockSize);
                processor.processBlock (buffer, midi);
                if (i >= 10)
                    for (int sample = 0; sample < blockSize; ++sample)
                        sum += std::abs (buffer.getSample (0, sample) - reference.getSample (0, sample));
            }
            return juce::Decibels::gainToDecibels (sum / (10.0f * blockSize));
        };

        const auto noDuck = delayLevel (0.0f);
        const auto ducked = delayLevel (9.0f);
        check (ducked < noDuck - 1.0f, "il ducking del delay funziona con il riverbero spento",
               juce::String (noDuck, 2) + " dB senza duck, " + juce::String (ducked, 2) + " dB con duck");
    }

    // ---- polarita' invertita + mix parziale non deve cancellare la voce
    {
        const double sampleRate = 48000.0;
        prepare (sampleRate);
        setAllModules (processor, false);
        if (auto* p = processor.apvts.getParameter ("polarity")) p->setValueNotifyingHost (1.0f);
        if (auto* mix = processor.apvts.getParameter ("mix"))
            mix->setValueNotifyingHost (mix->convertTo0to1 (50.0f));

        juce::AudioBuffer<float> buffer (2, blockSize);
        float peak = 0.0f;
        for (int i = 0; i < 8; ++i)
        {
            fillVoiceLike (buffer, sampleRate, 0.5f);
            processor.processBlock (buffer, midi);
            if (i >= 4) peak = juce::jmax (peak, buffer.getMagnitude (0, blockSize));
        }
        check (juce::Decibels::gainToDecibels (peak) > -20.0f,
               "polarita' invertita con mix al 50 % non cancella il segnale",
               juce::String (juce::Decibels::gainToDecibels (peak), 1) + " dBFS");
    }

    // ---- la latenza dichiarata dev'essere quella vera
    {
        const double sampleRate = 48000.0;
        prepare (sampleRate);
        setAllModules (processor, false);

        juce::AudioBuffer<float> buffer (2, blockSize);
        buffer.clear();
        buffer.setSample (0, 100, 1.0f);
        buffer.setSample (1, 100, 1.0f);
        processor.processBlock (buffer, midi);

        int firstNonZero = -1;
        for (int sample = 0; sample < blockSize && firstNonZero < 0; ++sample)
            if (std::abs (buffer.getSample (0, sample)) > 0.01f) firstNonZero = sample;

        const auto measured = firstNonZero >= 0 ? firstNonZero - 100 : -1;
        check (measured == processor.getLatencySamples(), "latenza dichiarata == latenza misurata",
               "misurata " + juce::String (measured) + ", dichiarata "
               + juce::String (processor.getLatencySamples()));
    }

    // ---- la saturazione tratta L e R allo stesso modo durante la rampa del drive
    {
        const double sampleRate = 48000.0;
        prepare (sampleRate);
        setAllModules (processor, false);
        if (auto* p = processor.apvts.getParameter ("satOn")) p->setValueNotifyingHost (1.0f);
        auto setDrive = [&processor] (float value)
        {
            if (auto* p = processor.apvts.getParameter ("satDrive"))
                p->setValueNotifyingHost (p->convertTo0to1 (value));
        };
        setDrive (0.0f);

        juce::AudioBuffer<float> buffer (2, blockSize);
        float worst = 0.0f;
        for (int i = 0; i < 12; ++i)
        {
            fillVoiceLike (buffer, sampleRate, 0.5f);       // identico su L e R
            if (i == 4) setDrive (100.0f);
            processor.processBlock (buffer, midi);
            for (int sample = 0; sample < blockSize; ++sample)
                worst = juce::jmax (worst, std::abs (buffer.getSample (0, sample)
                                                     - buffer.getSample (1, sample)));
        }
        check (juce::Decibels::gainToDecibels (worst, -120.0f) < -60.0f,
               "un ingresso mono resta mono anche durante la rampa del drive",
               "differenza L-R " + juce::String (juce::Decibels::gainToDecibels (worst, -120.0f), 1) + " dBFS");
    }

    // ---- la divisione del delay e' quella che dice il preset
    {
        const double sampleRate = 48000.0;
        auto echoPosition = [&] (float divisionIndex)
        {
            prepare (sampleRate);
            setAllModules (processor, false);
            auto set = [&processor] (const char* id, float value)
            {
                if (auto* p = processor.apvts.getParameter (id))
                    p->setValueNotifyingHost (p->convertTo0to1 (value));
            };
            if (auto* p = processor.apvts.getParameter ("dlyOn")) p->setValueNotifyingHost (1.0f);
            if (auto* p = processor.apvts.getParameter ("dlySync")) p->setValueNotifyingHost (1.0f);
            set ("dlyDivision", divisionIndex);
            set ("dlySend", -6.0f); set ("dlyDuck", 0.0f); set ("dlyHpf", 100.0f); set ("dlyLpf", 8000.0f);

            juce::AudioBuffer<float> buffer (2, blockSize);
            int position = -1, offset = 0;
            for (int block = 0; block < 80 && position < 0; ++block)
            {
                buffer.clear();
                if (block == 0) { buffer.setSample (0, 0, 1.0f); buffer.setSample (1, 0, 1.0f); }
                processor.processBlock (buffer, midi);
                for (int sample = (block == 0 ? 64 : 0); sample < blockSize && position < 0; ++sample)
                    if (std::abs (buffer.getSample (0, sample)) > 0.02f) position = offset + sample;
                offset += blockSize;
            }
            return position;
        };

        const auto quarter = echoPosition (0.0f);        // 1/4 = 500 ms a 120 bpm = 24000 campioni
        const auto dotted  = echoPosition (1.0f);        // 1/8 puntato = 375 ms = 18000 campioni
        check (quarter > 0 && dotted > 0 && quarter > dotted + 2000,
               "il delay usa la divisione del preset, non sempre 1/8 puntato",
               "1/4 a " + juce::String (quarter) + " campioni, 1/8 puntato a " + juce::String (dotted));
    }

    // ---- nessun parametro finto: ogni controllo deve ARRIVARE al DSP
    {
        // Il criterio giusto non è "cambia il suono su questo segnale" (una soglia di gate su un
        // segnale sempre sopra soglia non cambia niente, ed è giusto così), ma "il valore arriva
        // alle impostazioni che il DSP riceve". È esattamente il difetto B10: parametri dichiarati
        // e mai letti.
        prepare (48000.0);
        processor.applyPrompt ("voce tipo sfera ebbasta", vf::RulesEngine::defaultProfileId());

        juce::StringArray dead;
        for (auto* parameter : processor.getParameters())
        {
            auto* withId = dynamic_cast<juce::AudioProcessorParameterWithID*> (parameter);
            if (withId == nullptr || withId->paramID == "bypass") continue;

            const auto before = processor.currentSettings();
            const auto original = parameter->getValue();
            parameter->setValueNotifyingHost (original < 0.5f ? 1.0f : 0.0f);
            const auto after = processor.currentSettings();
            parameter->setValueNotifyingHost (original);

            // i due snapshot nascono dallo stesso costruttore: il riempimento è identico
            if (std::memcmp (&before, &after, sizeof (vf::ChainSettings)) == 0)
                dead.add (withId->paramID);
        }
        check (dead.isEmpty(), "nessun parametro finto: ogni controllo arriva al DSP",
               dead.isEmpty() ? juce::String() : "inerti: " + dead.joinIntoString (", "));
    }

    // ---- R1: lo stesso ingresso deve dare lo stesso risultato con qualunque dimensione di buffer
    {
        const double sampleRate = 48000.0;
        const int total = 4096;
        juce::AudioBuffer<float> source (2, total);
        fillVoiceLike (source, sampleRate, 0.5f);

        auto renderWithBlocks = [&] (int chunk)
        {
            processor.setRateAndBufferSizeDetails (sampleRate, chunk);
            processor.prepareToPlay (sampleRate, chunk);
            processor.applyPrompt ("voce tipo sfera ebbasta", vf::RulesEngine::defaultProfileId());

            juce::AudioBuffer<float> out (2, total);
            for (int ch = 0; ch < 2; ++ch) out.copyFrom (ch, 0, source, ch, 0, total);
            juce::AudioBuffer<float> view (2, chunk);
            for (int start = 0; start + chunk <= total; start += chunk)
            {
                for (int ch = 0; ch < 2; ++ch) view.copyFrom (ch, 0, out, ch, start, chunk);
                processor.processBlock (view, midi);
                for (int ch = 0; ch < 2; ++ch) out.copyFrom (ch, start, view, ch, 0, chunk);
            }
            return out;
        };

        const auto small = renderWithBlocks (64);
        const auto large = renderWithBlocks (1024);
        float difference = 0.0f;
        for (int ch = 0; ch < 2; ++ch)
            for (int sample = 0; sample < total; ++sample)
                difference = juce::jmax (difference, std::abs (small.getSample (ch, sample)
                                                               - large.getSample (ch, sample)));
        check (juce::Decibels::gainToDecibels (difference, -200.0f) < -60.0f,
               "il risultato non dipende dalla dimensione del buffer (ascolto == bounce)",
               "blocchi 64 vs 1024: " + juce::String (juce::Decibels::gainToDecibels (difference, -200.0f), 1) + " dBFS");
    }

    // ---- R8: nella banda di isteresi il gate resta APERTO, non si congela a mezza corsa
    {
        const double sampleRate = 48000.0;
        prepare (sampleRate);
        setAllModules (processor, false);
        auto set = [&processor] (const char* id, float value)
        {
            if (auto* p = processor.apvts.getParameter (id)) p->setValueNotifyingHost (p->convertTo0to1 (value));
        };
        if (auto* p = processor.apvts.getParameter ("gateOn")) p->setValueNotifyingHost (1.0f);
        set ("gateThresh", -30.0f); set ("gateRange", 24.0f); set ("gateAtk", 1.0f); set ("gateRel", 50.0f);
        set ("gateKeyLo", 60.0f); set ("gateKeyHi", 12000.0f);

        auto levelAfter = [&] (float amplitude, int blocks)
        {
            juce::AudioBuffer<float> buffer (2, blockSize);
            float last = 0.0f;
            for (int i = 0; i < blocks; ++i)
            {
                for (int sample = 0; sample < blockSize; ++sample)
                {
                    const auto t = (i * blockSize + sample) / sampleRate;
                    const auto value = amplitude * static_cast<float> (
                        std::sin (juce::MathConstants<double>::twoPi * 300.0 * t));
                    buffer.setSample (0, sample, value);
                    buffer.setSample (1, sample, value);
                }
                processor.processBlock (buffer, midi);
                last = buffer.getMagnitude (0, blockSize);
            }
            return juce::Decibels::gainToDecibels (last / juce::jmax (1.0e-6f, amplitude));
        };

        levelAfter (0.2f, 12);                                  // ben sopra soglia: il gate apre
        const auto inBand = levelAfter (0.022f, 12);            // dentro l'isteresi: deve restare aperto
        check (inBand > -3.0f, "nella banda di isteresi il gate resta aperto invece di congelarsi",
               juce::String (inBand, 2) + " dB di attenuazione");
    }

    // ---- R7: spegnere una mandata non tronca la coda, e la coda dichiarata la contiene
    {
        const double sampleRate = 48000.0;
        prepare (sampleRate);
        setAllModules (processor, false);
        auto set = [&processor] (const char* id, float value)
        {
            if (auto* p = processor.apvts.getParameter (id)) p->setValueNotifyingHost (p->convertTo0to1 (value));
        };
        if (auto* p = processor.apvts.getParameter ("revOn")) p->setValueNotifyingHost (1.0f);
        set ("revSend", -6.0f); set ("revDuck", 0.0f); set ("revDecay", 3.0f);

        juce::AudioBuffer<float> buffer (2, blockSize);
        for (int i = 0; i < 16; ++i)
        {
            fillVoiceLike (buffer, sampleRate, 0.5f);
            processor.processBlock (buffer, midi);
        }
        if (auto* p = processor.apvts.getParameter ("revOn")) p->setValueNotifyingHost (0.0f);

        buffer.clear();
        processor.processBlock (buffer, midi);
        const auto justAfter = juce::Decibels::gainToDecibels (buffer.getMagnitude (0, blockSize), -120.0f);
        check (justAfter > -60.0f, "spegnere il riverbero non tronca la coda di colpo",
               juce::String (justAfter, 1) + " dBFS nel blocco successivo");

        set ("dlyTime", 1200.0f); set ("dlyFeedback", 40.0f);
        if (auto* p = processor.apvts.getParameter ("dlyOn")) p->setValueNotifyingHost (1.0f);
        if (auto* p = processor.apvts.getParameter ("dlySync")) p->setValueNotifyingHost (0.0f);
        processor.processBlock (buffer, midi);
        check (processor.getTailLengthSeconds() >= 1.2,
               "la coda dichiarata all'host contiene davvero il delay",
               juce::String (processor.getTailLengthSeconds(), 2) + " s con delay da 1,2 s");
    }

    // ---- R2: la catena regge piu' di due canali senza timbri diversi fra loro
    {
        vf::ChainDsp chain;
        const double sampleRate = 48000.0;
        chain.prepare (sampleRate, blockSize, 4);
        vf::ChainSettings settings;
        chain.setSettings (settings);

        juce::AudioBuffer<float> buffer (4, blockSize);
        fillVoiceLike (buffer, sampleRate, 0.4f);
        for (int i = 0; i < 8; ++i)
        {
            fillVoiceLike (buffer, sampleRate, 0.4f);
            chain.process (buffer, 120.0);
        }

        bool finite = true;
        float spread = 0.0f;
        for (int ch = 0; ch < 4; ++ch)
            for (int sample = 0; sample < blockSize; ++sample)
            {
                finite = finite && std::isfinite (buffer.getSample (ch, sample));
                spread = juce::jmax (spread, std::abs (buffer.getSample (0, sample)
                                                       - buffer.getSample (ch, sample)));
            }
        check (finite && juce::Decibels::gainToDecibels (spread, -200.0f) < -60.0f,
               "con quattro canali identici tutti escono uguali (nessun canale non processato)",
               "scarto fra canali " + juce::String (juce::Decibels::gainToDecibels (spread, -200.0f), 1) + " dBFS");
    }

    // ---- stato: il prompt fa parte del progetto
    {
        processor.applyPrompt ("r&b intimo, voce calda e morbida", vf::RulesEngine::defaultProfileId());
        const auto beforeThresh = processor.apvts.getRawParameterValue ("c1Thresh")->load();

        juce::MemoryBlock state;
        processor.getStateInformation (state);

        processor.applyPrompt ("metal, voce urlata", vf::RulesEngine::defaultProfileId());
        processor.setStateInformation (state.getData(), static_cast<int> (state.getSize()));

        check (processor.getPrompt() == "r&b intimo, voce calda e morbida", "il prompt torna dallo stato salvato");
        check (std::abs (processor.apvts.getRawParameterValue ("c1Thresh")->load() - beforeThresh) < 1.0e-4f,
               "i parametri tornano dallo stato salvato");
    }

    // ---- latenza dichiarata
    check (processor.getLatencySamples() == 0,
           "latenza dichiarata pari a zero, come il DSP (nessuno stadio ha lookahead)",
           juce::String (processor.getLatencySamples()) + " campioni");

    std::cout << std::endl << (failures == 0 ? "OK   tutti i controlli passati" : "FALLITO")
              << "   (" << failures << " problemi)" << std::endl;
    return failures == 0 ? 0 : 1;
}
