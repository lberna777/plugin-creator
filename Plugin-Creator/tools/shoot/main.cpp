// Render headless dell'editor → PNG del VERO motore JUCE (non un mock).
// Loop di feedback visivo: build → ./shoot ui_shot.png → confronta col riferimento.
//
// Wiring (nel CMakeLists del plugin generato):
//   juce_add_console_app(shoot ...)
//   target_link_libraries(shoot PRIVATE <PluginSharedCode> NemoAssets juce::juce_gui_extra ...)
//   target_compile_definitions(shoot PRIVATE JUCE_STANDALONE_APPLICATION=1)
//
// Opzionale: imposta stati estremi per verificare switch/LED/meter prima del giro in DAW.

#include <JuceHeader.h>
#include "PluginProcessor.h"

int main(int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI guiInit;

    const juce::String outPath = (argc > 1) ? juce::String(argv[1]) : "ui_shot.png";

    // Sostituisci col nome reale della classe processor del plugin generato:
    PLUGIN_PROCESSOR_CLASS proc;

    // (opzionale) forza stati per lo screenshot:
    // proc.apvts.getParameter("phase")->setValueNotifyingHost(1.0f);

    std::unique_ptr<juce::AudioProcessorEditor> ed(proc.createEditor());
    if (ed == nullptr) { std::cerr << "nessun editor\n"; return 1; }

    const int w = ed->getWidth() > 0 ? ed->getWidth() : 600;
    const int h = ed->getHeight() > 0 ? ed->getHeight() : 400;
    ed->setSize(w, h);

    juce::Image img(juce::Image::ARGB, w, h, true);
    {
        juce::Graphics g(img);
        ed->paintEntireComponent(g, true);
    }

    juce::File outFile(juce::File::getCurrentWorkingDirectory().getChildFile(outPath));
    outFile.deleteFile();
    juce::FileOutputStream os(outFile);
    if (os.openedOk())
    {
        juce::PNGImageFormat png;
        png.writeImageToStream(img, os);
        std::cout << "render salvato: " << outFile.getFullPathName() << " (" << w << "x" << h << ")\n";
    }
    return 0;
}
