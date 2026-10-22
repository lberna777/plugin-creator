// Render headless dell'editor CON DATI VERI: genera un riferimento, fa "suonare" un mix con
// difetti noti (low-mid in eccesso, bassi non mono, più schiacciato) e fotografa l'editor reale.
//   ./shoot_demo ui_demo.png
#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "analysis/KWeighting.h"
#include <random>

namespace
{
struct Pink
{
    double b0 = 0, b1 = 0, b2 = 0, b3 = 0, b4 = 0, b5 = 0, b6 = 0;
    float next (double w)
    {
        b0 = 0.99886 * b0 + w * 0.0555179;  b1 = 0.99332 * b1 + w * 0.0750759;
        b2 = 0.96900 * b2 + w * 0.1538520;  b3 = 0.86650 * b3 + w * 0.3104856;
        b4 = 0.55000 * b4 + w * 0.5329522;  b5 = -0.7616 * b5 - w * 0.0168980;
        const double p = b0 + b1 + b2 + b3 + b4 + b5 + b6 + w * 0.5362;
        b6 = w * 0.115926;
        return (float) (p * 0.11);
    }
};

mc::Biquad peaking (double sr, double f, double q, double gainDb)
{
    constexpr double pi = 3.14159265358979323846;
    mc::Biquad b;
    const double A = std::pow (10.0, gainDb / 40.0), w0 = 2 * pi * f / sr, alpha = std::sin (w0) / (2 * q), a0 = 1 + alpha / A;
    b.b0 = (1 + alpha * A) / a0; b.b1 = -2 * std::cos (w0) / a0; b.b2 = (1 - alpha * A) / a0;
    b.a1 = -2 * std::cos (w0) / a0; b.a2 = (1 - alpha / A) / a0;
    return b;
}
mc::Biquad lowpass (double sr, double f)
{
    constexpr double pi = 3.14159265358979323846;
    mc::Biquad b;
    const double w0 = 2 * pi * f / sr, alpha = std::sin (w0) / (2 * 0.707), a0 = 1 + alpha;
    b.b0 = (1 - std::cos (w0)) / 2 / a0; b.b1 = (1 - std::cos (w0)) / a0; b.b2 = b.b0;
    b.a1 = -2 * std::cos (w0) / a0; b.a2 = (1 - alpha) / a0;
    return b;
}
} // namespace

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI guiInit;
    const juce::String outPath = argc > 1 ? juce::String (argv[1]) : "ui_demo.png";
    const double sr = 48000.0;
    std::mt19937 rng (1);
    std::normal_distribution<double> nd (0.0, 1.0);

    // riferimento: rosa con un po' di sub in più, stereo moderato, 20 s
    const auto refFile = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("Riferimento trap (demo).wav");
    {
        refFile.deleteFile();
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::OutputStream> os (refFile.createOutputStream().release());
        std::unique_ptr<juce::AudioFormatWriter> w (wav.createWriterFor (os.get(), sr, 2, 24, {}, 0));
        os.release();
        Pink c, l, r;
        auto sub = peaking (sr, 50, 0.8, 5.0);
        juce::AudioBuffer<float> b (2, 4800);
        for (int k = 0; k < 200; ++k)
        {
            for (int i = 0; i < 4800; ++i)
            {
                const double m = sub.process (c.next (nd (rng))) * 0.9;
                b.setSample (0, i, (float) (m + 0.25 * l.next (nd (rng))));
                b.setSample (1, i, (float) (m + 0.25 * r.next (nd (rng))));
            }
            w->writeFromAudioSampleBuffer (b, 0, 4800);
        }
    }

    PLUGIN_PROCESSOR_CLASS proc;
    proc.setRateAndBufferSizeDetails (sr, 512);
    proc.prepareToPlay (sr, 512);
    proc.loadReference (refFile);
    for (int t = 0; t < 3000 && proc.getReferenceStatus().state != MixCompassProcessor::RefState::ready; ++t)
        juce::Thread::sleep (10);

    // mix: low-mid +5 dB, bassi decorrelati, livello più basso, alti un po' più stretti
    {
        Pink c, l, r, bl, br;
        auto lowMid = peaking (sr, 250, 0.8, 5.0);
        auto lpL = lowpass (sr, 90), lpR = lowpass (sr, 90);
        juce::AudioBuffer<float> b (2, 512);
        juce::MidiBuffer midi;
        for (int k = 0; k < (int) (sr * 15 / 512); ++k)
        {
            for (int i = 0; i < 512; ++i)
            {
                const double m = lowMid.process (c.next (nd (rng))) * 0.4;
                b.setSample (0, i, (float) (m + 0.05 * l.next (nd (rng)) + 0.35 * lpL.process (bl.next (nd (rng)))));
                b.setSample (1, i, (float) (m + 0.05 * r.next (nd (rng)) + 0.35 * lpR.process (br.next (nd (rng)))));
            }
            proc.processBlock (b, midi);
            if (k % 40 == 0) juce::Thread::sleep (5);
        }
        juce::Thread::sleep (400);   // ultimo snapshot del thread di analisi
    }

    std::unique_ptr<juce::AudioProcessorEditor> ed (proc.createEditor());
    const int w = ed->getWidth(), h = ed->getHeight();
    juce::Image img (juce::Image::ARGB, w * 2, h * 2, true);   // @2x
    {
        juce::Graphics g (img);
        g.addTransform (juce::AffineTransform::scale (2.0f));
        ed->paintEntireComponent (g, true);
    }
    juce::File outFile (juce::File::getCurrentWorkingDirectory().getChildFile (outPath));
    outFile.deleteFile();
    juce::FileOutputStream os (outFile);
    juce::PNGImageFormat().writeImageToStream (img, os);
    std::cout << "render salvato: " << outFile.getFullPathName() << " (" << w << "x" << h << " @2x)\n";

    ed.reset();
    proc.releaseResources();
    refFile.deleteFile();
    return 0;
}
