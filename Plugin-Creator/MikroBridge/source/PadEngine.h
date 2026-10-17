// Da pressione continua (0..4095) a note MIDI: soglia con isteresi, velocity dal picco
// dei primi report dopo il colpo, aftertouch dalla pressione mentre il pad è tenuto.
// Niente JUCE e niente allocazioni: gira sul thread USB.
#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace mikro
{
enum class VelocityCurve { Soft, Linear, Hard, Fixed };

struct PadSettings
{
    int onThreshold = 250;          // pressione minima per suonare
    int fullScale = 3200;           // pressione che dà velocity 127
    int attackReports = 3;          // report su cui si cerca il picco (latenza ~1-3 ms)
    VelocityCurve curve = VelocityCurve::Linear;
    bool aftertouch = false;

    // Contro i doppi colpi: il rilascio vale solo se la pressione resta bassa per qualche report
    // (la gomma rimbalza quando si stacca il dito), poi il pad non riparte per retriggerMs
    int releaseReports = 3;
    double retriggerMs = 30.0;
    // Contro i colpi fantasma (diafonia tra pad vicini): un pad parte solo se ha almeno questa
    // frazione della pressione del pad più premuto in quel momento. 0 = spento
    float crosstalk = 0.35f;
};

struct PadEvent
{
    enum Type : uint8_t { None, NoteOn, NoteOff, Pressure } type = None;
    uint8_t value = 0;   // velocity o pressione 0..127
};

class PadEngine
{
public:
    // Al più due eventi per report (NoteOn+NoteOff se il colpo è più breve della finestra d'attacco)
    struct Events { PadEvent e[2]; int count = 0; void add (PadEvent ev) { e[count++] = ev; } };

    // nowMs: orologio in millisecondi; othersMax: pressione più alta tra gli ALTRI pad in questo report
    Events process (int pressure, const PadSettings& s, double nowMs = 0.0, int othersMax = 0) noexcept
    {
        Events out;
        const int off = s.onThreshold * 2 / 5;

        switch (state)
        {
            case State::Idle:
            {
                const bool lockedOut = releasedAt > -1.0e8 && nowMs - releasedAt < s.retriggerMs;
                const int threshold = std::max (s.onThreshold, int (s.crosstalk * float (othersMax)));
                if (! lockedOut && pressure >= threshold)
                {
                    state = State::Attack;
                    peak = pressure;
                    reports = 1;
                    if (s.attackReports <= 1)
                        fire (out, s);
                }
                break;
            }

            case State::Attack:
                if (pressure < off)
                {
                    fire (out, s);
                    release (out, nowMs);
                    break;
                }
                if (pressure < peak || ++reports >= s.attackReports)
                {
                    peak = std::max (peak, pressure);
                    fire (out, s);
                    break;
                }
                peak = pressure;
                break;

            case State::Held:
                if (pressure < off)
                {
                    if (++lowReports >= std::max (1, s.releaseReports))
                        release (out, nowMs);
                    break;
                }
                lowReports = 0;
                if (s.aftertouch)
                {
                    const int at = toMidi (pressure - s.onThreshold, s.fullScale - s.onThreshold, 1.0f);
                    if (std::abs (at - lastPressure) >= 2)
                    {
                        lastPressure = at;
                        out.add ({ PadEvent::Pressure, uint8_t (at) });
                    }
                }
                break;
        }
        return out;
    }

    bool isHeld() const noexcept    { return state == State::Held; }
    uint8_t velocity() const noexcept { return lastVelocity; }
    void reset() noexcept             { state = State::Idle; lowReports = 0; releasedAt = -1.0e9; }

    static int velocityFor (int peakPressure, const PadSettings& s) noexcept
    {
        if (s.curve == VelocityCurve::Fixed)
            return 127;
        const float gamma = s.curve == VelocityCurve::Soft ? 0.55f : s.curve == VelocityCurve::Hard ? 1.7f : 1.0f;
        return std::max (1, toMidi (peakPressure - s.onThreshold, s.fullScale - s.onThreshold, gamma));
    }

private:
    enum class State { Idle, Attack, Held };

    static int toMidi (int x, int range, float gamma) noexcept
    {
        const float n = std::clamp (float (x) / float (std::max (1, range)), 0.0f, 1.0f);
        return int (std::lround (127.0f * std::pow (n, gamma)));
    }

    void fire (Events& out, const PadSettings& s) noexcept
    {
        lastVelocity = uint8_t (velocityFor (peak, s));
        lastPressure = -1;
        lowReports = 0;
        state = State::Held;
        out.add ({ PadEvent::NoteOn, lastVelocity });
    }

    void release (Events& out, double nowMs) noexcept
    {
        state = State::Idle;
        releasedAt = nowMs;
        out.add ({ PadEvent::NoteOff, 0 });
    }

    State state = State::Idle;
    int peak = 0, reports = 0, lastPressure = -1, lowReports = 0;
    double releasedAt = -1.0e9;
    uint8_t lastVelocity = 0;
};
} // namespace mikro
