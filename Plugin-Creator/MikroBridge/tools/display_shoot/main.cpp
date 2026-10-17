// Render del display della Mikro in tutte le schermate, casi peggiori compresi, in un'unica PNG.
// Uso: ./MikroDisplayShoot foglio.png   (serve per vedere sovrapposizioni senza la Mikro in mano)
#include "DisplayRenderer.h"

#include <juce_gui_basics/juce_gui_basics.h>

using namespace mikro;

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    const juce::File out (argc > 1 ? juce::File::getCurrentWorkingDirectory().getChildFile (argv[1])
                                   : juce::File::getCurrentWorkingDirectory().getChildFile ("display_sheet.png"));

    struct Case { const char* title; DisplayState s; };
    std::vector<Case> cases;
    auto add = [&] (const char* t, std::function<void (DisplayState&)> f) { DisplayState s; f (s); cases.push_back ({ t, s }); };

    add ("DRUMS a riposo", [] (DisplayState& s) { s.mode = 0; s.lowNote = 36; s.highNote = 51; });
    add ("DRUMS colpo forte, AT ON, CH 16", [] (DisplayState& s) { s.mode = 0; s.lastNote = 99; s.lastVelocity = 127; s.lastPad = 15;
         s.lowNote = 84; s.highNote = 99; s.aftertouch = true; s.channel = 16; s.held = 0xA5A5; });
    add ("KEYS C# Pentatonica min.", [] (DisplayState& s) { s.mode = 1; s.root = 1; s.scale = 11; s.lastNote = 73; s.lastVelocity = 90;
         s.lowNote = 49; s.highNote = 80; });
    add ("KEYS G# Frigia dominante + ARP", [] (DisplayState& s) { s.mode = 1; s.root = 8; s.scale = 13; s.lastNote = 104; s.lastVelocity = 127;
         s.arp = true; s.lowNote = 56; s.highNote = 110; });
    add ("KEYS A# Minore armonica ARMONIA", [] (DisplayState& s) { s.mode = 1; s.root = 10; s.scale = 3; s.harmony = true; s.lastNote = 70;
         s.lastVelocity = 64; s.lowNote = 58; s.highNote = 106; });
    add ("CHORDS C maggiore, pad 15 (Bm7b5)", [] (DisplayState& s) { s.mode = 2; s.scale = 1; s.lastPad = 14; s.lastNote = 59; s.lastVelocity = 100; });
    add ("CHORDS G# min. armonica, pad 11 (maj7#5)", [] (DisplayState& s) { s.mode = 2; s.root = 8; s.scale = 3; s.lastPad = 10; s.lastNote = 59;
         s.lastVelocity = 127; s.arp = true; s.held = 0x0401; });
    add ("CHORDS nessun accordo ancora", [] (DisplayState& s) { s.mode = 2; s.scale = 2; s.root = 9; });
    add ("MANOPOLA Filtro 127", [] (DisplayState& s) { s.mode = 1; s.scale = 1; s.knobShown = true; s.knobFx = 1; s.knobValue = 127; });
    add ("MANOPOLA Pitch -64", [] (DisplayState& s) { s.mode = 2; s.scale = 1; s.knobShown = true; s.knobFx = 11; s.knobValue = 0; s.arp = true; });
    add ("MANOPOLA FL", [] (DisplayState& s) { s.mode = 0; s.knobShown = true; s.knobFx = 0; });
    add ("MANOPOLA ARP velocita' 1/16T", [] (DisplayState& s) { s.mode = 1; s.arp = true; s.knobShown = true; s.knobFx = 12; s.knobValue = 4; });

    constexpr int scale = 5, pad = 16, cols = 3;
    const int cellW = kDisplayW * scale + pad * 2, cellH = kDisplayH * scale + pad * 2 + 22;
    const int rows = (int (cases.size()) + cols - 1) / cols;
    juce::Image sheet (juce::Image::RGB, cellW * cols, cellH * rows, true);
    juce::Graphics g (sheet);
    g.fillAll (juce::Colour (0xff202428));

    DisplayRenderer r;
    for (size_t i = 0; i < cases.size(); ++i)
    {
        Display d;
        r.render (cases[i].s, d);
        const int x = int (i % cols) * cellW, y = int (i / cols) * cellH;
        g.setColour (juce::Colours::white);
        g.setFont (15.0f);
        g.drawText (cases[i].title, x + pad, y + 2, cellW - pad * 2, 20, juce::Justification::centredLeft);
        DisplayRenderer::paintPreview (g, juce::Rectangle<float> (float (x + pad), float (y + 24), float (kDisplayW * scale), float (kDisplayH * scale)), d);
    }
    out.deleteFile();
    juce::FileOutputStream os (out);
    juce::PNGImageFormat().writeImageToStream (sheet, os);
    std::printf ("foglio: %s\n", out.getFullPathName().toRawUTF8());
    return 0;
}
