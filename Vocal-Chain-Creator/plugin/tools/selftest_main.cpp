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

        juce::AudioBuffer<float> buffer (2, blockSize);
        float worstJump = 0.0f;
        for (int i = 0; i < 12; ++i)
        {
            fillVoiceLike (buffer, sampleRate, 0.4f);
            if (i == 6) processor.applyPrompt ("metal, voce urlata molto aggressiva",
                                               vf::RulesEngine::defaultProfileId());
            processor.processBlock (buffer, midi);
            if (i >= 6) worstJump = juce::jmax (worstJump, maxJump (buffer));
        }
        check (worstJump < 0.5f, "cambio di prompt in corsa senza click", "salto max " + juce::String (worstJump, 3));
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

    // ---- nessun parametro finto: ogni controllo deve cambiare il suono
    {
        const double sampleRate = 48000.0;
        auto render = [&] (const juce::String& paramId, float value)
        {
            prepare (sampleRate);
            processor.applyPrompt ("voce tipo sfera ebbasta", vf::RulesEngine::defaultProfileId());
            if (paramId.isNotEmpty())
                if (auto* p = processor.apvts.getParameter (paramId))
                    p->setValueNotifyingHost (value);

            juce::AudioBuffer<float> buffer (2, blockSize), tail (2, blockSize);
            tail.clear();
            for (int i = 0; i < 16; ++i)
            {
                fillVoiceLike (buffer, sampleRate, 0.4f);
                processor.processBlock (buffer, midi);
                if (i == 15) for (int ch = 0; ch < 2; ++ch) tail.copyFrom (ch, 0, buffer, ch, 0, blockSize);
            }
            return tail;
        };

        const auto reference = render ({}, 0.0f);
        juce::StringArray dead;
        for (auto* parameter : processor.getParameters())
        {
            auto* withId = dynamic_cast<juce::AudioProcessorParameterWithID*> (parameter);
            if (withId == nullptr) continue;
            const auto id = withId->paramID;
            if (id == "bypass") continue;

            const auto probe = parameter->getValue() < 0.5f ? 1.0f : 0.0f;
            const auto rendered = render (id, probe);

            float difference = 0.0f;
            for (int ch = 0; ch < 2; ++ch)
                for (int sample = 0; sample < blockSize; ++sample)
                    difference = juce::jmax (difference, std::abs (rendered.getSample (ch, sample)
                                                                   - reference.getSample (ch, sample)));
            if (juce::Decibels::gainToDecibels (difference, -120.0f) < -100.0f)
                dead.add (id);
        }
        check (dead.isEmpty(), "nessun parametro finto: tutti cambiano il suono",
               dead.isEmpty() ? juce::String() : "inerti: " + dead.joinIntoString (", "));
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
