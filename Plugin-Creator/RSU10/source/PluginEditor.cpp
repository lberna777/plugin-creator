#include "PluginEditor.h"

using namespace rsu;

namespace
{
constexpr int kTimerHz = 30;
}

RSU10Editor::RSU10Editor (RSU10AudioProcessor& p)
    : AudioProcessorEditor (p), proc (p), layout (BinaryData::layout_json, BinaryData::layout_jsonSize)
{
    addAndMakeVisible (kitScreen);
    addAndMakeVisible (browser);
    addAndMakeVisible (mainScreen);
    addAndMakeVisible (ribbon);
    kitScreen.onClick = [this] { kitMenu(); };

    for (int k = 0; k < kKnobs; ++k)
    {
        knobs[size_t (k)] = std::make_unique<Knob> (proc, ui);
        addAndMakeVisible (*knobs[size_t (k)]);
    }
    for (int i = 0; i < kPadsPerBank; ++i)
    {
        padViews[size_t (i)] = std::make_unique<PadView> (proc, ui, i);
        addAndMakeVisible (*padViews[size_t (i)]);
    }

    // ---- intestazione
    setupKey (keySave, [this] { saveKitAs(); });
    setupKey (keyLoad, [this] { openKit(); });

    // ---- tasti funzione dell'SU10
    setupKey (keyHold, [this] { proc.runCommand (Cmd::Hold); },
              [this] { return proc.apvts.getRawParameterValue (ParamID::ribbonHold)->load() > 0.5f; });
    setupKey (keyExit, [this] { proc.runCommand (Cmd::Exit); });
    setupKey (keyRev, [this] { setRibbonMode (int (RibbonMode::Reverse)); },
              [this] { return juce::roundToInt (proc.apvts.getRawParameterValue (ParamID::ribbonMode)->load()) == int (RibbonMode::Reverse); });
    setupKey (keyScratch, [this] { setRibbonMode (int (RibbonMode::Scratch)); },
              [this] { return juce::roundToInt (proc.apvts.getRawParameterValue (ParamID::ribbonMode)->load()) == int (RibbonMode::Scratch); });
    setupKey (keyShift, [this] { ui.shift = ! ui.shift; }, [this] { return ui.shift; });
    setupKey (keyBank, [this] { proc.runCommand (Cmd::Bank); },
              [this] { return ui.bank > 0; });

    // ---- pagine: i 5 tasti in gomma, scritta sul tasto (ambra = pagina attiva)
    for (int i = 0; i < kPages; ++i)
    {
        pageKeys[size_t (i)] = std::make_unique<Key> (kPageNames[i], skin::KeyStyle::Rubber);
        setupKey (*pageKeys[size_t (i)], [this, i] { proc.page = i; }, [this, i] { return proc.page.load() == i; }, false);
    }

    // ---- funzioni della Maschine: col mouse il tasto si aggancia (LED), sulla Mikro si tiene premuto
    auto actionKey = [this] (Key& k, PadAction a)
    {
        setupKey (k, [this, a] { proc.toggleUiAction (a); }, [this, a] { return proc.action() == a; });
    };
    actionKey (keyMute, PadAction::Mute);
    actionKey (keySolo, PadAction::Solo);
    actionKey (keySelect, PadAction::Select);
    actionKey (keyDup, PadAction::Duplicate);
    actionKey (keyErase, PadAction::Erase);
    setupKey (keyUndo, [this] { proc.runCommand (std::exchange (ui.shift, false) ? Cmd::Redo : Cmd::Undo); });
    setupKey (keyRepeat, [this]
    {
        if (std::exchange (ui.shift, false))
            proc.runCommand (Cmd::RepeatRate);   // SHIFT + REPEAT: griglia
        else
        {
            proc.repeatUi = ! proc.repeatUi.load();
            proc.postMessage (proc.repeatUi.load() ? juce::String ("NOTE REPEAT ") + RSU10AudioProcessor::kRepeatRateNames[proc.repeatRate.load()]
                                                     + ": tieni premuto un pad" : juce::String ("NOTE REPEAT spento"));
        }
    }, [this] { return proc.repeatUi.load() || proc.repeatHeld.load(); });
    setupKey (keyPadMode, [this] { proc.runCommand (Cmd::NextPadMode); }, [this] { return proc.padMode() != PadMode::Normal; });
    setupKey (keyFixVel, [this] { proc.runCommand (Cmd::FixedVel); }, [this] { return proc.fixedVelocity.load(); });

    setupKey (keyEnter, [this] { enterKey(); }, nullptr, false);
    setupKey (keyMinus, [this] { proc.runCommand (Cmd::SampleMinus); }, nullptr, false);
    setupKey (keyPlus,  [this] { proc.runCommand (Cmd::SamplePlus); }, nullptr, false);
    setupKey (keyLeft,  [this] { proc.runCommand (Cmd::PrevPad); }, nullptr, false);
    setupKey (keyRight, [this] { proc.runCommand (Cmd::NextPad); }, nullptr, false);

    // ---- trasporto
    setupKey (keyRec, [this] { proc.runCommand (Cmd::Rec); }, [this] { return proc.engine.recState() != Engine::RecState::Idle; });
    setupKey (keyTop,  [this] { proc.runCommand (Cmd::Top); });     // ribbon al centro, HOLD spento, anteprima ferma
    setupKey (keyBwd,  [this] { proc.runCommand (Cmd::Bwd); });
    setupKey (keyFwd,  [this] { proc.runCommand (Cmd::Fwd); });
    setupKey (keyStop, [this] { proc.runCommand (Cmd::Stop); });
    setupKey (keyPlay, [this] { proc.runCommand (Cmd::Play); }, [this] { return proc.engine.previewPlaying.load(); });

    // I tasti della Mikro che servono il browser arrivano qui
    proc.editorCommand = [this] (Cmd c) { return editorCommand (c); };
    for (int k = 0; k < kKnobs; ++k)
        knobs[size_t (k)]->setParam (pageParam (proc.page.load(), k));

    ui.bank = proc.selectedPad / kPadsPerBank;

    constrainer.setFixedAspectRatio (1.6);
    constrainer.setSizeLimits (960, 600, 1920, 1200);
    setConstrainer (&constrainer);
    setResizable (true, true);
    setSize (1280, 800);
    setWantsKeyboardFocus (true);
    startTimerHz (kTimerHz);
}

