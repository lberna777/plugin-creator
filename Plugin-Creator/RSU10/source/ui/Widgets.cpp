#include "Widgets.h"

using namespace rsu;

namespace
{
bool isBipolar (PP p) { return p == PP::Pan || p == PP::Tune || p == PP::Fine; }

juce::String filesDescription (const juce::Array<juce::File>& files)
{
    juce::StringArray paths;
    for (auto& f : files)
        paths.add (f.getFullPathName());
    return "files:" + paths.joinIntoString ("\n");
}

juce::Array<juce::File> filesFromDescription (const juce::String& d)
{
    juce::Array<juce::File> out;
    for (auto& p : juce::StringArray::fromLines (d.fromFirstOccurrenceOf ("files:", false, false)))
        if (p.isNotEmpty())
            out.add (juce::File (p));
    return out;
}

juce::Array<juce::File> audioFilesIn (const juce::File& folder)
{
    auto files = folder.findChildFiles (juce::File::findFiles, false);
    files.removeIf ([] (const juce::File& f) { return ! Sample::isAudioFile (f); });
    files.sort();
    return files;
}
} // namespace

// ================================================================ Knob

juce::Rectangle<float> Knob::knobArea() const
{
    const float side = juce::jmin (getWidth(), getHeight()) * 0.72f;
    return juce::Rectangle<float> (side, side).withCentre (getLocalBounds().toFloat().getCentre());
}

float Knob::value01() const
{
    if (pp < 0)
        return 0.0f;
    return rangeOf (PP (pp)).convertTo0to1 (proc.pads.get (proc.selectedPad, PP (pp)));
}

void Knob::setValue01 (float v)
{
    if (pp < 0)
        return;
    proc.pads.set (proc.selectedPad, PP (pp), rangeOf (PP (pp)).convertFrom0to1 (juce::jlimit (0.0f, 1.0f, v)));
    repaint();
}

void Knob::paint (juce::Graphics& g)
{
    const auto area = knobArea();
    const float v = value01();
    if (pp >= 0)
        skin::drawKnobArc (g, area, 0.0f, v, isBipolar (PP (pp)));
    else
        skin::drawKnobArc (g, area, 0.0f, 0.0f, false);
    skin::drawKnob (g, area, -juce::MathConstants<float>::pi * 0.75f + v * juce::MathConstants<float>::pi * 1.5f);
    if (pp < 0)
    {
        g.setColour (skin::col::alu.withAlpha (0.55f));
        g.fillEllipse (area.expanded (2.0f));
    }
}

void Knob::mouseDown (const juce::MouseEvent&)
{
    dragStart01 = value01();
}

void Knob::mouseDrag (const juce::MouseEvent& e)
{
    if (pp < 0)
        return;
    const float sensitivity = e.mods.isShiftDown() ? 1000.0f : 200.0f;
    float v = dragStart01 - float (e.getDistanceFromDragStartY() - e.getDistanceFromDragStartX() * 0.3f) / sensitivity;
    if (isChoice (PP (pp)))
    {
        // scelte: scatti netti
        const auto r = rangeOf (PP (pp));
        v = r.convertTo0to1 (r.snapToLegalValue (r.convertFrom0to1 (juce::jlimit (0.0f, 1.0f, v))));
    }
    setValue01 (v);
}

void Knob::mouseUp (const juce::MouseEvent&) {}

void Knob::mouseDoubleClick (const juce::MouseEvent&)
{
    if (pp >= 0)
    {
        proc.pads.set (proc.selectedPad, PP (pp), def (PP (pp)).def);
        repaint();
    }
}

void Knob::mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& w)
{
    if (pp < 0)
        return;
    if (isChoice (PP (pp)) || isInteger (PP (pp)))
    {
        const float step = (w.deltaY > 0 ? 1.0f : -1.0f) * (w.isReversed ? -1.0f : 1.0f);
        proc.pads.set (proc.selectedPad, PP (pp), proc.pads.get (proc.selectedPad, PP (pp)) + step);
        repaint();
        return;
    }
    setValue01 (value01() + w.deltaY * 0.15f);
}

// ================================================================ PadView

juce::Rectangle<float> PadView::padArea() const
{
    const float side = juce::jmin (getWidth(), getHeight()) * 0.84f;
    return juce::Rectangle<float> (side, side).withCentre (getLocalBounds().toFloat().getCentre());
}

