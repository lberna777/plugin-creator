#include "PluginEditor.h"

using namespace rsu;

namespace
{
const char* kPageNames[] { "SUONO", "TAGLIO", "COLORE", "USCITA" };
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

    for (int k = 0; k < 8; ++k)
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
    setupKey (keyHold, [this] { toggleParam (ParamID::ribbonHold); },
              [this] { return proc.apvts.getRawParameterValue (ParamID::ribbonHold)->load() > 0.5f; });
    setupKey (keyExit, [this]
    {
        if (proc.engine.recState() != Engine::RecState::Idle)
            proc.stopRecording();
        else
            browser.up();
    });
    setupKey (keyRev, [this] { setRibbonMode (int (RibbonMode::Reverse)); },
              [this] { return juce::roundToInt (proc.apvts.getRawParameterValue (ParamID::ribbonMode)->load()) == int (RibbonMode::Reverse); });
    setupKey (keyScratch, [this] { setRibbonMode (int (RibbonMode::Scratch)); },
              [this] { return juce::roundToInt (proc.apvts.getRawParameterValue (ParamID::ribbonMode)->load()) == int (RibbonMode::Scratch); });
    setupKey (keyShift, [this] { ui.shift = ! ui.shift; }, [this] { return ui.shift; });
    setupKey (keyBank, [this] { ui.bank = (ui.bank + 1) % kBanks; selectPad (ui.bank * kPadsPerBank + proc.selectedPad % kPadsPerBank); },
              [this] { return ui.bank > 0; });

    for (int i = 0; i < 4; ++i)
    {
        pageKeys[size_t (i)] = std::make_unique<Key> (kPageNames[i], skin::KeyStyle::Wide);
        setupKey (*pageKeys[size_t (i)], [this, i] { proc.page = i; }, [this, i] { return proc.page.load() == i; });
    }

    setupKey (keyEnter, [this]
    {
        const auto f = browser.selection();
        if (f.isDirectory())
            browser.openFolder (f);
        else if (Sample::isAudioFile (f))
            proc.loadSample (proc.selectedPad, ui.viewLayer, f);
    }, nullptr, false);
    setupKey (keyMinus, [this] { proc.stepSampleInFolder (proc.selectedPad, -1); }, nullptr, false);
    setupKey (keyPlus,  [this] { proc.stepSampleInFolder (proc.selectedPad, +1); }, nullptr, false);
    setupKey (keyLeft,  [this] { selectPad ((proc.selectedPad + kNumPads - 1) % kNumPads); }, nullptr, false);
    setupKey (keyRight, [this] { selectPad ((proc.selectedPad + 1) % kNumPads); }, nullptr, false);

    // ---- trasporto
    setupKey (keyRec, [this]
    {
        if (proc.engine.recState() == Engine::RecState::Idle)
            proc.startRecording();
        else
            proc.stopRecording();
    }, [this] { return proc.engine.recState() != Engine::RecState::Idle; });
    setupKey (keyTop, [this]
    {
        // ribbon al centro, HOLD spento, anteprima ferma
        proc.apvts.getParameter (ParamID::ribbon)->setValueNotifyingHost (0.5f);
        proc.apvts.getParameter (ParamID::ribbonHold)->setValueNotifyingHost (0.0f);
        proc.engine.stopPreview();
    });
    setupKey (keyBwd,  [this] { browser.step (-1, true); });
    setupKey (keyFwd,  [this] { browser.step (+1, true); });
    setupKey (keyStop, [this] { proc.engine.stopPreview(); proc.engine.panic(); });
    setupKey (keyPlay, [this] { transportPlay(); }, [this] { return proc.engine.previewPlaying.load(); });

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
}

void RSU10Editor::setupKey (Key& k, std::function<void()> onClick, std::function<bool()> led, bool labelAbove)
{
    k.onClick = std::move (onClick);
    k.led = std::move (led);
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
    const bool customPlate = skin::asset ("bg_plate.png").isValid();

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
    g.setColour (juce::Colours::black.withAlpha (0.55f));
    g.setFont (juce::Font (juce::FontOptions (H * 0.042f, juce::Font::bold)).withExtraKerningFactor (0.12f));
    g.drawText ("RSU-10", badge.translated (0.0f, 1.0f).removeFromLeft (badge.getWidth() * 0.55f), juce::Justification::centredLeft);
    g.setColour (juce::Colours::white.withAlpha (0.85f));
    g.drawText ("RSU-10", badge.removeFromLeft (badge.getWidth() * 0.55f), juce::Justification::centredLeft);
    skin::drawSilk (g, juce::String ("RESTOMOD\nSAMPLING UNIT  v") + JucePlugin_VersionString, badge, juce::Justification::centredRight,
                    H * 0.014f, juce::Colours::black.withAlpha (0.6f));

    // Etichette sopra i tasti (serigrafia bianca dell'SU10)
    for (auto& l : labelled)
        if (l.above)
        {
            const auto kb = l.key->getBounds().toFloat();
            const float lh = H * 0.017f;
            auto lr = juce::Rectangle<float> (kb.getX() - 10.0f, kb.getY() - lh * 1.35f, kb.getWidth() + 20.0f, lh * 1.2f);
            skin::drawSilk (g, l.key->getButtonText(), lr, juce::Justification::centred, lh);
        }

    // Titoli di gruppo
    auto pk = rect ("pageKeys");
    skin::drawSilk (g, "PAGINA", pk.withY (pk.getY() - H * 0.055f).withHeight (H * 0.02f), juce::Justification::centredLeft, H * 0.015f, skin::col::silk.withAlpha (0.6f));
    auto tr = rect ("transport");
    juce::ignoreUnused (tr);
}

