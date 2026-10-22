// probe — banco di prova offline del motore.
// Rende ogni scena in WAV passando dal VERO processBlock (block size e sample rate variabili,
// mono e stereo), e stampa picco/RMS/DC. Serve per ascoltare e per misurare, non per indovinare.
//
//   ./probe <cartella_out>

#include <JuceHeader.h>
#include "PluginProcessor.h"

using namespace juce;

namespace
{
    struct Stats { float peak = 0.0f, rms = 0.0f, dc = 0.0f; };

    Stats analyse (const AudioBuffer<float>& b)
    {
        Stats s;
        double sum = 0.0, sumSq = 0.0;
        int n = 0;
        for (int ch = 0; ch < b.getNumChannels(); ++ch)
            for (int i = 0; i < b.getNumSamples(); ++i)
            {
                const float v = b.getSample (ch, i);
                s.peak = jmax (s.peak, std::abs (v));
                sum += v; sumSq += (double) v * v; ++n;
            }
        s.rms = (float) std::sqrt (sumSq / jmax (1, n));
        s.dc  = (float) (sum / jmax (1, n));
        return s;
    }

    void setParam (AtmosForgeAudioProcessor& p, const char* id, float plainValue)
    {
        if (auto* param = p.apvts.getParameter (id))
            param->setValueNotifyingHost (param->convertTo0to1 (plainValue));
    }
}

int main (int argc, char** argv)
{
    ScopedJuceInitialiser_GUI init;

    const File outDir = argc > 1 ? File (String (argv[1])) : File::getCurrentWorkingDirectory();
    outDir.createDirectory();

    const double rates[] = { 44100.0, 48000.0, 96000.0 };
    const int blockSizes[] = { 16, 64, 512, 2048 };

    int failures = 0;

    for (int scene = 0; scene < 4; ++scene)
    {
        AtmosForgeAudioProcessor proc;

        setParam (proc, ParamID::scene, (float) scene);
        setParam (proc, ParamID::density, 0.6f);
        setParam (proc, ParamID::motion, 0.5f);
        setParam (proc, ParamID::tone, scene == 2 ? 0.8f : 0.55f);
        setParam (proc, ParamID::depth, 0.3f);
        setParam (proc, ParamID::width, 1.2f);
        setParam (proc, ParamID::level, -6.0f);
        setParam (proc, ParamID::seed, 1234.0f);

        const double sr = 48000.0;
        const int block = 512;
        proc.setPlayConfigDetails (0, 2, sr, block);
        proc.prepareToPlay (sr, block);

        const int seconds = 8;
        const int total = (int) (sr * seconds);
        AudioBuffer<float> out (2, total);
        out.clear();

        AudioBuffer<float> tmp (2, block);
        MidiBuffer midi;
        for (int pos = 0; pos < total; pos += block)
        {
            const int n = jmin (block, total - pos);
            tmp.setSize (2, n, false, false, true);
            tmp.clear();
            midi.clear();
            proc.processBlock (tmp, midi);
            out.copyFrom (0, pos, tmp, 0, 0, n);
            out.copyFrom (1, pos, tmp, 1, 0, n);
        }

        const auto st = analyse (out);
        const auto name = sceneNames()[scene].replace (" / ", "-").replace (" ", "");

        auto file = outDir.getChildFile ("probe_" + String (scene) + "_" + name + ".wav");
        file.deleteFile();
        if (auto* stream = file.createOutputStream().release())
        {
            WavAudioFormat wav;
            std::unique_ptr<AudioFormatWriter> w (wav.createWriterFor (stream, sr, 2, 24, {}, 0));
            if (w != nullptr) w->writeFromAudioSampleBuffer (out, 0, total);
            else delete stream;
        }

        std::cout << "scena " << scene << " " << name.toStdString()
                  << "  peak " << String (Decibels::gainToDecibels (st.peak), 1).toStdString() << " dB"
                  << "  rms " << String (Decibels::gainToDecibels (st.rms), 1).toStdString() << " dB"
                  << "  dc " << String (st.dc, 6).toStdString()
                  << "  -> " << file.getFileName().toStdString() << "\n";

        if (st.peak <= 0.0001f) { std::cout << "   !! silenzio\n"; ++failures; }
        if (st.peak > 1.0001f)  { std::cout << "   !! clipping oltre 0 dBFS\n"; ++failures; }
        if (std::abs (st.dc) > 0.01f) { std::cout << "   !! DC offset\n"; ++failures; }
    }

    // --- robustezza: sample rate e block size diversi, mono, e note MIDI
    std::cout << "\nrate/block/mono:\n";
    for (double sr : rates)
        for (int block : blockSizes)
            for (int channels : { 1, 2 })
            {
                AtmosForgeAudioProcessor proc;
                setParam (proc, ParamID::scene, 3.0f);      // passi: tanti eventi brevi
                proc.setPlayConfigDetails (0, channels, sr, block);
                proc.prepareToPlay (sr, block);

                AudioBuffer<float> b (channels, block);
                MidiBuffer midi;
                float peak = 0.0f;
                bool bad = false;

                for (int k = 0; k < (int) (sr * 2 / block); ++k)
                {
                    b.clear();
                    midi.clear();
                    if (k % 20 == 0) midi.addEvent (MidiMessage::noteOn (1, 48 + (k % 24), 0.9f), 0);
                    proc.processBlock (b, midi);
                    for (int ch = 0; ch < channels; ++ch)
                        for (int i = 0; i < block; ++i)
                        {
                            const float v = b.getSample (ch, i);
                            if (! std::isfinite (v)) bad = true;
                            peak = jmax (peak, std::abs (v));
                        }
                }

                const bool silent = peak <= 0.0001f;
                if (bad || silent || peak > 1.0001f)
                {
                    std::cout << "  !! sr " << sr << " block " << block << " ch " << channels
                              << (bad ? " NaN/Inf" : silent ? " silenzio" : " clip") << "\n";
                    ++failures;
                }
            }

    if (failures == 0) std::cout << "  tutte le combinazioni ok\n";

    // --- bypass: deve tacere, non lasciare spazzatura
    {
        AtmosForgeAudioProcessor proc;
        proc.setPlayConfigDetails (0, 2, 48000.0, 256);
        proc.prepareToPlay (48000.0, 256);
        AudioBuffer<float> b (2, 256);
        MidiBuffer midi;
        b.clear();
        proc.processBlockBypassed (b, midi);
        const auto st = analyse (b);
        std::cout << "\nbypass peak " << st.peak << (st.peak == 0.0f ? "  ok\n" : "  !! non silenzioso\n");
        if (st.peak != 0.0f) ++failures;
    }

    std::cout << (failures == 0 ? "\nPROBE OK\n" : "\nPROBE: " + String (failures).toStdString() + " problemi\n");
    return failures == 0 ? 0 : 1;
}
