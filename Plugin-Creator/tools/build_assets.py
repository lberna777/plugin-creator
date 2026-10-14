#!/usr/bin/env python3
"""Pipeline asset deterministica: sorgente -> operazioni -> verifica -> output.

Uso:
    python3 tools/build_assets.py tools/assets.manifest.json

Filosofia (lezione NEMO): ogni trasformazione d'immagine è versionata e ripetibile.
Niente editing "usa e getta" in Bash. Gli assert su alpha e dimensioni impediscono
i regressi tipici: PNG senza canale alpha, aspetto/scala sbagliati.

Dipendenze: Pillow, numpy, scipy
    python3 -m pip install --user Pillow numpy scipy
"""
import json
import sys
import numpy as np
from PIL import Image
from scipy import ndimage


def require_alpha(im, path):
    if im.mode != "RGBA":
        raise SystemExit(f"[ERRORE] {path}: manca il canale alpha (mode={im.mode}). "
                         f"Gli elementi mobili DEVONO essere PNG con trasparenza.")
    return im


def autocrop(im, pad=2, **_):
    a = np.asarray(im)
    ys, xs = np.where(a[..., 3] > 16)
    if len(xs) == 0:
        return im
    x0, x1 = max(0, xs.min() - pad), min(a.shape[1], xs.max() + 1 + pad)
    y0, y1 = max(0, ys.min() - pad), min(a.shape[0], ys.max() + 1 + pad)
    return im.crop((x0, y0, x1, y1))


