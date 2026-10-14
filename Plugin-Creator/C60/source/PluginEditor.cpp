#include "PluginEditor.h"

namespace
{
constexpr int topH = 132, pad = 8;

juce::Colour passColour (uint16_t pass)
{
    if (pass == 0) return juce::Colour (0xff1c2226);
    return juce::Colour::fromHSV (std::fmod (pass * 0.618034f, 1.0f), 0.55f, 0.85f, 1.0f);
}
}

C60AudioProcessorEditor::C60AudioProcessorEditor (C60AudioProcessor& p) : AudioProcessorEditor (p), proc (p)
{
    for (auto* b : { &punchIn, &punchOut, &undo, &memo, &cal, &erase, &exportWav })
        addAndMakeVisible (b);

    punchIn.onClick  = [this] { proc.setPunchInHere(); };
    punchOut.onClick = [this] { proc.setPunchOutHere(); };
    memo.onClick     = [this] { proc.setMemoHere(); };
    undo.onClick     = [this] { proc.undoLastPass(); };
    cal.onClick      = [this] { calAnim = 60; };   // ~2 s di "misura", poi calibra
    erase.onClick    = [this]
    {
        juce::AlertWindow::showOkCancelBox (juce::MessageBoxIconType::WarningIcon, "Cancellare il nastro?",
                                            "Tutte le registrazioni andranno perse.", "Cancella", "Annulla", this,
                                            juce::ModalCallbackFunction::create ([this] (int r) { if (r == 1) proc.eraseTape(); }));
    };
    exportWav.onClick = [this]
    {
        chooser = std::make_unique<juce::FileChooser> ("Esporta il nastro", juce::File(), "*.wav");
        chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles,
                              [this] (const juce::FileChooser& fc)
                              {
                                  const auto f = fc.getResult();
                                  if (f == juce::File()) return;
                                  juce::String err;
                                  if (! proc.exportTape (f.withFileExtension ("wav"), err))
                                      juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Esportazione", err);
                              });
    };

    status.setFont (juce::FontOptions (15.0f));
    status.setColour (juce::Label::textColourId, juce::Colour (0xff7fe8e0));
    addAndMakeVisible (status);
    addAndMakeVisible (timeline);
    addAndMakeVisible (params);

    setResizable (true, false);
    setSize (720, topH + 640);
    startTimerHz (30);
}

juce::String C60AudioProcessorEditor::formatTime (int64_t samples, double sr) const
{
    const bool neg = samples < 0;
    const double s = std::abs ((double) samples) / juce::jmax (1.0, sr);
    return juce::String (neg ? "-" : "") + juce::String ((int) s / 60) + ":" + juce::String ((int) s % 60).paddedLeft ('0', 2)
           + "." + juce::String ((int) (s * 10) % 10);
}

void C60AudioProcessorEditor::timerCallback()
{
    if (calAnim > 0 && --calAnim == 0) proc.calibrate();

    const auto d = proc.getDeckState();
    juce::String s;
    s << "COUNTER " << formatTime (d.position - d.memo, d.sampleRate)
      << "   TAPE " << formatTime (d.position, d.sampleRate) << " / " << formatTime (d.length, d.sampleRate)
      << "   IN " << formatTime (d.in, d.sampleRate) << "  OUT " << formatTime (d.out, d.sampleRate)
      << (d.recording ? "   ● REC" : "") << (d.playing ? "   ▶" : "   ■")
      << "   CAL " << (calAnim > 0 ? ((calAnim / 5) % 2 ? "400 Hz" : "10 kHz") : (d.calibrated ? "OK" : "--"))
      << "   UNDO " << (d.canUndo ? "sì" : "no");
    status.setText (s, juce::dontSendNotification);
    undo.setEnabled (d.canUndo);
    timeline.repaint();
}

void C60AudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff111416));
}

void C60AudioProcessorEditor::resized()
{
    auto r = getLocalBounds();
    auto top = r.removeFromTop (topH).reduced (pad);
    auto row = top.removeFromTop (30);
    const int w = row.getWidth() / 7;
    for (auto* b : { &punchIn, &punchOut, &undo, &memo, &cal, &erase, &exportWav })
        b->setBounds (row.removeFromLeft (w).reduced (2, 0));
    top.removeFromTop (pad);
    status.setBounds (top.removeFromTop (24));
    top.removeFromTop (pad);
    timeline.setBounds (top);
    params.setBounds (r);
}

void C60AudioProcessorEditor::Timeline::paint (juce::Graphics& g)
{
    const auto& tape = proc.getTape();
    const auto d = proc.getDeckState();
    auto b = getLocalBounds().toFloat();
    g.setColour (juce::Colour (0xff1c2226));
    g.fillRect (b);

    const int cells = tape.cells();
    if (cells <= 0 || d.length <= 0) return;
    const float cw = b.getWidth() / (float) cells;
    for (int i = 0; i < cells;)
    {
        const uint16_t p = tape.passAt (i);
        int j = i + 1;
        while (j < cells && tape.passAt (j) == p) ++j;
        if (p != 0)
        {
            g.setColour (passColour (p));
            g.fillRect (b.getX() + i * cw, b.getY() + 4, (j - i) * cw, b.getHeight() - 8);
        }
        i = j;
    }

    auto xOf = [&] (int64_t s) { return b.getX() + b.getWidth() * (float) juce::jlimit (0.0, 1.0, (double) s / (double) d.length); };
    g.setColour (juce::Colours::orange);
    g.drawVerticalLine ((int) xOf (d.in), b.getY(), b.getBottom());
    g.setColour (juce::Colours::orangered);
    g.drawVerticalLine ((int) xOf (d.out), b.getY(), b.getBottom());
    g.setColour (d.recording ? juce::Colours::red : juce::Colours::white);
    g.fillRect (xOf (d.position) - 1.0f, b.getY(), 2.0f, b.getHeight());
}
