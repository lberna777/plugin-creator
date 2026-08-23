#pragma once
#include <JuceHeader.h>

// Layout in coordinate NORMALIZZATE (0..1) caricate da JSON → niente numeri magici nel codice.
// Lezione NEMO: pixel assoluti legati a uno sfondo mutevole = ribasamenti manuali a ripetizione.
//
// Uso:
//   Layout layout (BinaryData::layout_json, BinaryData::layout_jsonSize);   // una volta
//   knob.setBounds (layout.rect ("knobs/hpFreq", getLocalBounds()));        // in resized()
//   auto col = layout.rect ("meter/outCol", meterArea);
//
// In DEBUG puoi caricarlo da file su disco e ricaricarlo a caldo, così spostare un
// controllo NON richiede ricompilare:
//   Layout layout (juce::File ("…/layout.json"));
//
// Formato JSON: ogni nodo è "gruppo/nome" con campi cx,cy,w,h (oppure x0,y0,x1,y1),
// tutti in frazioni 0..1 della superficie di riferimento passata a rect()/point().
class Layout
{
public:
    Layout(const char* jsonData, int size)         { tree = juce::JSON::parse(juce::String::fromUTF8(jsonData, size)); }
    explicit Layout(const juce::File& f)           { tree = juce::JSON::parse(f); }

    // Rettangolo da cx,cy,w,h (centro+dimensioni) oppure x0,y0,x1,y1 (angoli), in frazioni.
    juce::Rectangle<int> rect(const juce::String& path, juce::Rectangle<int> area) const
    {
        auto* o = node(path);
        if (o == nullptr) return {};
        const float W = (float) area.getWidth(), H = (float) area.getHeight();
        const float X = (float) area.getX(),     Y = (float) area.getY();

        if (o->hasProperty("w") || o->hasProperty("h"))
        {
            const float cx = get(o, "cx"), cy = get(o, "cy");
            const float w  = get(o, "w"),  h  = get(o, "h", get(o, "w"));
            return juce::Rectangle<float>(X + (cx - w * 0.5f) * W, Y + (cy - h * 0.5f) * H,
                                          w * W, h * H).toNearestInt();
        }
        const float x0 = get(o, "x0"), y0 = get(o, "y0"), x1 = get(o, "x1"), y1 = get(o, "y1");
        return juce::Rectangle<float>(X + x0 * W, Y + y0 * H, (x1 - x0) * W, (y1 - y0) * H).toNearestInt();
    }

    juce::Point<int> point(const juce::String& path, juce::Rectangle<int> area) const
    {
        auto* o = node(path);
        if (o == nullptr) return {};
        return { area.getX() + juce::roundToInt(get(o, "cx") * area.getWidth()),
                 area.getY() + juce::roundToInt(get(o, "cy") * area.getHeight()) };
    }

    float number(const juce::String& path, float fallback = 0.0f) const
    {
        auto v = resolve(path);
        return v.isVoid() ? fallback : (float) v;
    }

    bool isValid() const { return tree.isObject(); }

private:
    juce::var tree;

    juce::var resolve(const juce::String& path) const
    {
        juce::var cur = tree;
        for (auto& key : juce::StringArray::fromTokens(path, "/", ""))
        {
            if (auto* o = cur.getDynamicObject())
                cur = o->getProperty(key);
            else
                return {};
        }
        return cur;
    }
    juce::DynamicObject* node(const juce::String& path) const { return resolve(path).getDynamicObject(); }
    static float get(juce::DynamicObject* o, const juce::String& k, float dflt = 0.0f)
    {
        return o->hasProperty(k) ? (float) o->getProperty(k) : dflt;
    }
};