RSU10Editor::~RSU10Editor()
{
    stopTimer();
    proc.editorCommand = nullptr;
}

bool RSU10Editor::editorCommand (Cmd c)
{
    switch (c)
    {
        case Cmd::Play:  transportPlay(); return true;
        case Cmd::Bwd:   browser.step (-1, true); return true;
        case Cmd::Fwd:   browser.step (+1, true); return true;
        case Cmd::Enter: enterKey(); return true;
        case Cmd::Exit:
            if (proc.engine.recState() != Engine::RecState::Idle)
                proc.stopRecording();
            else
                browser.up();
            return true;
        default:
            return false;
    }
}

void RSU10Editor::enterKey()
{
    // ENTER (come sull'SU10): conferma la scelta del browser. Cartella = entra, suono = va nel pad scelto
    const auto f = browser.selection();
    if (f.isDirectory())
        browser.openFolder (f);
    else if (Sample::isAudioFile (f))
        proc.loadSample (proc.selectedPad, ui.viewLayer, f);
    else
        proc.postMessage ("ENTER: scegli un suono nel browser a sinistra, ENTER lo carica nel pad " + padLabel (proc.selectedPad));
}

juce::Rectangle<int> RSU10Editor::gridCell (const char* group, int col, int row) const
{
    const juce::String g (group);
    const float x0 = layout.number (g + "/c" + juce::String (col) + "/x0"), x1 = layout.number (g + "/c" + juce::String (col) + "/x1");
    const float y0 = layout.number (g + "/r" + juce::String (row) + "/y0"), y1 = layout.number (g + "/r" + juce::String (row) + "/y1");
    const auto b = getLocalBounds().toFloat();
    return juce::Rectangle<float>::leftTopRightBottom (b.getX() + x0 * b.getWidth(), b.getY() + y0 * b.getHeight(),
                                                        b.getX() + x1 * b.getWidth(), b.getY() + y1 * b.getHeight()).toNearestInt();
}