void PadView::paint (juce::Graphics& g)
{
    const int pad = padIndex();
    const auto area = padArea();
    const bool selected = proc.selectedPad == pad;
    const bool scaleOn = proc.apvts.getRawParameterValue (ParamID::scale)->load() > 0.5f;
    const bool hasSample = proc.samples.get (pad, 0) != nullptr;
    const bool playing = proc.engine.padVoices[pad].load() > 0;

    // Anello LED: colpo (bianco caldo), selezionato (ambra), pad della scala (ciano), trascinamento (ciano)
    if (hover)
        skin::drawPadRing (g, area, skin::col::cyan, 1.0f);
    else if (glow > 0.02f)
        skin::drawPadRing (g, area, skin::col::warmWhite, glow);
    else if (scaleOn && proc.scalePad == pad)
        skin::drawPadRing (g, area, skin::col::cyan, 0.7f);
    else if (selected)
        skin::drawPadRing (g, area, skin::col::amber, 0.55f);
    else if (playing)
        skin::drawPadRing (g, area, skin::col::amber, 0.25f);
    else if (hasSample)
        skin::drawPadRing (g, area, skin::col::amber, 0.06f);

    skin::drawPad (g, area, pressed);

    // Serigrafia sul pad come l'SU10: nome in alto a sinistra, numero in basso a destra
    const auto inner = area.reduced (area.getWidth() * 0.1f);
    juce::String name = hasSample ? proc.samples.get (pad, 0)->name().toUpperCase() : juce::String ("—");
    const int layers = proc.samples.layerCount (pad);
    if (layers > 1)
        name << "  x" << layers;
    skin::drawSilk (g, name, inner.withHeight (inner.getHeight() * 0.42f), juce::Justification::topLeft,
                    juce::jmax (8.0f, area.getHeight() * 0.11f), hasSample ? skin::col::silk : skin::col::dim);
    skin::drawSilk (g, juce::String (pad % kPadsPerBank + 1), inner.withTrimmedTop (inner.getHeight() * 0.5f),
                    juce::Justification::bottomRight, area.getHeight() * 0.26f, skin::col::silk);
}

void PadView::mouseDown (const juce::MouseEvent& e)
{
    const int pad = padIndex();
    if (e.mods.isPopupMenu())
    {
        proc.selectedPad = pad;
        showMenu();
        return;
    }
    proc.selectedPad = pad;
    dragging = false;
    if (! (ui.shift || e.mods.isShiftDown()))
    {
        // velocity dal punto del clic: in alto forte, in basso piano
        const auto area = padArea();
        const float y = juce::jlimit (0.0f, 1.0f, (float (e.y) - area.getY()) / area.getHeight());
        const int vel = juce::jlimit (1, 127, juce::roundToInt (127.0f - y * 90.0f));
        proc.engine.triggerFromUi (pad, vel, true);
        pressed = true;
    }
    if (auto* p = getParentComponent())
        p->repaint();
}

void PadView::mouseDrag (const juce::MouseEvent& e)
{
    if (! dragging && e.getDistanceFromDragStart() > 8)
        if (auto* dnd = juce::DragAndDropContainer::findParentDragContainerFor (this))
        {
            dragging = true;
            dnd->startDragging ("pad:" + juce::String (padIndex()), this);
        }
}

void PadView::mouseUp (const juce::MouseEvent&)
{
    if (pressed)
        proc.engine.triggerFromUi (padIndex(), 0, false);
    pressed = false;
    repaint();
}

bool PadView::isInterestedInDragSource (const SourceDetails& d)
{
    const auto s = d.description.toString();
    return s.startsWith ("files:") || s.startsWith ("pad:");
}

void PadView::itemDropped (const SourceDetails& d)
{
    hover = false;
    const auto s = d.description.toString();
    if (s.startsWith ("pad:"))
    {
        const int from = s.fromFirstOccurrenceOf ("pad:", false, false).getIntValue();
        proc.copyPad (from, padIndex());
        proc.postMessage (padLabel (from) + " copiato su " + padLabel (padIndex()));
    }
    else
    {
        dropFiles (filesFromDescription (s), ui.shift || juce::ModifierKeys::currentModifiers.isShiftDown());
    }
    proc.selectedPad = padIndex();
    repaint();
}

void PadView::filesDropped (const juce::StringArray& files, int, int)
{
    hover = false;
    juce::Array<juce::File> list;
    for (auto& f : files)
        list.add (juce::File (f));
    dropFiles (list, ui.shift || juce::ModifierKeys::currentModifiers.isShiftDown());
    proc.selectedPad = padIndex();
    repaint();
}

void PadView::dropFiles (const juce::Array<juce::File>& files, bool asLayer)
{
    juce::Array<juce::File> audio;
    for (auto& f : files)
    {
        if (f.isDirectory())
            audio.addArray (audioFilesIn (f));   // una cartella = un kit sui pad successivi
        else if (Sample::isAudioFile (f))
            audio.add (f);
    }
    if (audio.isEmpty())
        return;

    const int pad = padIndex();
    if (asLayer)
    {
        // SHIFT: strati di velocity sullo stesso pad
        int layer = proc.samples.layerCount (pad);
        for (auto& f : audio)
            if (layer < kMaxLayers)
                proc.loadSample (pad, layer++, f);
        return;
    }
    proc.loadSamplesFrom (pad, audio);
}

