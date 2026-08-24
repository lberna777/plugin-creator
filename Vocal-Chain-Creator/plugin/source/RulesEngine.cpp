#include "RulesEngine.h"
#include <cmath>
#include <cstdio>
#include <algorithm>

namespace vf
{

//==============================================================================
// Valutatore di espressioni: le stesse funzioni del valutatore Python
// (clamp, min, max, abs, round, pos, neg, lerp). Nessun numero di dominio qui dentro.
//==============================================================================
namespace expr
{
    struct Parser
    {
        Parser (const juce::String& source, const std::map<juce::String, double>& variables)
            : text (source), vars (variables) {}

        const juce::String& text;
        const std::map<juce::String, double>& vars;
        int pos = 0;
        bool failed = false;

        void skipWs()                       { while (pos < text.length() && text[pos] == ' ') ++pos; }
        bool eat (const char* word)
        {
            skipWs();
            const juce::String w (word);
            if (text.substring (pos, pos + w.length()) != w) return false;
            const auto after = pos + w.length();
            if (juce::CharacterFunctions::isLetterOrDigit (text[after > text.length() - 1 ? text.length() - 1 : after])
                && juce::CharacterFunctions::isLetter (w[0]) && after < text.length())
                return false;                                    // evita di mangiare "orange" per "or"
            pos = after;
            return true;
        }

        double parse()                       { auto v = parseOr(); skipWs(); return v; }

        double parseOr()
        {
            auto left = parseAnd();
            while (true) { skipWs(); if (! eat ("or")) return left; auto r = parseAnd(); left = (left != 0.0 || r != 0.0) ? 1.0 : 0.0; }
        }
        double parseAnd()
        {
            auto left = parseCompare();
            while (true) { skipWs(); if (! eat ("and")) return left; auto r = parseCompare(); left = (left != 0.0 && r != 0.0) ? 1.0 : 0.0; }
        }
        double parseCompare()
        {
            auto left = parseSum();
            skipWs();
            static const char* ops[] = { ">=", "<=", "==", "!=", ">", "<" };
            for (auto* op : ops)
            {
                const juce::String o (op);
                if (text.substring (pos, pos + o.length()) == o)
                {
                    pos += o.length();
                    auto right = parseSum();
                    bool r = false;
                    if (o == ">=") r = left >= right; else if (o == "<=") r = left <= right;
                    else if (o == "==") r = left == right; else if (o == "!=") r = left != right;
                    else if (o == ">")  r = left >  right; else r = left < right;
                    return r ? 1.0 : 0.0;
                }
            }
            return left;
        }
        double parseSum()
        {
            auto left = parseProduct();
            while (true)
            {
                skipWs();
                if (pos < text.length() && text[pos] == '+') { ++pos; left += parseProduct(); }
                else if (pos < text.length() && text[pos] == '-') { ++pos; left -= parseProduct(); }
                else return left;
            }
        }
        double parseProduct()
        {
            auto left = parseUnary();
            while (true)
            {
                skipWs();
                if (pos < text.length() && text[pos] == '*') { ++pos; left *= parseUnary(); }
                else if (pos < text.length() && text[pos] == '/') { ++pos; left /= parseUnary(); }
                else return left;
            }
        }
        double parseUnary()
        {
            skipWs();
            if (pos < text.length() && text[pos] == '-') { ++pos; return -parseUnary(); }
            if (pos < text.length() && text[pos] == '+') { ++pos; return  parseUnary(); }
            if (eat ("not")) return parseUnary() != 0.0 ? 0.0 : 1.0;
            return parsePrimary();
        }
        double parsePrimary()
        {
            skipWs();
            if (pos >= text.length()) { failed = true; return 0.0; }

            if (text[pos] == '(')
            {
                ++pos;
                auto v = parseOr();
                skipWs();
                if (pos < text.length() && text[pos] == ')') ++pos; else failed = true;
                return v;
            }

            if (juce::CharacterFunctions::isDigit (text[pos]) || text[pos] == '.')
            {
                const int start = pos;
                while (pos < text.length() && (juce::CharacterFunctions::isDigit (text[pos]) || text[pos] == '.')) ++pos;
                return text.substring (start, pos).getDoubleValue();
            }

            const int start = pos;
            while (pos < text.length() && (juce::CharacterFunctions::isLetterOrDigit (text[pos]) || text[pos] == '_')) ++pos;
            const auto name = text.substring (start, pos);
            if (name.isEmpty()) { failed = true; ++pos; return 0.0; }

            skipWs();
            if (pos < text.length() && text[pos] == '(')                      // chiamata di funzione
            {
                ++pos;
                std::vector<double> args;
                skipWs();
                if (pos < text.length() && text[pos] == ')') ++pos;
                else
                {
                    while (true)
                    {
                        args.push_back (parseOr());
                        skipWs();
                        if (pos < text.length() && text[pos] == ',') { ++pos; continue; }
                        if (pos < text.length() && text[pos] == ')') { ++pos; break; }
                        failed = true; break;
                    }
                }
                return callFunction (name, args);
            }

            if (name == "true"  || name == "True")  return 1.0;
            if (name == "false" || name == "False") return 0.0;

            const auto it = vars.find (name);
            if (it != vars.end()) return it->second;
            failed = true;
            return 0.0;
        }