void RSU10Editor::setupKey (Key& k, std::function<void()> onClick, std::function<bool()> led, bool labelAbove)
{
    k.onClick = std::move (onClick);
    k.led = std::move (led);
    k.labelOnKey = ! labelAbove && k.style == skin::KeyStyle::Rubber;
    addAndMakeVisible (k);
    labelled.push_back ({ &k, labelAbove });
}

void RSU10Editor::toggleParam (const char* id)
{
    if (auto* p = proc.apvts.getParameter (id))
        p->setValueNotifyingHost (p->getValue() > 0.5f ? 0.0f : 1.0f);
}

void RSU10Editor::setRibbonMode (int mode)
{
    auto* p = proc.apvts.getParameter (ParamID::ribbonMode);
    p->beginChangeGesture();
    p->setValueNotifyingHost (p->convertTo0to1 (float (mode)));
    p->endChangeGesture();
}

void RSU10Editor::transportPlay()
{
    if (Sample::isAudioFile (browser.selection()))
        browser.previewSelection();
    else
    {
        proc.engine.triggerFromUi (proc.selectedPad, 110, true);
        proc.engine.triggerFromUi (proc.selectedPad, 0, false);
    }
}

void RSU10Editor::selectPad (int pad)
{
    proc.selectedPad = juce::jlimit (0, kNumPads - 1, pad);
    ui.bank = proc.selectedPad / kPadsPerBank;
    ui.viewLayer = 0;
    repaint();
}

// ---------------------------------------------------------------- kit

void RSU10Editor::kitMenu()
{
    juce::PopupMenu m;
    m.addItem (1, "Carica kit...");
    m.addItem (2, "Salva kit con nome...");
    m.addSeparator();
    m.addItem (3, "Kit di prova");
    m.addItem (4, "Kit vuoto");
    m.addSeparator();
    m.addItem (5, "Ogni pad nel suo canale del mixer (pad 1 MAIN, pad 2-16 OUT 2-16)");
    m.addItem (6, "Tutti i pad sulla MAIN");
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&kitScreen), [this] (int r)
    {
        if (r == 1) openKit();
        else if (r == 2) saveKitAs();
        else if (r == 3) { proc.engine.panic(); proc.loadDemoKit(); }
        else if (r == 4)
        {
            proc.engine.panic();
            proc.samples.clearAll();
            proc.pads.resetAll();
            proc.kitName = "NUOVO KIT";
            proc.kitFile = juce::File();
        }
        else if (r == 5 || r == 6)
        {
            // pad n del banco -> uscita n (le 16 uscite stereo del plugin, una per pad)
            for (int p = 0; p < kNumPads; ++p)
                proc.pads.set (p, PP::Output, r == 5 ? float (p % kPadsPerBank) : 0.0f);
            proc.postMessage (r == 5 ? "Uscite separate: in FL abilita le uscite nel plugin (ingranaggio > Processing)"
                                     : "Tutti i pad sulla MAIN");
        }
        repaint();
    });
}

void RSU10Editor::saveKitAs()
{
    const auto folder = RSU10AudioProcessor::userFolder().getChildFile ("Kit");
    folder.createDirectory();
    const auto start = proc.kitFile != juce::File() ? proc.kitFile : folder.getChildFile (proc.kitName.toLowerCase() + ".rsu10kit");
    chooser = std::make_unique<juce::FileChooser> ("Salva il kit", start, "*.rsu10kit");
    chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting,
                          [this] (const juce::FileChooser& fc)
                          {
                              const auto f = fc.getResult();
                              if (f == juce::File())
                                  return;
                              juce::String err;
                              if (proc.saveKit (f, err))
                                  proc.postMessage ("Kit salvato: " + f.getFileNameWithoutExtension());
                              else
                                  proc.postMessage (err);
                              repaint();
                          });
}

