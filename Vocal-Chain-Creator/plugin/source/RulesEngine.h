#pragma once
#include <juce_core/juce_core.h>
#include <map>
#include <utility>
#include <set>
#include <vector>

/*  VOCAL FORGE — motore di regole.

    È la traduzione C++ di tools/chain_compiler.py e legge LO STESSO tools/data/rules.json
    (imbarcato come BinaryData). La parità non è un impegno: è una conseguenza del fatto che
    i numeri stanno tutti nel file di regole, e qui non ce n'è nemmeno uno.

    Verificata da `vocalforge_parity`, che confronta l'output di questa classe con i preset
    generati da Python in examples/ (vedi CHECKLIST.md).
*/
namespace vf
{

struct Param
{
    juce::String id, unit, why;
    juce::var    value;                       // double, bool o String
    double       asDouble() const  { return static_cast<double> (value); }
    bool         asBool()   const  { return static_cast<bool> (value); }
};

struct Module
{
    juce::String id, label, why;
    bool enabled = false;
    std::vector<Param> params;
    const Param* find (juce::StringRef paramId) const;
};

struct Send
{
    juce::String id, group, label, pluginLogic, why;
    std::vector<Param> settings;
    const Param* find (juce::StringRef settingId) const;
};

struct Intent
{
    juce::String prompt, genre, pitchClass, delivery, languageHint;
    juce::String artist, artistSound;
    std::map<juce::String, juce::String> forcedSends;                 // gruppo → id della variante
    std::vector<std::pair<juce::String, juce::String>> productionNotes;
    std::map<juce::String, double> axes;
    double loudnessTargetLufs = 0.0;
    juce::StringArray unknownTerms, matchedTerms;
    std::set<juce::String> integerAxes;      // assi finiti su un estremo intero del range
};

struct Preset
{
    juce::String prompt, profileId, rulesVersion, artist, artistSound;
    Intent intent;
    std::vector<Module> modules;
    std::vector<Send>   sends;
    juce::StringArray   warnings;
    double loudnessTargetLufs = 0.0, truePeakCeilingDb = 0.0, declaredLatencyMs = 0.0;
    bool   stereoIsDualMono = true;

    const Module* find (juce::StringRef moduleId) const;
    const Send*   sendOfGroup (juce::StringRef group) const;
};

class RulesEngine
{
public:
    RulesEngine() = default;

    /** Carica le regole da testo JSON. false se il file non è valido. */
    bool loadFromJson (const juce::String& jsonText);

    /** Carica le regole imbarcate in BinaryData. */
    bool loadEmbedded();

    bool isLoaded() const noexcept          { return rules.isObject(); }
    juce::String getRulesVersion() const;

    /** Il profilo di sorgente di default del progetto (stanza non trattata + Focusrite + Scarlett). */
    static const char* defaultProfileId()   { return "untreated_room_focusrite_scarlett"; }

    juce::StringArray getProfileIds() const;

    /** Valuta un'espressione con un contesto dato: serve al confronto di semantica col Python. */
    static double evaluateExpression (const juce::String& expression,
                                      const std::map<juce::String, double>& variables, bool* ok = nullptr);

    struct ArtistEntry { juce::String key, displayName, prompt, sound; };
    /** I profili artista disponibili, per il menù della UI. */
    std::vector<ArtistEntry> getArtists() const;

    /** testo → assi. Deterministico: stesso testo, stesso intento. */
    Intent parseIntent (const juce::String& prompt) const;

    /** testo + profilo → preset completo di catena e mandate. */
    Preset compile (const juce::String& prompt,
                    const juce::String& profileId = defaultProfileId()) const;

private:
    juce::var rules;
    mutable juce::StringArray failedExpressions;   // espressioni che non si sono valutate: diventano avvisi

    struct Context
    {
        std::map<juce::String, double> vars;
        std::set<juce::String> floatVars;        // quali variabili Python tratta come float
        std::map<juce::String, juce::String> text;
        double get (juce::StringRef name, bool& found) const;
    };

    Context buildContext (const Intent&, const juce::var& profile) const;
    Param   resolveParam (const juce::String& id, const juce::var& spec, const Context&) const;
    juce::String formatWhy (const juce::String& tmpl, const Context&) const;
};

} // namespace vf
