#pragma once
// FFT complessa radix-2 iterativa, in place. Tabelle (twiddle, bit-reversal) costruite in prepare():
// perform() non alloca. Header senza JUCE.
#include <cmath>
#include <complex>
#include <vector>

namespace mc
{
class FFT
{
public:
    using Complex = std::complex<double>;

    void prepare (int size)
    {
        n = size;
        log2n = 0;
        while ((1 << log2n) < n) ++log2n;

        twiddles.resize ((size_t) n / 2);
        constexpr double pi = 3.14159265358979323846;
        for (int k = 0; k < n / 2; ++k)
            twiddles[(size_t) k] = std::polar (1.0, -2.0 * pi * k / n);

        bitrev.resize ((size_t) n);
        for (int i = 0; i < n; ++i)
        {
            int r = 0;
            for (int b = 0; b < log2n; ++b)
                if (i & (1 << b)) r |= 1 << (log2n - 1 - b);
            bitrev[(size_t) i] = r;
        }
    }

    int getSize() const noexcept { return n; }

    void perform (Complex* data) const noexcept
    {
        for (int i = 0; i < n; ++i)
        {
            const int j = bitrev[(size_t) i];
            if (j > i) std::swap (data[i], data[j]);
        }
        for (int len = 2; len <= n; len <<= 1)
        {
            const int half = len >> 1;
            const int step = n / len;
            for (int start = 0; start < n; start += len)
            {
                for (int k = 0; k < half; ++k)
                {
                    const Complex t = twiddles[(size_t) (k * step)] * data[start + k + half];
                    data[start + k + half] = data[start + k] - t;
                    data[start + k] += t;
                }
            }
        }
    }

private:
    int n = 0, log2n = 0;
    std::vector<Complex> twiddles;
    std::vector<int> bitrev;
};
} // namespace mc