void RSU10Editor::openKit()
{
    const auto folder = RSU10AudioProcessor::userFolder().getChildFile ("Kit");
    chooser = std::make_unique<juce::FileChooser> ("Carica un kit", folder.isDirectory() ? folder : proc.browserFolder, "*.rsu10kit");
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [this] (const juce::FileChooser& fc)
                          {
                              const auto f = fc.getResult();
                              if (! f.existsAsFile())
                                  return;
                              juce::String err;
                              if (! proc.loadKit (f, err))
                                  proc.postMessage (err);
                              repaint();
                          });
}

// ---------------------------------------------------------------- disegno

void RSU10Editor::renderBackground()
{
    const auto bounds = getLocalBounds();
    if (bounds.isEmpty())
        return;
    const float scale = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay() != nullptr
                            ? (float) juce::Desktop::getInstance().getDisplays().getPrimaryDisplay()->scale : 1.0f;
    background = juce::Image (juce::Image::ARGB, juce::roundToInt (bounds.getWidth() * scale), juce::roundToInt (bounds.getHeight() * scale), true);
    juce::Graphics g (background);
    g.addTransform (juce::AffineTransform::scale (scale));

    const auto r = bounds.toFloat();
    skin::drawBody (g, r);
    const bool customPlate = skin::asset ("bg_plate.jpg").isValid();

    auto rect = [&] (const char* path) { return layout.rect (path, bounds).toFloat(); };
    const float H = r.getHeight();

    if (! customPlate)
    {
        skin::drawBezel (g, rect ("kit/bezel"), rect ("kit/glass"));
        skin::drawBezel (g, rect ("browser/bezel"), rect ("browser/glass"));
        skin::drawBezel (g, rect ("main/bezel"), rect ("main/glass"));
        skin::drawAluPlate (g, rect ("knobPlate"));
        skin::drawAluPlate (g, rect ("badge"));
    }

    // Serigrafia rossa sotto lo schermo, come "SAMPLING UNIT SU10"
    auto silk = rect ("main/silk");
    skin::drawSilk (g, "RESTOMOD SAMPLING UNIT", silk.removeFromLeft (silk.getWidth() * 0.62f), juce::Justification::centredRight,
                    H * 0.020f, skin::col::silkRed);
    silk.removeFromLeft (silk.getWidth() * 0.04f);
    g.setColour (skin::col::silkRed);
    g.setFont (juce::Font (juce::FontOptions (H * 0.036f, juce::Font::bold | juce::Font::italic)).withHorizontalScale (1.25f));
    g.drawText ("RSU-10", silk, juce::Justification::centredLeft);

    // Targhetta in alluminio (anima nuova)
    auto badge = rect ("badge").reduced (H * 0.012f, H * 0.006f);
    g.setFont (juce::Font (juce::FontOptions (H * 0.040f, juce::Font::bold)).withExtraKerningFactor (0.06f));
    auto logo = badge.removeFromLeft (badge.getWidth() * 0.56f);
    g.setColour (juce::Colours::black.withAlpha (0.55f));
    g.drawFittedText ("RSU-10", logo.translated (0.0f, 1.0f).toNearestInt(), juce::Justification::centredLeft, 1, 0.6f);
    g.setColour (juce::Colours::white.withAlpha (0.85f));
    g.drawFittedText ("RSU-10", logo.toNearestInt(), juce::Justification::centredLeft, 1, 0.6f);
    skin::drawSilk (g, juce::String ("RESTOMOD\nSAMPLING UNIT  v") + JucePlugin_VersionString, badge, juce::Justification::centredRight,
                    H * 0.014f, juce::Colours::black.withAlpha (0.6f));

    // Etichette sopra i tasti (serigrafia bianca dell'SU10)
    for (auto& l : labelled)
        if (l.above)
        {
            const auto kb = l.key->getBounds().toFloat();
            const float lh = H * 0.0145f;
            auto lr = juce::Rectangle<float> (kb.getX() - 6.0f, kb.getY() - lh * 1.3f, kb.getWidth() + 12.0f, lh * 1.2f);
            skin::drawSilk (g, l.key->getButtonText(), lr, juce::Justification::centred, lh);
        }

}

void RSU10Editor::paint (juce::Graphics& g)
{
    if (background.isNull())
        renderBackground();
    g.drawImage (background, getLocalBounds().toFloat());
}