void PadView::showMenu()
{
    const int pad = padIndex();
    juce::PopupMenu m;
    m.addSectionHeader ("PAD " + padLabel (pad));
    m.addItem (1, "Carica campione...");
    m.addItem (2, "Aggiungi strato di velocity...", proc.samples.layerCount (pad) < kMaxLayers);
    m.addItem (3, "Svuota pad", proc.samples.layerCount (pad) > 0);
    m.addItem (4, "Ripristina impostazioni del pad");
    m.addSeparator();
    juce::PopupMenu copyTo;
    for (int p = 0; p < kNumPads; ++p)
        if (p != pad)
            copyTo.addItem (100 + p, padLabel (p));
    m.addSubMenu ("Copia su", copyTo);
    if (auto* s = proc.samples.get (pad, 0); s != nullptr && s->file().existsAsFile())
        m.addItem (5, "Mostra nel Finder");

    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this), [this, pad] (int r)
    {
        if (r == 1 || r == 2)
        {
            const int layer = r == 1 ? 0 : proc.samples.layerCount (pad);
            auto chooser = std::make_shared<juce::FileChooser> ("Campione per " + padLabel (pad), proc.browserFolder,
                                                                "*.wav;*.aif;*.aiff;*.flac;*.mp3;*.ogg;*.m4a;*.caf");
            chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                                  [this, chooser, pad, layer] (const juce::FileChooser& fc)
                                  {
                                      if (fc.getResult().existsAsFile())
                                          proc.loadSample (pad, layer, fc.getResult());
                                  });
        }
        else if (r == 3) proc.samples.clearPad (pad);
        else if (r == 4) proc.pads.resetPad (pad);
        else if (r == 5) proc.samples.get (pad, 0)->file().revealToUser();
        else if (r >= 100) proc.copyPad (pad, r - 100);
        repaint();
    });
}

// ================================================================ Ribbon

namespace { const char* kRibbonModes[] { "SCRATCH", "PITCH", "FILTER", "REV", "X-FADE" }; }

juce::Rectangle<float> Ribbon::chipArea (int i) const
{
    auto top = getLocalBounds().toFloat().reduced (8.0f, 6.0f).removeFromTop (getHeight() * 0.28f);
    const float w = top.getWidth() / 6.0f;
    return { top.getX() + w * float (i), top.getY(), w, top.getHeight() };
}

juce::Rectangle<float> Ribbon::stripArea() const
{
    auto r = getLocalBounds().toFloat().reduced (8.0f, 6.0f);
    r.removeFromTop (getHeight() * 0.32f);
    return r;
}

void Ribbon::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    // vetro nero lucido (originale)
    g.setColour (skin::col::bezel);
    g.fillRoundedRectangle (r, 4.0f);
    g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (0.10f), r.getX(), r.getY(),
                                             juce::Colours::transparentWhite, r.getX(), r.getY() + r.getHeight() * 0.5f, false));
    g.fillRoundedRectangle (r.reduced (2.0f), 3.0f);

    const int mode = juce::roundToInt (proc.apvts.getRawParameterValue (ParamID::ribbonMode)->load());
    const bool hold = proc.apvts.getRawParameterValue (ParamID::ribbonHold)->load() > 0.5f;
    for (int i = 0; i < 5; ++i)
    {
        const auto c = chipArea (i);
        g.setColour (i == mode ? skin::col::cyan : skin::col::dim.withAlpha (0.7f));
        g.setFont (skin::screenFont (c.getHeight() * 0.62f, i == mode));
        g.drawText (kRibbonModes[i], c, juce::Justification::centred);
    }
    const auto hc = chipArea (5);
    g.setColour (hold ? skin::col::amber : skin::col::dim.withAlpha (0.5f));
    g.setFont (skin::screenFont (hc.getHeight() * 0.62f, hold));
    g.drawText ("HOLD", hc, juce::Justification::centred);

    // luce LED sotto il vetro (moderna) che segue il dito
    const auto strip = stripArea();
    g.setColour (juce::Colours::white.withAlpha (0.05f));
    g.drawRoundedRectangle (strip, 3.0f, 1.0f);
    const float v = proc.engine.ribbonShown.load();
    const bool on = proc.engine.ribbonTouchedShown.load() || touching;
    const float x = strip.getX() + v * strip.getWidth();
    if (on)
    {
        juce::ColourGradient glow (skin::col::cyan.withAlpha (0.85f), x, strip.getCentreY(),
                                   skin::col::cyan.withAlpha (0.0f), x + strip.getWidth() * 0.18f, strip.getCentreY(), true);
        g.setGradientFill (glow);
        g.fillRect (strip);
        g.setColour (juce::Colours::white);
        g.fillRect (juce::Rectangle<float> (2.0f, strip.getHeight() * 0.8f).withCentre ({ x, strip.getCentreY() }));
    }
    else
    {
        g.setColour (skin::col::cyan.withAlpha (0.18f));
        g.fillRect (juce::Rectangle<float> (2.0f, strip.getHeight() * 0.5f).withCentre ({ x, strip.getCentreY() }));
    }
}

void Ribbon::setFromX (float x)
{
    const auto strip = stripArea();
    if (auto* p = proc.apvts.getParameter (ParamID::ribbon))
        p->setValueNotifyingHost (juce::jlimit (0.0f, 1.0f, (x - strip.getX()) / strip.getWidth()));
}

