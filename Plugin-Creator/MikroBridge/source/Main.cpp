#include "MainComponent.h"

#include <juce_gui_extra/juce_gui_extra.h>

class MikroBridgeApp final : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override    { return JUCE_APPLICATION_NAME_STRING; }
    const juce::String getApplicationVersion() override { return JUCE_APPLICATION_VERSION_STRING; }
    bool moreThanOneInstanceAllowed() override           { return false; }   // la Mikro è una sola

    void initialise (const juce::String& args) override
    {
        // --dump-display <cartella>: salva le schermate del display come PNG (per controllarle senza Mikro)
        if (args.contains ("--dump-display"))
        {
            dumpDisplay (juce::File (args.fromFirstOccurrenceOf ("--dump-display", false, false).trim().unquoted()));
            quit();
            return;
        }

        juce::PropertiesFile::Options o;
        o.applicationName = "Mikro Bridge";
        o.filenameSuffix = ".settings";
        o.folderName = "Mikro Bridge";
        o.osxLibrarySubFolder = "Application Support";
        props = std::make_unique<juce::PropertiesFile> (o);

        bridge = std::make_unique<Bridge>();
        window = std::make_unique<Window> (getApplicationName() + " " + getApplicationVersion(),
                                           new MainComponent (*bridge, *props));
    }

    void shutdown() override
    {
        window = nullptr;   // salva le impostazioni
        bridge = nullptr;   // spegne LED e display, chiude le porte MIDI
        props = nullptr;
    }

    void systemRequestedQuit() override { quit(); }

private:
    static void dumpDisplay (const juce::File& dir)
    {
        dir.createDirectory();
        mikro::DisplayRenderer r;
        mikro::Display d;
        auto save = [&] (const juce::String& name)
        {
            juce::Image img (juce::Image::ARGB, 4 * mikro::kDisplayW + 24, 4 * mikro::kDisplayH + 24, true);
            {
                juce::Graphics g (img);
                g.fillAll (juce::Colour (0xff16171a));
                mikro::DisplayRenderer::paintPreview (g, img.getBounds().toFloat(), d);
            }
            juce::FileOutputStream out (dir.getChildFile (name + ".png"));
            out.setPosition (0);
            out.truncate();
            juce::PNGImageFormat().writeImageToStream (img, out);
        };

        mikro::DisplayState s;
        s.lowNote = 36; s.highNote = 51; s.lastNote = 38; s.lastVelocity = 104; s.lastPad = 2; s.held = 1u << 2;
        r.render (s, d); save ("1-drums");

        s = {};
        s.mode = 1; s.root = 9; s.scale = 2; s.lowNote = 57; s.highNote = 81; s.lastNote = 64; s.lastVelocity = 88; s.lastPad = 4;
        r.render (s, d); save ("2-keys");

        s = {};
        s.mode = 2; s.root = 0; s.scale = 1; s.lowNote = 48; s.highNote = 72; s.lastPad = 13; s.lastNote = 57; s.lastVelocity = 90;
        s.held = 1u << 13;
        r.render (s, d); save ("3-chords");

        for (int k = 0; k < 4; ++k)
        {
            r.renderSplash (mikro::DisplayRenderer::kSplashMs * (0.2 + 0.2 * k), d);
            save ("0-splash-" + juce::String (k));
        }
    }

    struct Window final : juce::DocumentWindow
    {
        Window (const juce::String& name, juce::Component* content)
            : DocumentWindow (name, juce::Colour (0xff16171a), DocumentWindow::closeButton | DocumentWindow::minimiseButton)
        {
            setUsingNativeTitleBar (true);
            setContentOwned (content, true);
            setResizable (false, false);
            centreWithSize (getWidth(), getHeight());
            setVisible (true);
        }
        void closeButtonPressed() override { juce::JUCEApplication::getInstance()->systemRequestedQuit(); }
    };

    std::unique_ptr<juce::PropertiesFile> props;
    std::unique_ptr<Bridge> bridge;
    std::unique_ptr<Window> window;
};

START_JUCE_APPLICATION (MikroBridgeApp)
