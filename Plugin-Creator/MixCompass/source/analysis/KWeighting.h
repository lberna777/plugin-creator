#pragma once
// K-weighting ITU-R BS.1770-4: shelf alto (testa) + passa-alto RLB, ricalcolati per qualunque
// sample rate dai parametri analogici (stessa derivazione di libebur128), non solo i coefficienti
// tabulati a 48 kHz. Header senza JUCE: riusabile da VOCAL FORGE.
#include <cmath>

namespace mc
{
struct Biquad
{
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
    double z1 = 0, z2 = 0;

    inline double process (double x) noexcept
    {
        // Direct Form II transposta
        const double y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }
    void reset() noexcept { z1 = z2 = 0; }
};

class KWeighting
{
public:
    void prepare (double sampleRate) noexcept
    {
        constexpr double pi = 3.14159265358979323846;
        {   // stadio 1: shelf alto (~+4 dB sopra ~1.7 kHz), modello della testa
            const double f0 = 1681.974450955533, G = 3.999843853973347, Q = 0.7071752369554196;
            const double K  = std::tan (pi * f0 / sampleRate);
            const double Vh = std::pow (10.0, G / 20.0);
            const double Vb = std::pow (Vh, 0.4996667741545416);
            const double a0 = 1.0 + K / Q + K * K;
            shelf.b0 = (Vh + Vb * K / Q + K * K) / a0;
            shelf.b1 = 2.0 * (K * K - Vh) / a0;
            shelf.b2 = (Vh - Vb * K / Q + K * K) / a0;
            shelf.a1 = 2.0 * (K * K - 1.0) / a0;
            shelf.a2 = (1.0 - K / Q + K * K) / a0;
        }
        {   // stadio 2: passa-alto RLB (~38 Hz)
            const double f0 = 38.13547087602444, Q = 0.5003270373238773;
            const double K  = std::tan (pi * f0 / sampleRate);
            const double a0 = 1.0 + K / Q + K * K;
            highpass.b0 = 1.0;
            highpass.b1 = -2.0;
            highpass.b2 = 1.0;
            highpass.a1 = 2.0 * (K * K - 1.0) / a0;
            highpass.a2 = (1.0 - K / Q + K * K) / a0;
        }
        reset();
    }

    void reset() noexcept { shelf.reset(); highpass.reset(); }

    inline double process (double x) noexcept { return highpass.process (shelf.process (x)); }

private:
    Biquad shelf, highpass;
};
} // namespace mc