void Ribbon::mouseDown (const juce::MouseEvent& e)
{
    for (int i = 0; i < 6; ++i)
        if (chipArea (i).contains (e.position))
        {
            if (i < 5)
            {
                auto* p = proc.apvts.getParameter (ParamID::ribbonMode);
                p->beginChangeGesture();
                p->setValueNotifyingHost (p->convertTo0to1 (float (i)));
                p->endChangeGesture();
            }
            else if (auto* h = proc.apvts.getParameter (ParamID::ribbonHold))
            {
                h->setValueNotifyingHost (h->getValue() > 0.5f ? 0.0f : 1.0f);
            }
            repaint();
            return;
        }

    touching = true;
    proc.apvts.getParameter (ParamID::ribbon)->beginChangeGesture();
    proc.apvts.getParameter (ParamID::ribbonTouch)->setValueNotifyingHost (1.0f);
    setFromX (e.position.x);
}

void Ribbon::mouseDrag (const juce::MouseEvent& e)
{
    if (touching)
        setFromX (e.position.x);
}

void Ribbon::mouseUp (const juce::MouseEvent&)
{
    if (! touching)
        return;
    touching = false;
    proc.apvts.getParameter (ParamID::ribbonTouch)->setValueNotifyingHost (0.0f);
    proc.apvts.getParameter (ParamID::ribbon)->endChangeGesture();
}

// ================================================================ BrowserScreen

BrowserScreen::BrowserScreen (RSU10AudioProcessor& p, UiState& u) : proc (p), ui (u)
{
    places = {
        { "MUSICA",    juce::File::getSpecialLocation (juce::File::userMusicDirectory) },
        { "SCRIVANIA", juce::File::getSpecialLocation (juce::File::userDesktopDirectory) },
        { "DOWNLOAD",  juce::File::getSpecialLocation (juce::File::userHomeDirectory).getChildFile ("Downloads") },
        { "RSU-10",    RSU10AudioProcessor::userFolder() },
    };

    list.setModel (this);
    list.setColour (juce::ListBox::backgroundColourId, juce::Colours::transparentBlack);
    list.setColour (juce::ListBox::outlineColourId, juce::Colours::transparentBlack);
    list.setRowHeight (20);
    list.getVerticalScrollBar().setColour (juce::ScrollBar::thumbColourId, skin::col::dim.withAlpha (0.6f));
    addAndMakeVisible (list);
    openFolder (proc.browserFolder);
}

juce::Rectangle<int> BrowserScreen::headerArea() const
{
    return getLocalBounds().reduced (6).removeFromTop (44);
}

juce::Rectangle<int> BrowserScreen::placeArea (int i) const
{
    auto row = headerArea().removeFromTop (20);
    const int w = row.getWidth() / (int) places.size();
    return { row.getX() + w * i, row.getY(), w, row.getHeight() };
}

void BrowserScreen::resized()
{
    auto r = getLocalBounds().reduced (6);
    r.removeFromTop (48);
    list.setBounds (r);
    list.setRowHeight (juce::jmax (16, getHeight() / 30));
}

void BrowserScreen::paint (juce::Graphics& g)
{
    for (int i = 0; i < (int) places.size(); ++i)
    {
        const auto a = placeArea (i).toFloat().reduced (2.0f, 1.0f);
        const bool here = proc.browserFolder == places[size_t (i)].folder || proc.browserFolder.isAChildOf (places[size_t (i)].folder);
        g.setColour (here ? skin::col::cyan.withAlpha (0.18f) : juce::Colours::white.withAlpha (0.05f));
        g.fillRoundedRectangle (a, 3.0f);
        g.setColour (here ? skin::col::cyan : skin::col::dim);
        g.setFont (skin::screenFont (a.getHeight() * 0.6f, here));
        g.drawFittedText (places[size_t (i)].name, a.toNearestInt(), juce::Justification::centred, 1, 0.7f);
    }
    auto pathRow = headerArea().withTrimmedTop (24);
    g.setColour (skin::col::warmWhite.withAlpha (0.75f));
    g.setFont (skin::screenFont (13.0f));
    g.drawFittedText (proc.browserFolder.getFileName().toUpperCase() + "/", pathRow.reduced (2, 0), juce::Justification::centredLeft, 1, 0.8f);
    g.setColour (autoPreview ? skin::col::amber : skin::col::dim);
    g.drawText (autoPreview ? "ASCOLTO ON" : "ASCOLTO OFF", pathRow.reduced (2, 0), juce::Justification::centredRight);
    g.setColour (juce::Colours::white.withAlpha (0.08f));
    g.drawHorizontalLine (pathRow.getBottom() + 1, 6.0f, float (getWidth() - 6));
}

