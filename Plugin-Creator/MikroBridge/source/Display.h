// Framebuffer 1 bit del display 128x64 della Mikro MK2, con testo nel font da 5 px (scala 1 o 2).
#pragma once

#include "Font5.h"
#include "MikroProtocol.h"

#include <cstring>

namespace mikro
{
class Display
{
public:
    using Buffer = std::array<uint8_t, kDisplayBytes>;

    void clear() noexcept { fb.fill (0); }

    void pixel (int x, int y, bool on) noexcept
    {
        if (x < 0 || y < 0 || x >= kDisplayW || y >= kDisplayH)
            return;
        auto& b = fb[size_t ((y >> 3) * kDisplayW + x)];
        const uint8_t bit = uint8_t (1u << (y & 7));
        b = on ? uint8_t (b | bit) : uint8_t (b & ~bit);
    }

    void fillRect (int x, int y, int w, int h, bool on) noexcept
    {
        for (int j = y; j < y + h; ++j)
            for (int i = x; i < x + w; ++i)
                pixel (i, j, on);
    }

    // Disegna il testo con l'angolo in alto a sinistra in (x, y); ritorna la x finale
    int text (int x, int y, const char* s, int scale = 1, bool on = true) noexcept
    {
        for (; *s != 0; ++s)
        {
            const unsigned char c = (unsigned char) *s;
            const Glyph& g = kFont5[(c >= 32 && c < 128) ? c - 32 : '?' - 32];
            for (int col = 0; col < g.width; ++col)
                for (int row = 0; row < 5; ++row)
                    if ((g.cols[col] >> row) & 1)
                        fillRect (x + col * scale, y + row * scale, scale, scale, on);
            x += (g.width + 1) * scale;
        }
        return x;
    }

    static int textWidth (const char* s, int scale = 1) noexcept
    {
        int w = 0;
        for (; *s != 0; ++s)
        {
            const unsigned char c = (unsigned char) *s;
            w += (kFont5[(c >= 32 && c < 128) ? c - 32 : '?' - 32].width + 1) * scale;
        }
        return w > 0 ? w - scale : 0;
    }

    const Buffer& data() const noexcept { return fb; }
    bool operator!= (const Display& o) const noexcept { return std::memcmp (fb.data(), o.fb.data(), fb.size()) != 0; }

private:
    Buffer fb {};
};
} // namespace mikro
