"""Icona di Mikro Bridge: la barca dell'animazione d'avvio (DisplayRenderer::renderSplash) su un LCD a 1 bit.

La barca e le onde si rasterizzano a bassa risoluzione con la stessa geometria del display, poi ogni pixel
acceso diventa un punto chiaro dell'LCD dentro il riquadro arrotondato delle icone macOS.
Uso: python3 tools/make_icon.py  ->  assets/icon_1024.png
"""
import math
from pathlib import Path

from PIL import Image, ImageDraw, ImageFilter

N = 40                       # pixel dell'LCD per lato
WATER = 28.5                 # riga dell'onda
SS = 16                      # supercampionamento per la rasterizzazione
SIZE, MARGIN, RADIUS = 1024, 100, 185
ON, OFF, GLASS, BEZEL = (220, 236, 255), (24, 36, 44), (13, 20, 24), (40, 46, 52)
BAYER = [[0, 8, 2, 10], [12, 4, 14, 6], [3, 11, 1, 9], [15, 7, 13, 5]]

OUT = Path(__file__).resolve().parent.parent / "assets" / "icon_1024.png"


def wave_y(x):
    return WATER + 1.3 * math.sin(x * 0.38 + 0.6)


def boat_layers():
    """Matrice N x N di pixel accesi: barca (come in renderSplash), onda netta, mare in dithering ordinato."""
    cx, tilt = N / 2 + 0.5, -0.06

    def xf(x, y):   # come renderSplash: rotazione, poi traslazione sull'onda
        c, s = math.cos(tilt), math.sin(tilt)
        return ((cx + x * c - y * s) * SS, (WATER + 0.5 + x * s + y * c) * SS)

    def poly(points):
        return [xf(x, y) for x, y in points]

    hull = poly([(-14, -4), (14, -4), (9, 2), (-10, 2)])
    mast = poly([(-0.5, -25), (1.0, -25), (1.0, -4), (-0.5, -4)])
    main = poly([(2.5, -23), (2.5, -6), (13, -6)])     # vele staccate dall'albero di un pixel: si leggono meglio
    jib = poly([(-2, -20), (-2, -6), (-11, -6)])

    big = N * SS
    lit = Image.new("L", (big, big), 0)      # 255 = acceso
    cover = Image.new("L", (big, big), 0)    # 255 = coperto dalla barca e dal suo contorno nero
    dl, dc = ImageDraw.Draw(lit), ImageDraw.Draw(cover)
    for shape in (hull, main, jib, mast):    # contorno nero di 2 px attorno alla barca
        dc.polygon(shape, fill=255)
        dc.line(shape + [shape[0]], fill=255, width=4 * SS, joint="curve")
    for shape in (hull, main, jib, mast):
        dl.polygon(shape, fill=255)
    dl.line([xf(5, -16), xf(8, -9)], fill=0, width=SS)   # cucitura della vela

    lit = lit.resize((N, N), Image.BOX)
    cover = cover.resize((N, N), Image.BOX)

    pixels = [[False] * N for _ in range(N)]
    for x in range(N):
        surface = round(wave_y(x))
        for y in range(N):
            if cover.getpixel((x, y)) > 100:
                pixels[y][x] = lit.getpixel((x, y)) > 128
            elif y == surface:
                pixels[y][x] = True
            elif y > surface:   # mare: grigio che scende verso il fondo, dithering ordinato come sul display
                t = (y - surface) / (N - surface)
                g = 0.6 + (0.15 - 0.6) * t
                pixels[y][x] = g * 255 > BAYER[y & 3][x & 3] * 16 + 8
    return pixels


def main():
    pixels = boat_layers()
    img = Image.new("RGBA", (SIZE, SIZE), (0, 0, 0, 0))

    # Ombra morbida sotto il riquadro
    shadow = Image.new("RGBA", (SIZE, SIZE), (0, 0, 0, 0))
    ImageDraw.Draw(shadow).rounded_rectangle(
        (MARGIN, MARGIN + 14, SIZE - MARGIN, SIZE - MARGIN + 14), RADIUS, fill=(0, 0, 0, 110))
    img.alpha_composite(shadow.filter(ImageFilter.GaussianBlur(18)))

    d = ImageDraw.Draw(img)
    d.rounded_rectangle((MARGIN, MARGIN, SIZE - MARGIN, SIZE - MARGIN), RADIUS, fill=BEZEL)
    inner = MARGIN + 26
    d.rounded_rectangle((inner, inner, SIZE - inner, SIZE - inner), RADIUS - 26, fill=GLASS)

    # LCD: pixel quadrati con un filo di spazio, i punti accesi con un leggero bagliore; ritagliato sul vetro
    base = img
    img = Image.new("RGBA", (SIZE, SIZE), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    area = SIZE - 2 * inner
    cell = area / N
    x0 = y0 = (SIZE - cell * N) / 2
    glow = Image.new("RGBA", (SIZE, SIZE), (0, 0, 0, 0))
    dg = ImageDraw.Draw(glow)
    for y in range(N):
        for x in range(N):
            r = (x0 + x * cell + 1.5, y0 + y * cell + 1.5, x0 + (x + 1) * cell - 1.5, y0 + (y + 1) * cell - 1.5)
            if pixels[y][x]:
                d.rounded_rectangle(r, 3, fill=ON)
                dg.rounded_rectangle(r, 3, fill=ON + (150,))
            else:
                d.rounded_rectangle(r, 3, fill=OFF)
    img.alpha_composite(glow.filter(ImageFilter.GaussianBlur(10)))
    # ridisegna i punti accesi sopra il bagliore, netti
    d = ImageDraw.Draw(img)
    for y in range(N):
        for x in range(N):
            if pixels[y][x]:
                d.rounded_rectangle((x0 + x * cell + 1.5, y0 + y * cell + 1.5,
                                     x0 + (x + 1) * cell - 1.5, y0 + (y + 1) * cell - 1.5), 3, fill=ON)

    mask = Image.new("L", (SIZE, SIZE), 0)
    lcd = inner + 14
    ImageDraw.Draw(mask).rounded_rectangle((lcd, lcd, SIZE - lcd, SIZE - lcd), RADIUS - 40, fill=255)
    base.paste(img, (0, 0), Image.composite(img.getchannel("A"), Image.new("L", (SIZE, SIZE), 0), mask))
    img = base

    OUT.parent.mkdir(exist_ok=True)
    img.save(OUT)
    print(OUT)


if __name__ == "__main__":
    main()