void BrowserScreen::mouseDown (const juce::MouseEvent& e)
{
    for (int i = 0; i < (int) places.size(); ++i)
        if (placeArea (i).contains (e.getPosition()))
        {
            places[size_t (i)].folder.createDirectory();
            openFolder (places[size_t (i)].folder);
            return;
        }
    auto pathRow = headerArea().withTrimmedTop (24);
    if (pathRow.contains (e.getPosition()) && e.x > pathRow.getRight() - 90)
    {
        autoPreview = ! autoPreview;
        repaint();
    }
}

void BrowserScreen::openFolder (const juce::File& folder)
{
    if (! folder.isDirectory())
        return;
    proc.browserFolder = folder;
    entries.clear();
    if (folder.getParentDirectory() != folder)
        entries.push_back (folder.getParentDirectory());   // ".."

    auto dirs = folder.findChildFiles (juce::File::findDirectories, false);
    dirs.removeIf ([] (const juce::File& f) { return f.isHidden() || f.getFileName().startsWithChar ('.'); });
    dirs.sort();
    auto files = folder.findChildFiles (juce::File::findFiles, false);
    files.removeIf ([] (const juce::File& f) { return ! (Sample::isAudioFile (f) || f.hasFileExtension (".rsu10kit")); });
    files.sort();
    for (auto& d : dirs)  entries.push_back (d);
    for (auto& f : files) entries.push_back (f);

    list.updateContent();
    list.deselectAllRows();
    list.scrollToEnsureRowIsOnscreen (0);
    repaint();
}

void BrowserScreen::up()
{
    openFolder (proc.browserFolder.getParentDirectory());
}

juce::File BrowserScreen::selection() const
{
    const int row = list.getSelectedRow();
    return row >= 0 && row < (int) entries.size() ? entries[size_t (row)] : juce::File();
}

void BrowserScreen::step (int delta, bool preview)
{
    if (entries.empty())
        return;
    const int row = juce::jlimit (0, (int) entries.size() - 1, list.getSelectedRow() + delta);
    list.selectRow (row);
    if (preview)
        previewSelection();
}

void BrowserScreen::previewSelection()
{
    const auto f = selection();
    if (! Sample::isAudioFile (f) || f.getSize() > 40 * 1024 * 1024)
        return;
    if (auto s = Sample::loadFromFile (f))
        proc.engine.startPreview (s);
}

void BrowserScreen::paintListBoxItem (int row, juce::Graphics& g, int w, int h, bool selected)
{
    if (row < 0 || row >= (int) entries.size())
        return;
    const auto& f = entries[size_t (row)];
    const bool parent = row == 0 && f == proc.browserFolder.getParentDirectory();
    if (selected)
    {
        g.setColour (skin::col::cyan.withAlpha (0.2f));
        g.fillRect (0, 0, w, h);
        g.setColour (skin::col::cyan);
        g.fillRect (0, 0, 2, h);
    }
    g.setFont (skin::screenFont (h * 0.66f));
    juce::String text;
    juce::Colour c = skin::col::warmWhite.withAlpha (0.85f);
    if (parent)                        { text = ".."; c = skin::col::dim; }
    else if (f.isDirectory())          { text = f.getFileName() + "/"; c = skin::col::amber; }
    else if (f.hasFileExtension (".rsu10kit")) { text = "KIT  " + f.getFileNameWithoutExtension(); c = skin::col::cyan; }
    else                               text = f.getFileNameWithoutExtension();
    g.setColour (c);
    g.drawFittedText (text, 8, 0, w - 12, h, juce::Justification::centredLeft, 1, 0.75f);
}

void BrowserScreen::selectedRowsChanged (int)
{
    ui.browserSelection = selection();
    if (autoPreview && Sample::isAudioFile (ui.browserSelection))
        previewSelection();
}

void BrowserScreen::listBoxItemDoubleClicked (int row, const juce::MouseEvent&)
{
    returnKeyPressed (row);
}

void BrowserScreen::returnKeyPressed (int row)
{
    if (row < 0 || row >= (int) entries.size())
        return;
    const auto f = entries[size_t (row)];
    if (f.isDirectory())
        openFolder (f);
    else if (f.hasFileExtension (".rsu10kit"))
    {
        juce::String err;
        if (! proc.loadKit (f, err))
            proc.postMessage (err);
    }
    else if (Sample::isAudioFile (f))
        proc.loadSample (proc.selectedPad, ui.viewLayer, f);
}

juce::var BrowserScreen::getDragSourceDescription (const juce::SparseSet<int>& rows)
{
    juce::Array<juce::File> files;
    for (int i = 0; i < rows.size(); ++i)
    {
        const int r = rows[i];
        if (r >= 0 && r < (int) entries.size() && ! (r == 0 && entries[0] == proc.browserFolder.getParentDirectory()))
            files.add (entries[size_t (r)]);
    }
    return files.isEmpty() ? juce::var() : juce::var (filesDescription (files));
}

// ================================================================ MainScreen