        double callFunction (const juce::String& name, const std::vector<double>& a)
        {
            auto arg = [&a] (size_t i) { return i < a.size() ? a[i] : 0.0; };
            if (name == "clamp") return juce::jlimit (arg (1), arg (2), arg (0));
            if (name == "min")   return std::min (arg (0), arg (1));
            if (name == "max")   return std::max (arg (0), arg (1));
            if (name == "abs")   return std::abs (arg (0));
            if (name == "round") return std::nearbyint (arg (0));
            if (name == "pos")   return std::max (0.0, arg (0));
            if (name == "neg")   return std::max (0.0, -arg (0));
            if (name == "lerp")  { auto t = juce::jlimit (0.0, 1.0, arg (2)); return arg (0) + (arg (1) - arg (0)) * t; }
            failed = true;
            return 0.0;
        }
    };

    static double evaluate (const juce::String& source, const std::map<juce::String, double>& vars, bool* ok = nullptr)
    {
        Parser p (source, vars);
        const auto value = p.parse();
        if (ok != nullptr) *ok = ! p.failed;
        jassert (! p.failed);
        return value;
    }
}

//==============================================================================
// Arrotondamento compatibile con Python (half-to-even sulla rappresentazione decimale):
// serve alla parità bit-per-bit con chain_compiler.py.
//==============================================================================
static double roundTo (double value, int decimals)
{
    char buffer[64];
    std::snprintf (buffer, sizeof (buffer), "%.*f", juce::jlimit (0, 15, decimals), value);
    return juce::String (buffer).getDoubleValue();
}

//==============================================================================
// Normalizzazione del testo: minuscolo, senza accenti, punteggiatura -> spazi.
//==============================================================================
static juce::String normalize (const juce::String& input)
{
    static const std::map<juce::juce_wchar, char> accents {
        { L'à', 'a' }, { L'á', 'a' }, { L'â', 'a' }, { L'ä', 'a' }, { L'ã', 'a' },
        { L'è', 'e' }, { L'é', 'e' }, { L'ê', 'e' }, { L'ë', 'e' },
        { L'ì', 'i' }, { L'í', 'i' }, { L'î', 'i' }, { L'ï', 'i' },
        { L'ò', 'o' }, { L'ó', 'o' }, { L'ô', 'o' }, { L'ö', 'o' }, { L'õ', 'o' },
        { L'ù', 'u' }, { L'ú', 'u' }, { L'û', 'u' }, { L'ü', 'u' },
        { L'ç', 'c' }, { L'ñ', 'n' }
    };

    juce::String out;
    for (auto c : input.toLowerCase())
    {
        const auto it = accents.find (c);
        const auto ch = (it != accents.end()) ? static_cast<juce::juce_wchar> (it->second) : c;

        if (ch == '&') { out << " & "; continue; }
        const bool keep = (ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9')
                          || ch == '/' || ch == '\'' || ch == ' ';
        out << (keep ? juce::String::charToString (ch) : juce::String (" "));
    }
    return out;
}

static juce::StringArray tokenize (const juce::String& text)
{
    juce::StringArray tokens;
    tokens.addTokens (normalize (text), " ", "");
    tokens.removeEmptyStrings();
    return tokens;
}

//==============================================================================
const Param* Module::find (juce::StringRef paramId) const
{
    for (auto& p : params) if (p.id == paramId) return &p;
    return nullptr;
}

const Param* Send::find (juce::StringRef settingId) const
{
    for (auto& s : settings) if (s.id == settingId) return &s;
    return nullptr;
}

const Module* Preset::find (juce::StringRef moduleId) const
{
    for (auto& m : modules) if (m.id == moduleId) return &m;
    return nullptr;
}

const Send* Preset::sendOfGroup (juce::StringRef group) const
{
    for (auto& s : sends) if (s.group == group) return &s;
    return nullptr;
}

//==============================================================================
bool RulesEngine::loadFromJson (const juce::String& jsonText)
{
    rules = juce::JSON::parse (jsonText);
    return isLoaded();
}

juce::String RulesEngine::getRulesVersion() const
{
    return rules.getProperty ("version", juce::String()).toString();
}

juce::StringArray RulesEngine::getProfileIds() const
{
    juce::StringArray ids;
    if (auto* obj = rules.getProperty ("source_profiles", {}).getDynamicObject())
        for (auto& prop : obj->getProperties())
            ids.add (prop.name.toString());
    return ids;
}

std::vector<RulesEngine::ArtistEntry> RulesEngine::getArtists() const
{
    std::vector<ArtistEntry> artists;
    if (auto* obj = rules.getProperty ("lexicon", {}).getProperty ("artists", {}).getDynamicObject())
        for (auto& prop : obj->getProperties())
        {
            ArtistEntry entry;
            entry.key   = prop.name.toString();
            entry.sound = prop.value.getProperty ("sound", {}).toString();

            if (auto* aliases = prop.value.getProperty ("aliases", {}).getArray())
                if (! aliases->isEmpty())
                    entry.prompt = (*aliases)[0].toString();
            if (entry.prompt.isEmpty()) entry.prompt = entry.key.replace ("_", " ");

            for (auto word : juce::StringArray::fromTokens (entry.prompt, " ", ""))
                entry.displayName << (entry.displayName.isEmpty() ? "" : " ")
                                  << word.substring (0, 1).toUpperCase() << word.substring (1);
            artists.push_back (std::move (entry));
        }
    return artists;
}

//==============================================================================
namespace
{
    struct AliasHit { juce::String key; juce::var definition; int index; juce::String phrase; };

    // alias (normalizzato) -> chiave + definizione, con match più lungo per primo
    struct AliasTable
    {
        std::map<juce::String, std::pair<juce::String, juce::var>> entries;
        int maxWords = 1;

        void build (const juce::var& section)
        {
            if (auto* obj = section.getDynamicObject())
            {
                for (auto& prop : obj->getProperties())
                {
                    const auto key = prop.name.toString();
                    auto aliases = prop.value.getProperty ("aliases", {});
                    juce::StringArray list;
                    if (auto* arr = aliases.getArray())
                        for (auto& a : *arr) list.add (a.toString());
                    else
                        list.add (key);

                    for (auto& alias : list)
                    {
                        const auto norm = normalize (alias).trim();
                        entries[norm] = { key, prop.value };
                        maxWords = std::max (maxWords, juce::StringArray::fromTokens (norm, " ", "").size());
                    }
                }
            }
        }

        std::vector<AliasHit> findIn (const juce::StringArray& tokens) const
        {
            std::vector<AliasHit> hits;
            int i = 0;
            while (i < tokens.size())
            {
                bool matched = false;
                for (int span = std::min (maxWords, tokens.size() - i); span > 0 && ! matched; --span)
                {
                    juce::String phrase;
                    for (int k = 0; k < span; ++k) phrase << (k > 0 ? " " : "") << tokens[i + k];
                    const auto it = entries.find (phrase);
                    if (it != entries.end())
                    {
                        hits.push_back ({ it->second.first, it->second.second, i, phrase });
                        i += span;
                        matched = true;
                    }
                }
                if (! matched) ++i;
            }
            return hits;
        }
    };

    juce::StringArray varToStringArray (const juce::var& v)
    {
        juce::StringArray out;
        if (auto* arr = v.getArray()) for (auto& item : *arr) out.add (item.toString());
        return out;
    }
}

//==============================================================================
Intent RulesEngine::parseIntent (const juce::String& prompt) const
{
    Intent intent;
    intent.prompt = prompt;

    const auto lexicon  = rules.getProperty ("lexicon", {});
    const auto defaults = rules.getProperty ("intent_defaults", {});
    const auto mods     = rules.getProperty ("modifiers", {});

    if (auto* axesObj = defaults.getProperty ("axes", {}).getDynamicObject())
        for (auto& prop : axesObj->getProperties())
            intent.axes[prop.name.toString()] = static_cast<double> (prop.value);

    intent.pitchClass = defaults.getProperty ("pitch_class", "mid").toString();
    intent.delivery   = defaults.getProperty ("delivery", "sung").toString();
    intent.loudnessTargetLufs = static_cast<double> (defaults.getProperty ("loudness_target_lufs", -14.0));

    const auto tokens = tokenize (prompt);
    std::vector<bool> consumed (static_cast<size_t> (tokens.size()), false);

    const auto negation    = varToStringArray (mods.getProperty ("negation", {}));
    const auto intensifier = varToStringArray (mods.getProperty ("intensifier", {}).getProperty ("terms", {}));
    const auto diminisher  = varToStringArray (mods.getProperty ("diminisher", {}).getProperty ("terms", {}));
    const double intensifierScale = static_cast<double> (mods.getProperty ("intensifier", {}).getProperty ("scale", 1.0));
    const double diminisherScale  = static_cast<double> (mods.getProperty ("diminisher", {}).getProperty ("scale", 1.0));
    const int window = static_cast<int> (mods.getProperty ("window", 3));

    auto modifierScale = [&] (int index)
    {
        double scale = 1.0;
        bool negated = false;
        for (int i = std::max (0, index - window); i < index; ++i)
        {
            const auto& word = tokens[i];
            if (negation.contains (word))         negated = true;
            else if (intensifier.contains (word)) scale *= intensifierScale;
            else if (diminisher.contains (word))  scale *= diminisherScale;
        }
        return negated ? -scale : scale;
    };

    auto record = [&] (const juce::String& phrase, int index, const juce::var& axesVar, double scale)
    {
        if (auto* obj = axesVar.getDynamicObject())
            for (auto& prop : obj->getProperties())
            {
                const auto axis = prop.name.toString();
                if (intent.axes.count (axis) > 0)
                    intent.axes[axis] += static_cast<double> (prop.value) * scale;
            }
        intent.matchedTerms.add (phrase);
        const int words = juce::StringArray::fromTokens (phrase, " ", "").size();
        for (int k = index; k < index + words && k < tokens.size(); ++k)
            consumed[static_cast<size_t> (k)] = true;
    };

    // 0) artista — il preset di partenza più forte: sposta tutto e può FORZARE le mandate
    AliasTable artists; artists.build (lexicon.getProperty ("artists", {}));
    bool haveArtist = false;
    for (auto& hit : artists.findIn (tokens))
    {
        const bool first = ! haveArtist;
        if (first)
        {
            haveArtist = true;
            intent.artist      = hit.key;
            intent.artistSound = hit.definition.getProperty ("sound", {}).toString();
            intent.genre       = hit.definition.getProperty ("genre", {}).toString();
            if (hit.definition.hasProperty ("loudness_target_lufs"))
                intent.loudnessTargetLufs = static_cast<double> (hit.definition.getProperty ("loudness_target_lufs", {}));
            if (hit.definition.hasProperty ("delivery"))    intent.delivery   = hit.definition.getProperty ("delivery", {}).toString();
            if (hit.definition.hasProperty ("pitch_class")) intent.pitchClass = hit.definition.getProperty ("pitch_class", {}).toString();

            if (auto* sendsObj = hit.definition.getProperty ("sends", {}).getDynamicObject())
                for (auto& prop : sendsObj->getProperties())
                    intent.forcedSends[prop.name.toString()] = prop.value.toString();

            if (auto* notes = hit.definition.getProperty ("production", {}).getDynamicObject())
                for (auto& prop : notes->getProperties())
                    intent.productionNotes.emplace_back (prop.name.toString(), prop.value.toString());
        }
        record (hit.phrase, hit.index, first ? hit.definition.getProperty ("axes", {}) : juce::var(), 1.0);
    }

    // 1) genere — solo il primo sposta gli assi, e solo se non c'è già un artista
    AliasTable genres;   genres.build (lexicon.getProperty ("genre", {}));
    bool haveGenre = haveArtist;
    for (auto& hit : genres.findIn (tokens))
    {
        const bool first = ! haveGenre;
        if (first)
        {
            haveGenre = true;
            intent.genre = hit.key;
            if (hit.definition.hasProperty ("loudness_target_lufs"))
                intent.loudnessTargetLufs = static_cast<double> (hit.definition.getProperty ("loudness_target_lufs", {}));
            if (hit.definition.hasProperty ("delivery"))
                intent.delivery = hit.definition.getProperty ("delivery", {}).toString();
        }
        record (hit.phrase, hit.index, first ? hit.definition.getProperty ("axes", {}) : juce::var(), 1.0);
    }

    // 2) esecuzione, registro, destinazione d'uso
    AliasTable deliveries; deliveries.build (lexicon.getProperty ("delivery", {}));
    for (auto& hit : deliveries.findIn (tokens)) { intent.delivery = hit.key; record (hit.phrase, hit.index, hit.definition.getProperty ("axes", {}), 1.0); }

    AliasTable pitches; pitches.build (lexicon.getProperty ("pitch", {}));
    for (auto& hit : pitches.findIn (tokens))    { intent.pitchClass = hit.key; record (hit.phrase, hit.index, hit.definition.getProperty ("axes", {}), 1.0); }

    AliasTable usages; usages.build (lexicon.getProperty ("usage", {}));
    for (auto& hit : usages.findIn (tokens))
    {
        if (hit.definition.hasProperty ("loudness_target_lufs"))
            intent.loudnessTargetLufs = static_cast<double> (hit.definition.getProperty ("loudness_target_lufs", {}));
        record (hit.phrase, hit.index, hit.definition.getProperty ("axes", {}), 1.0);
    }

    // 3) aggettivi, con negazioni e intensificatori
    AliasTable descriptors; descriptors.build (lexicon.getProperty ("descriptors", {}));
    for (auto& hit : descriptors.findIn (tokens))
        record (hit.phrase, hit.index, hit.definition.getProperty ("axes", {}), modifierScale (hit.index));

    // 4) clamp finale
    if (auto* rangesObj = rules.getProperty ("axis_ranges", {}).getDynamicObject())
        for (auto& prop : rangesObj->getProperties())
        {
            const auto axis = prop.name.toString();
            if (auto* range = prop.value.getArray())
                if (range->size() == 2 && intent.axes.count (axis) > 0)
                {
                    const auto lo = static_cast<double> ((*range)[0]), hi = static_cast<double> ((*range)[1]);
                    const auto raw = intent.axes[axis];
                    // Python: clamp() restituisce l'estremo così com'è (intero nel JSON) e round() su un
                    // intero resta intero. Solo i valori NON clampati restano float.
                    if (raw < lo)      { intent.axes[axis] = lo; intent.integerAxes.insert (axis); }
                    else if (raw > hi) { intent.axes[axis] = hi; intent.integerAxes.insert (axis); }
                    else               { intent.axes[axis] = roundTo (raw, 4); intent.integerAxes.erase (axis); }
                }
        }

    // termini non riconosciuti: si dichiarano, non si interpretano
    juce::StringArray stop (negation);
    stop.addArray (intensifier);
    stop.addArray (diminisher);
    stop.addTokens ("voce vocale una un e ed con per di da il la lo the a and with for in ma but molto che come su tipo stile", " ", "");
    for (int i = 0; i < tokens.size(); ++i)
        if (! consumed[static_cast<size_t> (i)] && ! stop.contains (tokens[i]) && tokens[i].length() > 2)
            intent.unknownTerms.add (tokens[i]);

    return intent;
}

//==============================================================================
double RulesEngine::Context::get (juce::StringRef name, bool& found) const
{
    const auto it = vars.find (name);
    found = (it != vars.end());
    return found ? it->second : 0.0;
}

RulesEngine::Context RulesEngine::buildContext (const Intent& intent, const juce::var& profile) const
{
    Context ctx;
    for (auto& axis : intent.axes)
    {
        ctx.vars[axis.first] = axis.second;
        if (intent.integerAxes.count (axis.first) == 0) ctx.floatVars.insert (axis.first);
    }

    // il tipo lo decide il JSON: 5 resta intero, 0.85 resta float — come in Python
    auto number = [&ctx] (const juce::String& name, const juce::var& value)
    {
        ctx.vars[name] = static_cast<double> (value);
        if (value.isDouble()) ctx.floatVars.insert (name);
    };

    const auto room  = profile.getProperty ("room", {});
    const auto mic   = profile.getProperty ("mic", {});
    const auto iface = profile.getProperty ("interface", {});
    auto arrayItem = [] (const juce::var& v, int i) -> double
    {
        if (auto* arr = v.getArray()) if (i < arr->size()) return static_cast<double> ((*arr)[i]);
        return 0.0;
    };

    number ("pitch_factor", rules.getProperty ("pitch_factors", {}).getProperty (intent.pitchClass, 1.0));
    ctx.vars["loudness_target"] = intent.loudnessTargetLufs;
    ctx.floatVars.insert ("loudness_target");        // in Python passa sempre da float()
    ctx.vars["treated"]       = static_cast<bool> (room.getProperty ("treated", false)) ? 1.0 : 0.0;
    number ("noise_floor", room.getProperty ("noise_floor_dbfs", 0));
    ctx.vars["mode1"]         = arrayItem (room.getProperty ("modes_hz", {}), 0);
    ctx.vars["mode2"]         = arrayItem (room.getProperty ("modes_hz", {}), 1);
    ctx.vars["mode3"]         = arrayItem (room.getProperty ("modes_hz", {}), 2);
    ctx.vars["boxy_lo"]       = arrayItem (room.getProperty ("boxy_band_hz", {}), 0);
    ctx.vars["boxy_hi"]       = arrayItem (room.getProperty ("boxy_band_hz", {}), 1);
    ctx.vars["flutter_lo"]    = arrayItem (room.getProperty ("flutter_band_hz", {}), 0);
    ctx.vars["flutter_hi"]    = arrayItem (room.getProperty ("flutter_band_hz", {}), 1);
    number ("proximity_boost", mic.getProperty ("proximity_boost_db", 0));
    number ("sib_center", mic.getProperty ("sibilance_center_hz", 0));
    number ("presence_peak", mic.getProperty ("presence_peak", 0));
    number ("target_peak", iface.getProperty ("target_peak_dbfs", 0));
    number ("assumed_peak", iface.getProperty ("assumed_peak_dbfs", 0));

    const juce::StringArray rhythmic { "trap", "rap", "edm", "metal" };
    ctx.vars["genre_is_rhythmic"] = rhythmic.contains (intent.genre) ? 1.0 : 0.0;

    for (auto* name : { "spoken", "rapped", "sung", "screamed", "whispered" })
        ctx.vars[juce::String ("delivery_is_") + name] = (intent.delivery == name) ? 1.0 : 0.0;

    ctx.text["pitch_class"] = intent.pitchClass;
    ctx.text["delivery"]    = intent.delivery;
    ctx.text["genre"]       = intent.genre;
    return ctx;
}

juce::String RulesEngine::formatWhy (const juce::String& tmpl, const Context& ctx) const
{
    juce::String out;
    int i = 0;
    while (i < tmpl.length())
    {
        if (tmpl[i] == '{')
        {
            const auto close = tmpl.indexOfChar (i, '}');
            if (close > i)
            {
                const auto name = tmpl.substring (i + 1, close);
                const auto textIt = ctx.text.find (name);
                if (textIt != ctx.text.end())
                {
                    out << textIt->second;
                }
                else
                {
                    bool found = false;
                    const auto value = ctx.get (name, found);
                    if (found)
                    {
                        const auto rounded = roundTo (value, 2);
                        const bool isFloat = ctx.floatVars.count (name) > 0;
                        if (! isFloat)
                        {
                            out << juce::String (static_cast<juce::int64> (rounded));
                        }
                        else if (rounded == std::floor (rounded))
                        {
                            out << juce::String (static_cast<juce::int64> (rounded)) << ".0";
                        }
                        else
                        {
                            char buffer[32];
                            std::snprintf (buffer, sizeof (buffer), "%.2f", rounded);
                            juce::String text (buffer);
                            while (text.endsWithChar ('0')) text = text.dropLastCharacters (1);
                            out << text;
                        }
                    }
                    else
                    {
                        out << tmpl.substring (i, close + 1);
                    }
                }
                i = close + 1;
                continue;
            }
        }
        out << juce::String::charToString (tmpl[i]);
        ++i;
    }
    return out;
}

Param RulesEngine::resolveParam (const juce::String& id, const juce::var& spec, const Context& ctx) const
{
    Param param;
    param.id   = id;
    param.unit = spec.getProperty ("unit", juce::String()).toString();

    if (spec.hasProperty ("value"))
    {
        param.value = spec.getProperty ("value", {});
    }
    else if (spec.hasProperty ("choices"))
    {
        if (auto* choices = spec.getProperty ("choices", {}).getArray())
            for (auto& choice : *choices)
            {
                const bool matches = ! choice.hasProperty ("when")
                                     || expr::evaluate (choice.getProperty ("when", {}).toString(), ctx.vars) != 0.0;
                if (matches) { param.value = choice.getProperty ("value", {}); break; }
            }
    }
    else
    {
        const auto raw = expr::evaluate (spec.getProperty ("expr", {}).toString(), ctx.vars);
        if (spec.hasProperty ("round"))
        {
            const int decimals = static_cast<int> (spec.getProperty ("round", 0));
            const auto rounded = roundTo (raw, decimals);
            param.value = (decimals == 0) ? juce::var (static_cast<int> (rounded)) : juce::var (rounded);
        }
        else
        {
            param.value = juce::var (roundTo (raw, 4));
        }
    }

    param.why = formatWhy (spec.getProperty ("why", juce::String()).toString(), ctx);
    return param;
}

//==============================================================================
Preset RulesEngine::compile (const juce::String& prompt, const juce::String& profileId) const
{
    Preset preset;
    preset.prompt       = prompt;
    preset.profileId    = profileId;
    preset.rulesVersion = getRulesVersion();

    const auto profile = rules.getProperty ("source_profiles", {}).getProperty (profileId, {});
    preset.intent = parseIntent (prompt);
    preset.artist      = preset.intent.artist;
    preset.artistSound = preset.intent.artistSound;
    const auto ctx = buildContext (preset.intent, profile);

    if (auto* chain = rules.getProperty ("chain", {}).getArray())
        for (auto& spec : *chain)
        {
            Module module;
            module.id      = spec.getProperty ("id", {}).toString();
            module.label   = spec.getProperty ("label", {}).toString();
            module.enabled = expr::evaluate (spec.getProperty ("enabled", {}).toString(), ctx.vars) != 0.0;

            const auto whyKey = module.enabled ? "why_on" : "why_off";
            auto whyTemplate  = spec.getProperty (whyKey, {}).toString();
            if (whyTemplate.isEmpty()) whyTemplate = spec.getProperty ("why_on", {}).toString();
            module.why = formatWhy (whyTemplate, ctx);

            if (module.enabled)
                if (auto* params = spec.getProperty ("params", {}).getDynamicObject())
                    for (auto& prop : params->getProperties())
                        module.params.push_back (resolveParam (prop.name.toString(), prop.value, ctx));

            preset.modules.push_back (std::move (module));
        }

    // mandate: una sola variante per gruppo, prima condizione vera in ordine di priorità
    if (auto* sends = rules.getProperty ("sends", {}).getArray())
    {
        std::vector<juce::var> ordered (sends->begin(), sends->end());
        std::stable_sort (ordered.begin(), ordered.end(), [] (const juce::var& a, const juce::var& b)
        {
            const auto ga = a.getProperty ("group", {}).toString(), gb = b.getProperty ("group", {}).toString();
            if (ga != gb) return ga < gb;
            return static_cast<int> (a.getProperty ("priority", 0)) < static_cast<int> (b.getProperty ("priority", 0));
        });

        juce::StringArray chosenGroups;
        for (auto& spec : ordered)
        {
            const auto group = spec.getProperty ("group", {}).toString();
            if (chosenGroups.contains (group)) continue;

            const auto forced = preset.intent.forcedSends.find (group);
            if (forced != preset.intent.forcedSends.end())
            {
                // il profilo artista sceglie la variante: le condizioni non si applicano
                if (spec.getProperty ("id", {}).toString() != forced->second) continue;
            }
            else if (expr::evaluate (spec.getProperty ("enabled", {}).toString(), ctx.vars) == 0.0)
            {
                continue;
            }

            chosenGroups.add (group);
            if (static_cast<bool> (spec.getProperty ("skip", false)))
                continue;                                    // il gruppo si chiude senza mandata

            Send send;
            send.id          = spec.getProperty ("id", {}).toString();
            send.group       = group;
            send.label       = spec.getProperty ("label", {}).toString();
            send.pluginLogic = spec.getProperty ("plugin_logic", {}).toString();
            send.why = formatWhy (spec.getProperty ("why", {}).toString(), ctx);
            if (forced != preset.intent.forcedSends.end())
            {
                auto artistName = preset.intent.artist.replace ("_", " ");
                juce::String titled;
                for (auto word : juce::StringArray::fromTokens (artistName, " ", ""))
                    titled << (titled.isEmpty() ? "" : " ") << word.substring (0, 1).toUpperCase() << word.substring (1);
                send.why = "Scelta dal profilo " + titled + ". " + send.why;
            }

            if (auto* settings = spec.getProperty ("settings", {}).getDynamicObject())
                for (auto& prop : settings->getProperties())
                    send.settings.push_back (resolveParam (prop.name.toString(), prop.value, ctx));

            preset.sends.push_back (std::move (send));
        }
    }

    preset.loudnessTargetLufs = preset.intent.loudnessTargetLufs;
    preset.truePeakCeilingDb  = static_cast<double> (rules.getProperty ("io_defaults", {})
                                                          .getProperty ("true_peak_ceiling_dbfs", -1.0));
    preset.declaredLatencyMs  = static_cast<double> (rules.getProperty ("io_defaults", {})
                                                          .getProperty ("declared_latency_ms", 0.0));
    preset.stereoIsDualMono   = static_cast<bool> (profile.getProperty ("io", {})
                                                          .getProperty ("stereo_is_dual_mono", false));

    if (preset.intent.matchedTerms.isEmpty())
        preset.warnings.add ("Nessun termine riconosciuto: applicato il profilo neutro dichiarato in rules.json.");
    if (! preset.intent.unknownTerms.isEmpty())
        preset.warnings.add ("Termini non nel vocabolario (ignorati): " + preset.intent.unknownTerms.joinIntoString (", "));
    if (! static_cast<bool> (profile.getProperty ("room", {}).getProperty ("treated", false)))
        preset.warnings.add ("Stanza non trattata: le riflessioni precoci non sono correggibili a valle. "
                             "Avvicinati al microfono e metti qualcosa di morbido dietro di te.");

    return preset;
}

} // namespace vf