void RSU10Editor::paintOverChildren (juce::Graphics& g)
{
    // Vetro sopra gli schermi: un riflesso diagonale e un filo di luce sul bordo alto,
    // così il contenuto sta "dietro" il vetro come nella foto della piastra
    const auto b = getLocalBounds();
    for (const char* glass : { "kit/glass", "browser/glass", "main/glass", "ribbon" })
    {
        const auto r = layout.rect (glass, b).toFloat();
        juce::Path refl;
        refl.startNewSubPath (r.getX() + r.getWidth() * 0.52f, r.getY());
        refl.lineTo (r.getX() + r.getWidth() * 0.80f, r.getY());
        refl.lineTo (r.getX() + r.getWidth() * 0.42f, r.getBottom());
        refl.lineTo (r.getX() + r.getWidth() * 0.14f, r.getBottom());
        refl.closeSubPath();
        g.saveState();
        g.reduceClipRegion (r.toNearestInt());
        g.setColour (juce::Colours::white.withAlpha (0.035f));
        g.fillPath (refl);
        g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (0.06f), r.getX(), r.getY(),
                                                 juce::Colours::transparentWhite, r.getX(), r.getY() + r.getHeight() * 0.18f, false));
        g.fillRect (r);
        g.restoreState();
    }

    // LED moderni accanto alle etichette dei tasti che hanno uno stato
    for (auto& l : labelled)
        if (l.key->led != nullptr && l.above)
        {
            const auto kb = l.key->getBounds().toFloat();
            const float rad = juce::jmax (2.0f, getHeight() * 0.0035f);
            const auto c = l.key->style == skin::KeyStyle::Rec ? skin::col::recRed : skin::col::amber;
            if (l.key->style == skin::KeyStyle::Rec)
            {
                skin::drawLed (g, { kb.getRight() + rad * 2.5f, kb.getY() + rad }, rad, l.key->led(), c);
                continue;
            }
            const float lh = getHeight() * 0.0145f;
            const float textW = juce::GlyphArrangement::getStringWidth (skin::silkFont (lh), l.key->getButtonText());
            skin::drawLed (g, { kb.getCentreX() - textW * 0.5f - rad * 3.0f, kb.getY() - lh * 0.75f }, rad, l.key->led(), c);
        }
}

void RSU10Editor::resized()
{
    const auto b = getLocalBounds();
    auto rect = [&] (const char* path) { return layout.rect (path, b); };

    kitScreen.setBounds (rect ("kit/glass"));
    browser.setBounds (rect ("browser/glass"));
    mainScreen.setBounds (rect ("main/glass"));
    ribbon.setBounds (rect ("ribbon"));

    // 7 manopole sui fori della placca, colonne uguali a quelle dei valori sullo schermo
    {
        auto k = rect ("knobs").toFloat();
        const float w = k.getWidth() / float (kKnobs);
        for (int i = 0; i < kKnobs; ++i)
            knobs[size_t (i)]->setBounds (juce::Rectangle<float> (k.getX() + w * float (i), k.getY(), w, k.getHeight()).toNearestInt());
    }

    // I tasti stanno nei fori misurati della piastra, appena più piccoli del foro
    auto inHole = [] (juce::Rectangle<int> hole)
    {
        return hole.toFloat().reduced (hole.getWidth() * 0.05f, hole.getHeight() * 0.07f).toNearestInt();
    };

    // funzioni della Maschine: 3x3
    {
        Key* grid[3][3] { { &keyMute, &keySolo, &keySelect }, { &keyDup, &keyErase, &keyUndo }, { &keyRepeat, &keyPadMode, &keyFixVel } };
        for (int row = 0; row < 3; ++row)
            for (int col = 0; col < 3; ++col)
                grid[row][col]->setBounds (inHole (gridCell ("maschine", col, row)));
    }
    // tasti dell'SU10: 2x3, EXIT viola in alto a destra
    {
        Key* grid[3][2] { { &keyHold, &keyExit }, { &keyRev, &keyScratch }, { &keyShift, &keyBank } };
        for (int row = 0; row < 3; ++row)
            for (int col = 0; col < 2; ++col)
                grid[row][col]->setBounds (inHole (gridCell ("su10", col, row)));
    }
    for (int i = 0; i < kPages; ++i)
        pageKeys[size_t (i)]->setBounds (inHole (rect (juce::String ("pages/p" + juce::String (i)).toRawUTF8())));
    keyEnter.setBounds (inHole (rect ("enter")));
    keyMinus.setBounds (inHole (rect ("minus")));
    keyPlus.setBounds (inHole (rect ("plus")));
    keyLeft.setBounds (inHole (rect ("left")));
    keyRight.setBounds (inHole (rect ("right")));
    keySave.setBounds (inHole (rect ("keySave")));
    keyLoad.setBounds (inHole (rect ("keyLoad")));

    // trasporto: REC tondo + 5 tasti in gomma
    keyRec.setBounds (inHole (rect ("transport/rec")));
    keyTop.setBounds (inHole (rect ("transport/top")));
    keyBwd.setBounds (inHole (rect ("transport/bwd")));
    keyStop.setBounds (inHole (rect ("transport/stop")));
    keyPlay.setBounds (inHole (rect ("transport/play")));
    keyFwd.setBounds (inHole (rect ("transport/fwd")));

    // 4x4 pad, pad 1 in basso a sinistra (come la Mikro)
    {
        auto p = rect ("pads").toFloat();
        const float cw = p.getWidth() / 4.0f, ch = p.getHeight() / 4.0f;
        for (int i = 0; i < kPadsPerBank; ++i)
        {
            const int col = i % 4, row = 3 - i / 4;
            padViews[size_t (i)]->setBounds (juce::Rectangle<float> (p.getX() + cw * col, p.getY() + ch * row, cw, ch).toNearestInt());
        }
    }

    background = {};
    repaint();
}