juce::Rectangle<float> MainScreen::topBar() const   { return getLocalBounds().toFloat().reduced (8.0f, 4.0f).removeFromTop (getHeight() * 0.13f); }
juce::Rectangle<float> MainScreen::valueBar() const { return getLocalBounds().toFloat().reduced (0.0f, 4.0f).removeFromBottom (getHeight() * 0.2f); }
juce::Rectangle<float> MainScreen::waveArea() const
{
    auto r = getLocalBounds().toFloat().reduced (10.0f, 4.0f);
    r.removeFromTop (getHeight() * 0.15f);
    r.removeFromBottom (getHeight() * 0.22f);
    return r;
}

juce::Rectangle<float> MainScreen::chip (int i) const
{
    auto bar = topBar();
    const float w = bar.getHeight() * 1.6f;
    // da destra: sorgente REC, SCALE, poi gli strati 4..1
    if (i == 5) return bar.removeFromRight (w * 1.9f).reduced (2.0f);
    bar.removeFromRight (w * 1.9f);
    if (i == 4) return bar.removeFromRight (w * 1.5f).reduced (2.0f);
    bar.removeFromRight (w * 1.5f);
    bar.removeFromRight (6.0f);
    for (int l = 3; l > i; --l)
        bar.removeFromRight (w);
    return bar.removeFromRight (w).reduced (2.0f);
}

float MainScreen::xToFrac (float x) const { const auto w = waveArea(); return juce::jlimit (0.0f, 1.0f, (x - w.getX()) / w.getWidth()); }
float MainScreen::fracToX (float f) const { const auto w = waveArea(); return w.getX() + f * w.getWidth(); }

int MainScreen::markerAt (juce::Point<float> p) const
{
    if (! waveArea().expanded (0.0f, 4.0f).contains (p))
        return -1;
    const int pad = proc.selectedPad;
    const float s = proc.pads.get (pad, PP::Start), e = proc.pads.get (pad, PP::End);
    const float loop = s + proc.pads.get (pad, PP::LoopStart) * (e - s);
    const float xs[] { fracToX (s), fracToX (e), fracToX (loop) };
    int best = -1;
    float bestD = 8.0f;
    for (int i = 0; i < 3; ++i)
    {
        if (i == 2 && proc.pads.getInt (pad, PP::PlayMode) != int (PlayMode::Loop))
            continue;
        const float d = std::abs (p.x - xs[i]);
        if (d < bestD) { bestD = d; best = i; }
    }
    return best;
}