void RSU10Editor::paint (juce::Graphics& g)
{
    if (background.isNull())
        renderBackground();
    g.drawImage (background, getLocalBounds().toFloat());
}

void RSU10Editor::paintOverChildren (juce::Graphics& g)
{
    // LED moderni accanto alle etichette dei tasti che hanno uno stato
    for (auto& l : labelled)
        if (l.key->led != nullptr)
        {
            const auto kb = l.key->getBounds().toFloat();
            const float rad = juce::jmax (2.0f, getHeight() * 0.0035f);
            const auto c = l.key->style == skin::KeyStyle::Rec ? skin::col::recRed : skin::col::amber;
            if (l.key->style == skin::KeyStyle::Rec)
                skin::drawLed (g, { kb.getRight() + rad * 2.5f, kb.getY() + rad }, rad, l.key->led(), c);
            else
                skin::drawLed (g, { kb.getX() + rad, kb.getY() - getHeight() * 0.0105f }, rad, l.key->led(), c);
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
    keySave.setBounds (rect ("keySave"));
    keyLoad.setBounds (rect ("keyLoad"));

    // 8 manopole in fila, colonne uguali a quelle dei valori sullo schermo
    {
        auto k = rect ("knobs");
        const int w = k.getWidth() / 8;
        for (int i = 0; i < 8; ++i)
            knobs[size_t (i)]->setBounds (k.getX() + i * w, k.getY(), w, k.getHeight());
    }

    // tasti funzione: 2 colonne x 3 righe, spazio per l'etichetta sopra
    {
        auto f = rect ("fnKeys").toFloat();
        const float cw = f.getWidth() / 2.0f, rh = f.getHeight() / 3.0f;
        Key* grid[3][2] { { &keyHold, &keyExit }, { &keyRev, &keyScratch }, { &keyShift, &keyBank } };
        for (int row = 0; row < 3; ++row)
            for (int col = 0; col < 2; ++col)
            {
                auto cell = juce::Rectangle<float> (f.getX() + cw * col, f.getY() + rh * row, cw, rh);
                grid[row][col]->setBounds (cell.withTrimmedTop (rh * 0.38f).reduced (cw * 0.14f, rh * 0.06f).toNearestInt());
            }
    }
    {
        auto p = rect ("pageKeys").toFloat();
        const float w = p.getWidth() / 4.0f;
        for (int i = 0; i < 4; ++i)
            pageKeys[size_t (i)]->setBounds (juce::Rectangle<float> (p.getX() + w * i, p.getY(), w, p.getHeight()).reduced (w * 0.07f, 0.0f).toNearestInt());
    }
    keyEnter.setBounds (rect ("enter"));
    keyMinus.setBounds (rect ("minus"));
    keyPlus.setBounds (rect ("plus"));
    keyLeft.setBounds (rect ("left"));
    keyRight.setBounds (rect ("right"));

    // trasporto: REC tondo + 5 tasti in gomma
    {
        auto t = rect ("transport").toFloat();
        const float h = t.getHeight();
        auto recArea = t.removeFromLeft (h * 1.2f);
        keyRec.setBounds (juce::Rectangle<float> (h * 0.82f, h * 0.82f).withCentre (recArea.getCentre()).toNearestInt());
        Key* keys[] { &keyTop, &keyBwd, &keyStop, &keyPlay, &keyFwd };
        const float w = t.getWidth() / 5.0f;
        for (int i = 0; i < 5; ++i)
        {
            const float kw = (i == 2 || i == 3) ? w * 0.9f : w * 0.7f;   // STOP e PLAY più larghi, come sull'SU10
            keys[i]->setBounds (juce::Rectangle<float> (kw, h * 0.62f).withCentre ({ t.getX() + w * (i + 0.5f), t.getCentreY() + h * 0.1f }).toNearestInt());
        }
    }

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
    if (k == juce::KeyPress::deleteKey || k == juce::KeyPress::backspaceKey) { proc.samples.clearPad (proc.selectedPad); return true; }
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
        for (int k = 0; k < 8; ++k)
        {
            const int idx = page * 8 + k;
            knobs[size_t (k)]->setParam (idx < kNumPP ? idx : -1);
        }
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