// ---------------------------------------------------------------- tastiera del computer

bool RSU10Editor::keyPressed (const juce::KeyPress& k)
{
    if (k == juce::KeyPress::leftKey)  { selectPad ((proc.selectedPad + kNumPads - 1) % kNumPads); return true; }
    if (k == juce::KeyPress::rightKey) { selectPad ((proc.selectedPad + 1) % kNumPads); return true; }
    if (k == juce::KeyPress::spaceKey) { transportPlay(); return true; }
    if (k == juce::KeyPress::deleteKey || k == juce::KeyPress::backspaceKey) { proc.clearSamples (proc.selectedPad); return true; }
    return false;
}

// ---------------------------------------------------------------- animazione

void RSU10Editor::timerCallback()
{
    // LED dei pad: lampo sul colpo che sfuma
    for (int i = 0; i < kPadsPerBank; ++i)
    {
        auto& pv = *padViews[size_t (i)];
        const int pad = pv.padIndex();
        const float hit = proc.engine.padHit[pad].exchange (0.0f);
        const float before = pv.glow;
        pv.glow = juce::jmax (hit, pv.glow * 0.82f);
        if (pv.glow > 0.01f || before > 0.01f || proc.engine.padVoices[pad].load() > 0)
            pv.repaint();
    }

    // Manopole: seguono pagina e pad selezionato
    const int page = proc.page.load(), sel = proc.selectedPad.load();
    const auto gen = proc.pads.generation.load(), sgen = proc.samples.generation.load();
    if (page != lastPage || sel != lastSelected || gen != lastGeneration || sgen != lastSampleGen)
    {
        for (int k = 0; k < kKnobs; ++k)
            knobs[size_t (k)]->setParam (pageParam (page, k));
        if (sel / kPadsPerBank != ui.bank && sel != lastSelected)
            ui.bank = sel / kPadsPerBank;
        if (sel != lastSelected || sgen != lastSampleGen)
            for (auto& pv : padViews)
                pv->repaint();
        lastPage = page;
        lastSelected = sel;
        lastGeneration = gen;
        lastSampleGen = sgen;
        repaint();   // LED delle pagine e dei tasti
    }

    mainScreen.repaint();
    kitScreen.repaint();
    ribbon.repaint();

    // LED dei tasti con stato
    static uint32_t tick = 0;
    if (++tick % 6 == 0)
        repaint();
}
