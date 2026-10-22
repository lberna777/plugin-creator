#pragma once
// Confronto mix ↔ riferimento → al massimo 5 frasi in italiano chiaro, ordinate per gravità,
// ognuna con un consiglio pratico. Gira sul thread della UI (alloca stringhe: MAI sul thread audio).
// Header senza JUCE. Stringhe UTF-8.
#include "MixAnalyzer.h"
#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

namespace mc
{
struct Finding
{
    double severity = 0;   // più alto = più importante
    std::string text;
};

struct CompareSettings
{
    double zoneThresholdDb = 1.5;   // sotto questa differenza di zona: rumore di misura, nessuna frase
    double plrThreshold    = 1.5;
    double monoBassMinCorr = 0.9;
    double widthThresholdDb = 4.0;
    size_t maxFindings     = 5;
};

namespace detail
{
    inline std::string fmt (const char* f, double a)
    {
        char buf[64];
        std::snprintf (buf, sizeof (buf), f, a);
        std::string s (buf);
        // segno meno tipografico, come nei testi della UI
        for (size_t p = 0; (p = s.find ('-', p)) != std::string::npos; )
            s.replace (p, 1, "\xE2\x88\x92"), p += 3;
        return s;
    }
    inline std::string fmt2 (const char* f, double a, double b)
    {
        char buf[96];
        std::snprintf (buf, sizeof (buf), f, a, b);
        std::string s (buf);
        for (size_t p = 0; (p = s.find ('-', p)) != std::string::npos; )
            s.replace (p, 1, "\xE2\x88\x92"), p += 3;
        return s;
    }

    struct ZoneAdvice { const char* range; const char* more; const char* less; };

