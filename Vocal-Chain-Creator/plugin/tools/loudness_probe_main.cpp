/*  vocalforge_loudness_probe — quanto alza la catena, misurato.

    Per ogni preset trap e per tre livelli d'ingresso (-24 / -12 / -6 dBFS di picco) fa passare
    12 s di voce di prova (TestVoice.h) nel processore vero e stampa:
      - loudness integrata (BS.1770) d'ingresso = catena SPENTA, e d'uscita = catena ACCESA;
      - la differenza in LU: è quello che l'orecchio scambia per "suona meglio";
      - il lavoro massimo del limiter e il picco d'uscita.

    Non è un test (non fallisce): è lo strumento con cui si leggono i numeri prima/dopo.
    I controlli che falliscono stanno in selftest_main.cpp.
*/
#include "../source/PluginProcessor.h"
#include "LoudnessMeter.h"
#include "TestVoice.h"
#include <cstdio>

int main()
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    constexpr double sampleRate = 48000.0;
    constexpr int blockSize = 512, seconds = 12;
    const char* const prompts[] { "voce trap aggressiva ma non stridula", "sfera ebbasta", "shiva",
                                  "tony boy", "glockyy", "capo plaza" };

    std::printf ("%-38s %8s %9s %9s %8s %9s %9s\n", "preset", "in dBFS", "in LUFS", "out LUFS",
                 "diff LU", "lim max", "out pk");

    for (auto* prompt : prompts)
        for (float peak : { -24.0f, -12.0f, -6.0f })
        {
            VocalForgeProcessor processor;
            processor.setRateAndBufferSizeDetails (sampleRate, blockSize);
            processor.prepareToPlay (sampleRate, blockSize);
            processor.applyPrompt (prompt, vf::RulesEngine::defaultProfileId());
            processor.prepareToPlay (sampleRate, blockSize);

            const int total = static_cast<int> (sampleRate) * seconds;
            juce::AudioBuffer<float> input (2, total), output (2, total);
            vftest::fillTake (input, sampleRate, peak);
            output.makeCopyOf (input);

            juce::MidiBuffer midi;
            float limiterMax = 0.0f;
            for (int start = 0; start < total; start += blockSize)
            {
                const auto count = juce::jmin (blockSize, total - start);
                float* pointers[2] { output.getWritePointer (0) + start, output.getWritePointer (1) + start };
                juce::AudioBuffer<float> view (pointers, 2, count);
                processor.processBlock (view, midi);
                limiterMax = juce::jmax (limiterMax, processor.getMeters().limGr.load());
            }

            const auto inLufs  = vftest::integratedLufs (input.getArrayOfReadPointers(), 2, total, sampleRate);
            const auto outLufs = vftest::integratedLufs (output.getArrayOfReadPointers(), 2, total, sampleRate);
            const auto outPeak = juce::Decibels::gainToDecibels (output.getMagnitude (0, total));
            std::printf ("%-38s %8.1f %9.2f %9.2f %+8.2f %9.2f %9.2f\n", prompt, peak, inLufs, outLufs,
                         outLufs - inLufs, limiterMax, outPeak);
        }
    return 0;
}