def square(im, **_):
    """Canvas quadrato, contenuto centrato (per knob ruotabili)."""
    w, h = im.size
    s = max(w, h)
    out = Image.new("RGBA", (s, s), (0, 0, 0, 0))
    out.paste(im, ((s - w) // 2, (s - h) // 2), im)
    return out


def key_background(im, lum_thresh=150, sat_thresh=45, **_):
    """Scontorna uno sfondo chiaro/uniforme via flood-fill dai bordi.
    Utile se la sorgente è arrivata appiattita (NO: pretendi PNG-alpha alla fonte)."""
    a = np.asarray(im.convert("RGB")).astype(float)
    lum = a.mean(2)
    sat = a.max(2) - a.min(2)
    fillable = (lum > lum_thresh) & (sat < sat_thresh)
    lbl, _ = ndimage.label(fillable)
    border = set(np.unique(np.concatenate([lbl[0], lbl[-1], lbl[:, 0], lbl[:, -1]])))
    border.discard(0)
    bg = np.isin(lbl, list(border))
    alpha = np.where(bg, 0.0, 255.0)
    alpha = ndimage.gaussian_filter(alpha, 0.8)
    return Image.fromarray(np.dstack([a, alpha]).astype("uint8"), "RGBA")


def chroma_key(im, key=(0, 255, 0), tol=90, soft=40, despill=True, **_):
    """Rimuove lo sfondo di colore pieno (verde #00FF00 delle sorgenti Gemini).
    Alpha 0 entro `tol` dal colore chiave, 255 oltre `tol+soft`, rampa lineare in mezzo.
    Con `despill`, il verde residuo sui bordi viene schiacciato a max(rosso, blu)."""
    a = np.asarray(im.convert("RGB")).astype(float)
    d = np.sqrt(((a - np.asarray(key, dtype=float)) ** 2).sum(2))
    alpha = np.clip((d - tol) / max(soft, 1e-6), 0.0, 1.0) * 255.0
    if despill:
        rb = np.maximum(a[..., 0], a[..., 2])
        fix = (alpha > 0) & (a[..., 1] > rb)
        a[..., 1] = np.where(fix, rb, a[..., 1])
    out = np.dstack([a, alpha])
    return Image.fromarray(np.clip(np.round(out), 0, 255).astype("uint8"))


def rotate_to_top(im, **_):
    """Porta l'indicatore chiaro del knob a ore 12 (assume sfondo già trasparente)."""
    import math
    a = np.asarray(im)
    al = a[..., 3] > 40
    lum = a[..., :3].mean(2)
    ind = al & (lum > 150)
    ys, xs = np.where(ind)
    if len(xs) == 0:
        return im
    cx, cy = im.size[0] / 2, im.size[1] / 2
    ang = math.degrees(math.atan2(xs.mean() - cx, -(ys.mean() - cy)))  # CW da su
    return im.rotate(ang, resample=Image.BICUBIC, expand=False)


def crop_box(im, box, **_):
    """Ritaglia [x0, y0, x1, y1] in pixel della sorgente (es. piastra con sfondo intorno)."""
    return im.crop(tuple(box))


def hslice(im, left, center, right, **_):
    """Accorcia in larghezza senza deformare: tiene i bordi (left/right px) e la fascia centrale (center px)."""
    w, h = im.size
    cx = w // 2
    parts = [im.crop((0, 0, left, h)), im.crop((cx - center // 2, 0, cx - center // 2 + center, h)), im.crop((w - right, 0, w, h))]
    out = Image.new("RGBA", (left + center + right, h), (0, 0, 0, 0))
    x = 0
    for p in parts:
        out.paste(p, (x, 0))
        x += p.width
    return out


def resize(im, w=None, h=None, **_):
    """Ridimensiona (Lanczos) alla misura @2x d'uso. Con un solo lato mantiene le proporzioni."""
    if w is None:
        w = round(im.width * h / im.height)
    if h is None:
        h = round(im.height * w / im.width)
    return im.resize((int(w), int(h)), Image.LANCZOS)


def opaque(im, **_):
    """Toglie l'alpha (per sfondi da salvare in JPEG)."""
    return im.convert("RGB")


def circle_crop(im, r=0.5, **_):
    """Tiene solo il disco centrale di raggio r (frazione del lato): es. il mozzo senza la flangia."""
    import math
    w, h = im.size
    cx, cy, rad = w / 2, h / 2, r * min(w, h)
    yy, xx = np.mgrid[0:h, 0:w]
    m = np.clip(rad - np.sqrt((xx - cx) ** 2 + (yy - cy) ** 2) + 0.5, 0, 1)
    a = np.asarray(im).astype(float)
    a[..., 3] *= m
    out = Image.fromarray(a.astype("uint8"), "RGBA")
    return out.crop((int(cx - rad), int(cy - rad), int(cx + rad), int(cy + rad)))


OPS = {
    "key_background": key_background,
    "chroma_key": chroma_key,
    "crop_box": crop_box,
    "hslice": hslice,
    "resize": resize,
    "opaque": opaque,
    "circle_crop": circle_crop,
    "autocrop": autocrop,
    "rotate_to_top": rotate_to_top,
    "square": square,
}


def run(manifest_path):
    man = json.load(open(manifest_path))
    for a in man["assets"]:
        im = Image.open(a["input"])
        if a.get("expect_alpha", True) and im.mode == "RGBA":
            require_alpha(im, a["input"])
        im = im.convert("RGBA")
        for step in a.get("ops", []):
            op = step if isinstance(step, str) else step["op"]
            args = {} if isinstance(step, str) else {k: v for k, v in step.items() if k != "op"}
            im = OPS[op](im, **args)
        if "expect" in a:
            ew, eh = a["expect"]
            if abs(im.width - ew) > 2 or abs(im.height - eh) > 2:
                raise SystemExit(f"[ERRORE] {a['output']}: dimensioni {im.size} != attese {(ew, eh)}")
        im.save(a["output"], **({"quality": a["quality"]} if "quality" in a else {}))
        print(f"OK  {a['output']}  {im.size}")
    print("Pipeline asset completata.")


if __name__ == "__main__":
    if len(sys.argv) < 2:
        sys.exit("uso: python3 build_assets.py <manifest.json>")
    run(sys.argv[1])