    inline const std::array<ZoneAdvice, zones.size()>& zoneAdvice()
    {
        static const std::array<ZoneAdvice, zones.size()> a {{
            { "20–60 Hz",
              "il basso rimbomba e si mangia l'headroom del master. Abbassa un po' l'808/sub o taglia sotto i 30 Hz.",
              "l'808 si sente meno. Alza l'808 o il sub, e controlla che il passa-alto del master non sia troppo alto." },
            { "60–120 Hz",
              "cassa e 808 si pestano i piedi, il mix suona gonfio. Abbassa un filo l'808 o fai spazio alla cassa col sidechain.",
              "manca peso e pancia. Alza cassa/808 o aggiungi un po' di saturazione all'808." },
            { "120–400 Hz",
              "il mix suona impastato, fangoso. Prova a togliere 2–3 dB tra 200 e 400 Hz su beat o voce.",
              "il mix suona sottile, senza corpo. Togli meno low-mid sulla voce o alza gli strumenti caldi." },
            { "400 Hz–2 kHz",
              "suono nasale, scatolato. Cerca la risonanza tra 500 Hz e 1.5 kHz e togli 1–2 dB.",
              "suono vuoto: voce e melodie spariscono su telefono e laptop. Togli meno medi, soprattutto dalla voce." },
            { "2–6 kHz",
              "suono aggressivo che stanca. Controlla esse e hi-hat, togli 1–2 dB intorno a 3–5 kHz.",
              "la voce non esce dal beat. Prova +1–2 dB intorno a 3 kHz sulla voce." },
            { "6–20 kHz",
              "troppo brillante, sibilante. Abbassa gli hi-hat o de-essa la voce.",
              "suono opaco, chiuso. Prova uno shelf +1–2 dB sopra i 10 kHz sulla voce o sul master." },
        }};
        return a;
    }
}

inline std::vector<Finding> compare (const Snapshot& mix, const Snapshot* ref, const CompareSettings& cfg = {})
{
    using namespace detail;
    std::vector<Finding> out;

    if (ref == nullptr || ! ref->valid)
    {
        out.push_back ({ 10, "Carica un pezzo trap che ti piace come riferimento: da lì ti dico dove il tuo mix è diverso." });
        return out;
    }
    if (! mix.valid)
    {
        out.push_back ({ 10, fmt ("Fai suonare il tuo pezzo: servono almeno 5 secondi di audio per un confronto affidabile (ora %.0f s).",
                                  std::min (mix.seconds, MixAnalyzer::minSeconds)) });
        return out;
    }

    // 1) tonalità per zone, a PARITÀ DI LOUDNESS (ogni zona relativa all'integrated del suo segnale)
    for (size_t z = 0; z < zones.size(); ++z)
    {
        const double d = (mix.zoneDb[z] - mix.integrated) - (ref->zoneDb[z] - ref->integrated);
        if (std::abs (d) < cfg.zoneThresholdDb || mix.zoneDb[z] <= Snapshot::silence || ref->zoneDb[z] <= Snapshot::silence)
            continue;
        const auto& a = zoneAdvice()[z];
        std::string t = std::string (zones[z].in) + " (" + a.range + ") hai "
                        + fmt ("%+.1f dB", d) + " rispetto al riferimento → " + (d > 0 ? a.more : a.less);
        out.push_back ({ std::abs (d), t });
    }

    // 2) compressione: PLR (true peak − integrated)
    {
        const double dp = mix.plr - ref->plr;
        if (dp <= -cfg.plrThreshold)
            out.push_back ({ std::abs (dp) * 1.2,
                "Sei " + fmt ("%.1f LU", -dp) + " più compresso del riferimento (PLR "
                + fmt2 ("%.1f contro %.1f", mix.plr, ref->plr)
                + ") → meno punch, transienti schiacciati. Togli guadagno al limiter o al clipper." });
        else if (dp >= cfg.plrThreshold + 1.0)
            out.push_back ({ dp * 0.5,
                "Sei " + fmt ("%.1f LU", dp) + " più dinamico del riferimento (PLR "
                + fmt2 ("%.1f contro %.1f", mix.plr, ref->plr)
                + "). Non è un errore: se vuoi la densità del riferimento, spingi un po' di più limiter o clipper." });
    }

    // 3) bassi in mono
    if (mix.stereo)
    {
        const double c = mix.correlation[0];
        if (c < 0.0)
            out.push_back ({ 8, "I tuoi bassi sotto i 120 Hz sono FUORI FASE (correlazione " + fmt ("%.2f", c)
                                + ") → in mono e in club spariscono. Metti in mono 808 e sub e controlla la fase della cassa." });
        else if (c < cfg.monoBassMinCorr)
            out.push_back ({ 3.0 + (cfg.monoBassMinCorr - c) * 10.0,
                "I tuoi bassi sotto i 120 Hz non sono mono (correlazione " + fmt ("%.2f", c)
                + ") → in club e sui telefoni si perdono. Metti in mono 808 e sub (un \"mono maker\" sotto i 120 Hz)." });

        // 4) larghezza medi/alti: UNA frase, sul gruppo con la differenza più grande
        size_t widest = 1;
        for (size_t g = 2; g < stereoGroups.size(); ++g)
            if (std::abs (mix.sideToMidDb[g] - ref->sideToMidDb[g]) > std::abs (mix.sideToMidDb[widest] - ref->sideToMidDb[widest]))
                widest = g;
        if (ref->stereo)
        {
            const size_t g = widest;
            const double d = mix.sideToMidDb[g] - ref->sideToMidDb[g];
            if (d <= -cfg.widthThresholdDb)
                out.push_back ({ std::abs (d) * 0.5,
                    std::string (stereoGroups[g].in) + " sei più stretto del riferimento (" + fmt ("%.1f dB", -d)
                    + " di lati in meno) → il mix suona piccolo. Allarga pad, hi-hat e cori; la voce principale resta al centro." });
            else if (d >= cfg.widthThresholdDb)
                out.push_back ({ std::abs (d) * 0.5,
                    std::string (stereoGroups[g].in) + " sei più largo del riferimento (" + fmt ("%+.1f dB", d)
                    + " di lati) → in mono perdi pezzi. Restringi un po' gli elementi più allargati." });
        }
    }

    // 5) true peak
    if (mix.truePeakDb > 0.0)
        out.push_back ({ 3.5, "Il tuo true peak è " + fmt ("%+.1f dBTP", mix.truePeakDb)
                              + " → dopo la conversione mp3/aac di Spotify può distorcere. Metti il ceiling del limiter a −1 dBTP." });
    else if (mix.truePeakDb > -1.0)
        out.push_back ({ 1.0, "Il tuo true peak è " + fmt ("%.1f dBTP", mix.truePeakDb)
                              + ": al limite. Per stare sicuro dopo la conversione mp3/aac, ceiling del limiter a −1 dBTP." });

    // 6) volume: informazione, non difetto
    {
        const double d = mix.integrated - ref->integrated;
        if (std::abs (d) >= 1.0)
            out.push_back ({ 0.8, "Sei " + fmt ("%.1f LU", std::abs (d)) + (d < 0 ? " più basso" : " più forte")
                                  + " del riferimento (" + fmt2 ("%.1f contro %.1f LUFS", mix.integrated, ref->integrated)
                                  + "). Non è un difetto: Spotify porta tutto a −14 LUFS. Conta come suona allo stesso volume: usa A/B con Match." });
    }

    std::stable_sort (out.begin(), out.end(), [] (const Finding& a, const Finding& b) { return a.severity > b.severity; });
    if (out.size() > cfg.maxFindings) out.resize (cfg.maxFindings);
    if (out.empty())
        out.push_back ({ 0, "Il tuo mix è vicino al riferimento in tutte le zone: nessuna differenza sopra 1.5 dB. Ora decide l'orecchio." });
    return out;
}
} // namespace mc