void MainScreen::paint (juce::Graphics& g)
{
    const int pad = proc.selectedPad;
    const int layer = juce::jlimit (0, kMaxLayers - 1, ui.viewLayer);
    auto* sample = proc.samples.get (pad, layer);

    // ---- barra in alto
    auto bar = topBar();
    g.setFont (skin::screenFont (bar.getHeight() * 0.72f, true));
    g.setColour (skin::col::amber);
    g.drawText (padLabel (pad), bar.removeFromLeft (bar.getHeight() * 2.0f), juce::Justification::centredLeft);
    g.setColour (skin::col::warmWhite);
    g.setFont (skin::screenFont (bar.getHeight() * 0.62f));
    const auto nameArea = bar.withRight (chip (0).getX() - 6.0f);
    double ageMs = 0.0;
    const auto msg = proc.getMessage (ageMs);
    if (msg.isNotEmpty() && ageMs < 3500.0)
    {
        g.setColour (skin::col::cyan);
        g.drawFittedText (msg.toUpperCase(), nameArea.toNearestInt(), juce::Justification::centredLeft, 1, 0.7f);
    }
    else
    {
        juce::String info = sample != nullptr ? sample->name().toUpperCase() : juce::String ("PAD VUOTO");
        if (sample != nullptr)
        {
            const double secs = sample->seconds() * (proc.pads.get (pad, PP::End) - proc.pads.get (pad, PP::Start));
            info << "   " << juce::String (secs, 2) << " s";
            if (secs > 1.0)   // tempo del loop, come sull'SU10
            {
                const int beats = Sample::guessBeats (secs);
                info << "   " << beats << " BAT = " << juce::String (Sample::bpmFor (secs, beats), 1) << " BPM";
            }
        }
        g.drawFittedText (info, nameArea.toNearestInt(), juce::Justification::centredLeft, 1, 0.7f);
    }

    for (int l = 0; l < kMaxLayers; ++l)
    {
        const auto c = chip (l);
        const bool has = proc.samples.get (pad, l) != nullptr;
        const bool sel = l == layer;
        const bool lit = proc.engine.padVoices[pad].load() > 0 && proc.engine.padLastLayer[pad].load() == l;
        g.setColour (sel ? skin::col::amber.withAlpha (0.25f) : juce::Colours::white.withAlpha (0.04f));
        g.fillRoundedRectangle (c, 3.0f);
        g.setColour (lit ? skin::col::warmWhite : sel ? skin::col::amber : has ? skin::col::warmWhite.withAlpha (0.6f) : skin::col::dim.withAlpha (0.5f));
        g.setFont (skin::screenFont (c.getHeight() * 0.62f, sel));
        g.drawText ("V" + juce::String (l + 1), c, juce::Justification::centred);
    }
    {
        const bool scaleOn = proc.apvts.getRawParameterValue (ParamID::scale)->load() > 0.5f;
        const auto c = chip (4);
        g.setColour (scaleOn ? skin::col::cyan.withAlpha (0.25f) : juce::Colours::white.withAlpha (0.04f));
        g.fillRoundedRectangle (c, 3.0f);
        g.setColour (scaleOn ? skin::col::cyan : skin::col::dim);
        g.setFont (skin::screenFont (c.getHeight() * 0.62f, scaleOn));
        g.drawText ("SCALE", c, juce::Justification::centred);

        const auto r = chip (5);
        g.setColour (juce::Colours::white.withAlpha (0.04f));
        g.fillRoundedRectangle (r, 3.0f);
        g.setColour (skin::col::dim);
        g.drawText (proc.recSource.load() == 0 ? "REC: INGR." : "REC: USCITA", r, juce::Justification::centred);
    }

    // ---- forma d'onda
    const auto wave = waveArea();
    g.setColour (juce::Colours::white.withAlpha (0.05f));
    for (int i = 1; i < 8; ++i)
        g.drawVerticalLine (int (wave.getX() + wave.getWidth() * float (i) / 8.0f), wave.getY(), wave.getBottom());
    g.drawHorizontalLine (int (wave.getCentreY()), wave.getX(), wave.getRight());

    const auto rec = proc.engine.recState();
    if (rec == rsu::Engine::RecState::Armed || rec == rsu::Engine::RecState::Recording)
    {
        g.setColour (skin::col::recRed);
        g.setFont (skin::screenFont (wave.getHeight() * 0.16f, true));
        g.drawText (rec == rsu::Engine::RecState::Armed ? "REC  -  IN ATTESA DEL SEGNALE"
                                                        : "REC  " + juce::String (proc.engine.recordedSeconds(), 1) + " s",
                    wave, juce::Justification::centred);
        return;
    }

    if (sample == nullptr)
    {
        g.setColour (skin::col::dim);
        g.setFont (skin::screenFont (wave.getHeight() * 0.1f));
        g.drawText ("TRASCINA UN CAMPIONE SUL PAD  -  SHIFT + TRASCINA = STRATO V" + juce::String (proc.samples.layerCount (pad) + 1),
                    wave, juce::Justification::centred);
    }
    else
    {
        const auto& peaks = sample->peaks();
        const float s = proc.pads.get (pad, PP::Start), e = proc.pads.get (pad, PP::End);
        const bool rev = proc.pads.getInt (pad, PP::Reverse) != 0;
        const float mid = wave.getCentreY(), half = wave.getHeight() * 0.46f;
        const int w = int (wave.getWidth());
        for (int x = 0; x < w; ++x)
        {
            const float f = float (x) / float (w);
            const auto& pk = peaks[size_t (juce::jlimit (0, Sample::kPeakBins - 1, int (f * Sample::kPeakBins)))];
            const bool inside = f >= s && f <= e;
            g.setColour (inside ? (rev ? skin::col::cyan : skin::col::amber) : skin::col::dim.withAlpha (0.35f));
            g.drawVerticalLine (int (wave.getX()) + x, mid - pk.second * half, mid - pk.first * half + 1.0f);
        }

        // fade
        const double secs = sample->seconds() * juce::jmax (0.001f, e - s);
        const float fin = float (proc.pads.get (pad, PP::FadeIn) * 0.001 / secs) * (e - s);
        const float fout = float (proc.pads.get (pad, PP::FadeOut) * 0.001 / secs) * (e - s);
        g.setColour (skin::col::warmWhite.withAlpha (0.7f));
        if (fin > 0.0005f)
            g.drawLine (fracToX (s), wave.getBottom(), fracToX (juce::jmin (e, s + fin)), wave.getY(), 1.2f);
        if (fout > 0.0005f)
            g.drawLine (fracToX (juce::jmax (s, e - fout)), wave.getY(), fracToX (e), wave.getBottom(), 1.2f);

        // marcatori
        auto marker = [&] (float f, juce::Colour c, const char* label, bool hot)
        {
            const float x = fracToX (f);
            g.setColour (c.withAlpha (hot ? 1.0f : 0.85f));
            g.fillRect (juce::Rectangle<float> (hot ? 3.0f : 2.0f, wave.getHeight()).withCentre ({ x, wave.getCentreY() }));
            g.setFont (skin::screenFont (11.0f, true));
            g.drawText (label, juce::Rectangle<float> (x + 3.0f, wave.getY(), 40.0f, 12.0f), juce::Justification::centredLeft);
        };
        marker (s, skin::col::warmWhite, "S", hoverMarker == 0 || dragMarker == 0);
        marker (e, skin::col::warmWhite, "E", hoverMarker == 1 || dragMarker == 1);
        if (proc.pads.getInt (pad, PP::PlayMode) == int (PlayMode::Loop))
            marker (s + proc.pads.get (pad, PP::LoopStart) * (e - s), skin::col::cyan, "LOOP", hoverMarker == 2 || dragMarker == 2);

        if (proc.engine.padVoices[pad].load() > 0 && proc.engine.padLastLayer[pad].load() == layer)
        {
            const float x = fracToX (proc.engine.padPosition[pad].load());
            g.setColour (juce::Colours::white);
            g.drawVerticalLine (int (x), wave.getY(), wave.getBottom());
        }
    }

    // ---- valori delle 8 manopole, allineati alle colonne
    const auto vb = valueBar();
    g.setColour (juce::Colours::white.withAlpha (0.08f));
    g.drawHorizontalLine (int (vb.getY()), 6.0f, float (getWidth() - 6));
    const float colW = vb.getWidth() / 8.0f;
    const int pg = juce::jlimit (0, 3, proc.page.load());
    for (int k = 0; k < 8; ++k)
    {
        const int idx = pg * 8 + k;
        auto c = juce::Rectangle<float> (vb.getX() + colW * float (k), vb.getY() + 2.0f, colW, vb.getHeight() - 2.0f).reduced (3.0f, 0.0f);
        if (idx >= kNumPP)
            continue;
        g.setColour (skin::col::dim);
        g.setFont (skin::screenFont (c.getHeight() * 0.36f));
        g.drawFittedText (ppDefs()[size_t (idx)].name, c.removeFromTop (c.getHeight() * 0.48f).toNearestInt(), juce::Justification::centred, 1, 0.6f);
        const float v = proc.pads.get (pad, PP (idx));
        const bool changed = ! juce::exactlyEqual (v, ppDefs()[size_t (idx)].def);
        g.setColour (changed ? skin::col::amber : skin::col::warmWhite);
        g.setFont (skin::screenFont (c.getHeight() * 0.44f, true));
        g.drawFittedText (formatValue (PP (idx), v), c.toNearestInt(), juce::Justification::centred, 1, 0.6f);
    }
}

