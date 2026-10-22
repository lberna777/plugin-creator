#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <random>

/*  Una take di voce sintetica per misurare il LOUDNESS (non i singoli stadi).

    Frasi da 1,8 s separate da 0,6 s di pausa, ogni frase a un livello diverso (±3 dB, come
    chi si avvicina e si allontana dal microfono), sillabe a 4 Hz, una 's' all'inizio di ogni
    sillaba e il rumore di stanza sempre presente. La take si normalizza al picco chiesto:
    è il dato che l'utente vede sul meter della Scarlett.
*/
namespace vftest
{

inline void fillTake (juce::AudioBuffer<float>& buffer, double sampleRate, float peakDbfs, unsigned seed = 97)
{
    std::mt19937 generator (seed);
    std::uniform_real_distribution<float> noise (-1.0f, 1.0f);
    const double phraseLevelsDb[] { 0.0, -3.0, 2.0, -1.5, 3.0, -2.5 };
    constexpr double phrase = 1.8, pause = 0.6, twoPi = juce::MathConstants<double>::twoPi;

    for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
    {
        const auto t = static_cast<double> (sample) / sampleRate;
        const auto cycle = static_cast<int> (t / (phrase + pause));
        const auto inCycle = t - cycle * (phrase + pause);

        double value = 0.002 * noise (generator);                       // stanza: ~-54 dB sotto la voce
        if (inCycle < phrase)
        {
            const auto level = std::pow (10.0, phraseLevelsDb[cycle % 6] / 20.0);
            const auto edge  = juce::jmin (1.0, juce::jmin (inCycle, phrase - inCycle) / 0.03);
            const auto syllable = 0.35 + 0.65 * std::pow (std::abs (std::sin (juce::MathConstants<double>::pi * 4.0 * inCycle)), 0.7);
            const auto f0 = 165.0 + 20.0 * std::sin (twoPi * 0.7 * t);    // un po' di melodia

            auto voice = 0.55 * std::sin (twoPi * f0 * t) + 0.3 * std::sin (twoPi * 2.0 * f0 * t)
                       + 0.18 * std::sin (twoPi * 3.0 * f0 * t) + 0.08 * std::sin (twoPi * 5.0 * f0 * t)
                       + 0.05 * std::sin (twoPi * 2900.0 * t);
            const auto inSyllable = std::fmod (inCycle, 0.25);
            if (inSyllable < 0.05)                                          // la 's' che apre la sillaba
                voice += 0.35 * (0.6 * std::sin (twoPi * 6800.0 * t) + 0.4 * noise (generator));

            value += voice * syllable * edge * level;
        }

        for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
            buffer.setSample (ch, sample, static_cast<float> (value));
    }

    const auto current = buffer.getMagnitude (0, buffer.getNumSamples());
    if (current > 0.0f)
        buffer.applyGain (juce::Decibels::decibelsToGain (peakDbfs) / current);
}

} // namespace vftest
