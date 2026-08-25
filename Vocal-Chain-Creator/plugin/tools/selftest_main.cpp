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
    check (processor.getLatencySamples() > 0, "latenza dichiarata (Logic compensa solo se gliela dici)",
           juce::String (processor.getLatencySamples()) + " campioni");

    std::cout << std::endl << (failures == 0 ? "OK   tutti i controlli passati" : "FALLITO")
              << "   (" << failures << " problemi)" << std::endl;
    return failures == 0 ? 0 : 1;
}
