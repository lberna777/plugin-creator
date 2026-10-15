#include "MainComponent.h"

#include <juce_gui_extra/juce_gui_extra.h>

class MikroBridgeApp final : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override    { return JUCE_APPLICATION_NAME_STRING; }
    const juce::String getApplicationVersion() override { return JUCE_APPLICATION_VERSION_STRING; }
    bool moreThanOneInstanceAllowed() override           { return false; }   // la Mikro è una sola

    void initialise (const juce::String&) override
    {
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
