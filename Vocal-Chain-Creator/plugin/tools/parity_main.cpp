/*  vocalforge_parity — il test di parità imposto da CHECKLIST.md.

    Compila i prompt di examples/ con il motore C++ e li confronta con i preset generati
    da tools/chain_compiler.py. Interi: uguaglianza esatta. Float: tolleranza 1e-6.

    Uso:  vocalforge_parity <rules.json> <examples-dir>
*/
#include "../source/RulesEngine.h"
#include <iostream>

namespace
{
    int failures = 0, checks = 0;

    void fail (const juce::String& message)
    {
        ++failures;
        std::cout << "  FAIL  " << message << std::endl;
    }

    bool sameValue (const juce::var& expected, const juce::var& actual, juce::String& detail)
    {
        ++checks;
        if (expected.isString() || actual.isString())
        {
            if (expected.toString() == actual.toString()) return true;
            detail = "\"" + expected.toString() + "\" != \"" + actual.toString() + "\"";
            return false;
        }
        if (expected.isBool() || actual.isBool())
        {
            if (static_cast<bool> (expected) == static_cast<bool> (actual)) return true;
            detail = juce::String (static_cast<bool> (expected) ? "true" : "false") + " != "
                   + juce::String (static_cast<bool> (actual) ? "true" : "false");
            return false;
        }
        const auto a = static_cast<double> (expected), b = static_cast<double> (actual);
        if (expected.isInt() || expected.isInt64())
        {
            if (a == b) return true;
            detail = juce::String (a) + " != " + juce::String (b) + " (intero)";
            return false;
        }
        if (std::abs (a - b) <= 1.0e-6) return true;
        detail = juce::String (a, 6) + " != " + juce::String (b, 6);
        return false;
    }