void MainScreen::mouseDown (const juce::MouseEvent& e)
{
    const int pad = proc.selectedPad;
    for (int l = 0; l < kMaxLayers; ++l)
        if (chip (l).contains (e.position))
        {
            ui.viewLayer = l;
            repaint();
            return;
        }
    if (chip (4).contains (e.position))
    {
        auto* p = proc.apvts.getParameter (ParamID::scale);
        const bool on = p->getValue() < 0.5f;
        if (on)
            proc.scalePad = pad;   // il pad selezionato diventa la tastiera
        p->setValueNotifyingHost (on ? 1.0f : 0.0f);
        proc.postMessage (on ? "SCALE: " + padLabel (pad) + " su tutte le note (C3 = originale)" : "SCALE spento");
        repaint();
        return;
    }
    if (chip (5).contains (e.position))
    {
        proc.recSource = proc.recSource.load() == 0 ? 1 : 0;
        repaint();
        return;
    }
    dragMarker = markerAt (e.position);
}

void MainScreen::mouseDrag (const juce::MouseEvent& e)
{
    if (dragMarker < 0)
        return;
    const int pad = proc.selectedPad;
    const float f = xToFrac (e.position.x);
    const float s = proc.pads.get (pad, PP::Start), en = proc.pads.get (pad, PP::End);
    if (dragMarker == 0)      proc.pads.set (pad, PP::Start, juce::jmin (f, en - 0.001f));
    else if (dragMarker == 1) proc.pads.set (pad, PP::End, juce::jmax (f, s + 0.001f));
    else                      proc.pads.set (pad, PP::LoopStart, juce::jlimit (0.0f, 1.0f, (f - s) / juce::jmax (0.001f, en - s)));
    repaint();
}

void MainScreen::mouseUp (const juce::MouseEvent&)
{
    dragMarker = -1;
}

void MainScreen::mouseMove (const juce::MouseEvent& e)
{
    const int m = markerAt (e.position);
    if (m != hoverMarker)
    {
        hoverMarker = m;
        setMouseCursor (m >= 0 ? juce::MouseCursor::LeftRightResizeCursor : juce::MouseCursor::NormalCursor);
        repaint();
    }
}

// ================================================================ KitScreen

void KitScreen::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced (8.0f, 2.0f);
    g.setColour (skin::col::amber);
    g.setFont (skin::screenFont (r.getHeight() * 0.55f, true));
    g.drawFittedText ("KIT  " + proc.kitName, r.toNearestInt(), juce::Justification::centredLeft, 1, 0.7f);

    // misuratore d'uscita a destra
    const float peak = juce::jlimit (0.0f, 1.0f, proc.engine.outPeak.load());
    auto meter = r.removeFromRight (r.getWidth() * 0.22f).reduced (0.0f, r.getHeight() * 0.36f);
    g.setColour (juce::Colours::white.withAlpha (0.08f));
    g.fillRect (meter);
    g.setColour (peak > 0.98f ? skin::col::recRed : skin::col::cyan);
    g.fillRect (meter.withWidth (meter.getWidth() * std::sqrt (peak)));
}
