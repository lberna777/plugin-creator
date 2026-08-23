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


OPS = {
    "key_background": key_background,
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
        im.save(a["output"])
        print(f"OK  {a['output']}  {im.size}")
    print("Pipeline asset completata.")


if __name__ == "__main__":
    if len(sys.argv) < 2:
        sys.exit("uso: python3 build_assets.py <manifest.json>")
    run(sys.argv[1])