    void comparePreset (const juce::File& file, const vf::RulesEngine& engine)
    {
        const auto expected = juce::JSON::parse (file.loadFileAsString());
        const auto prompt   = expected.getProperty ("prompt", {}).toString();
        const auto profile  = expected.getProperty ("source_profile", {}).getProperty ("id", {}).toString();
        const auto actual   = engine.compile (prompt, profile);

        std::cout << "· \"" << prompt << "\"" << std::endl;

        // intento
        if (auto* axes = expected.getProperty ("intent", {}).getProperty ("axes", {}).getDynamicObject())
            for (auto& prop : axes->getProperties())
            {
                const auto axis = prop.name.toString();
                juce::String detail;
                const auto it = actual.intent.axes.find (axis);
                if (it == actual.intent.axes.end()) { fail ("asse mancante: " + axis); continue; }
                if (! sameValue (prop.value, juce::var (it->second), detail))
                    fail ("asse " + axis + ": " + detail);
            }

        if (expected.getProperty ("intent", {}).getProperty ("genre", {}).toString() != actual.intent.genre)
            fail ("genere: " + expected.getProperty ("intent", {}).getProperty ("genre", {}).toString()
                  + " != " + actual.intent.genre);
        if (expected.getProperty ("artist", {}).toString() != actual.artist)
            fail ("artista: " + expected.getProperty ("artist", {}).toString() + " != " + actual.artist);
        if (auto* notes = expected.getProperty ("production_notes", {}).getArray())
        {
            if (static_cast<size_t> (notes->size()) != actual.intent.productionNotes.size())
                fail ("numero di note di produzione diverso");
            else
                for (int i = 0; i < notes->size(); ++i)
                    if ((*notes)[i].getProperty ("text", {}).toString() != actual.intent.productionNotes[static_cast<size_t> (i)].second)
                        fail ("nota di produzione diversa: " + (*notes)[i].getProperty ("id", {}).toString());
        }
        if (expected.getProperty ("intent", {}).getProperty ("delivery", {}).toString() != actual.intent.delivery)
            fail ("esecuzione diversa");
        if (expected.getProperty ("intent", {}).getProperty ("pitch_class", {}).toString() != actual.intent.pitchClass)
            fail ("registro diverso");

        // moduli
        auto* modules = expected.getProperty ("modules", {}).getArray();
        if (modules == nullptr) { fail ("nessun modulo nel riferimento"); return; }
        if (static_cast<size_t> (modules->size()) != actual.modules.size())
        { fail ("numero di moduli diverso"); return; }

        for (int i = 0; i < modules->size(); ++i)
        {
            const auto& ref = (*modules)[i];
            const auto& got = actual.modules[static_cast<size_t> (i)];
            const auto id = ref.getProperty ("id", {}).toString();

            if (id != got.id)                                     { fail ("ordine dei moduli diverso: " + id); continue; }
            if (static_cast<bool> (ref.getProperty ("enabled", false)) != got.enabled)
            { fail (id + ": enabled diverso"); continue; }
            if (ref.getProperty ("why", {}).toString() != got.why)
                fail (id + ": motivazione diversa\n         py: " + ref.getProperty ("why", {}).toString()
                      + "\n         cpp: " + got.why);

            if (auto* params = ref.getProperty ("params", {}).getDynamicObject())
                for (auto& prop : params->getProperties())
                {
                    const auto pid = prop.name.toString();
                    const auto* mine = got.find (pid);
                    if (mine == nullptr) { fail (id + "." + pid + ": parametro mancante"); continue; }

                    juce::String detail;
                    if (! sameValue (prop.value.getProperty ("value", {}), mine->value, detail))
                        fail (id + "." + pid + ": " + detail);
                    if (prop.value.getProperty ("why", {}).toString() != mine->why)
                        fail (id + "." + pid + ": motivazione diversa\n         py: "
                              + prop.value.getProperty ("why", {}).toString() + "\n         cpp: " + mine->why);
                }
        }

        // mandate
        auto* sends = expected.getProperty ("sends", {}).getArray();
        const size_t expectedSends = sends != nullptr ? static_cast<size_t> (sends->size()) : 0;
        if (expectedSends != actual.sends.size())
        { fail ("numero di mandate diverso: " + juce::String ((int) expectedSends) + " != " + juce::String ((int) actual.sends.size())); return; }

        for (size_t i = 0; i < actual.sends.size(); ++i)
        {
            const auto& ref = (*sends)[static_cast<int> (i)];
            const auto& got = actual.sends[i];
            if (ref.getProperty ("id", {}).toString() != got.id)
            { fail ("mandata diversa: " + ref.getProperty ("id", {}).toString() + " != " + got.id); continue; }

            if (auto* settings = ref.getProperty ("settings", {}).getDynamicObject())
                for (auto& prop : settings->getProperties())
                {
                    const auto sid = prop.name.toString();
                    const auto* mine = got.find (sid);
                    if (mine == nullptr) { fail (got.id + "." + sid + ": impostazione mancante"); continue; }
                    juce::String detail;
                    if (! sameValue (prop.value, mine->value, detail))
                        fail (got.id + "." + sid + ": " + detail);
                }
        }

        // avvisi
        if (auto* warnings = expected.getProperty ("warnings", {}).getArray())
        {
            if (static_cast<size_t> (warnings->size()) != static_cast<size_t> (actual.warnings.size()))
                fail ("numero di avvisi diverso");
            else
                for (int i = 0; i < warnings->size(); ++i)
                    if ((*warnings)[i].toString() != actual.warnings[i])
                        fail ("avviso diverso: " + actual.warnings[i]);
        }
    }
}

int main (int argc, char** argv)
{
    if (argc < 3)
    {
        std::cout << "uso: vocalforge_parity <rules.json> <examples-dir>" << std::endl;
        return 2;
    }

    const juce::File rulesFile { juce::String (argv[1]) };
    const juce::File examplesDir { juce::String (argv[2]) };

    vf::RulesEngine engine;
    if (! engine.loadFromJson (rulesFile.loadFileAsString()))
    {
        std::cout << "regole non caricate: " << rulesFile.getFullPathName() << std::endl;
        return 2;
    }

    std::cout << "VOCAL FORGE — parità C++ ↔ Python (regole v" << engine.getRulesVersion() << ")" << std::endl;

    int files = 0;
    for (const auto& entry : juce::RangedDirectoryIterator (examplesDir, false, "*.preset.json"))
    {
        ++files;
        comparePreset (entry.getFile(), engine);
    }

    std::cout << std::endl << (failures == 0 ? "OK   " : "FALLITO   ")
              << files << " preset, " << checks << " valori confrontati, "
              << failures << " differenze" << std::endl;
    return failures == 0 ? 0 : 1;
}
