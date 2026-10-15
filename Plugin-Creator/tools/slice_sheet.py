#!/usr/bin/env python3
"""Ritaglia un "foglio" Gemini con più oggetti su verde #00FF00 in un file per oggetto.

Invece di chiedere a Gemini un'immagine per tasto, si chiede UN foglio con tutti gli oggetti
in griglia (prompt "foglio unico" in RSU10/ui/GEMINI_PROMPTS.md). Questo script:
  1. trova gli oggetti sul verde (componenti connesse della maschera, le briciole scartate);
  2. li ordina per righe e colonne, nell'ordine dei nomi passati;
  3. salva ognuno ritagliato con un margine di verde, pronto per build_assets.py.
Con --pressed genera anche lo stato premuto (_down) da quello a riposo (_up): un po' più
piccolo, più scuro al centro. Così Gemini disegna solo gli oggetti a riposo.

Uso (da RSU10/ui):
    python3 ../../tools/slice_sheet.py foglio.png --out src --pressed \\
        --names knob pad_up key_rec_up key_grey_up key_violet_up key_enter_up \\
                key_rubber_up key_wide_up key_tiny_up

Un file già presente in --out non viene sovrascritto senza --force.
"""
import argparse
import os
import sys
import numpy as np
from PIL import Image
from scipy import ndimage

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from build_assets import chroma_key  # stessa chiave della pipeline

GREEN = np.array([0, 255, 0], dtype=float)


def find_objects(rgb, min_frac=0.002):
    """Rettangoli (y0, y1, x0, x1) degli oggetti, con maschera e etichette per isolarli."""
    alpha = np.asarray(chroma_key(Image.fromarray(rgb)))[..., 3] > 128
    # chiude piccoli buchi (es. la linea incisa del knob) senza unire oggetti vicini
    solid = ndimage.binary_closing(alpha, iterations=3)
    solid = ndimage.binary_fill_holes(solid)
    labels, n = ndimage.label(solid)
    if n == 0:
        return [], labels
    sizes = ndimage.sum(solid, labels, range(1, n + 1))
    keep = [i + 1 for i, s in enumerate(sizes) if s >= min_frac * solid.size]
    boxes = []
    for lab, sl in enumerate(ndimage.find_objects(labels), start=1):
        if lab in keep:
            boxes.append((sl[0].start, sl[0].stop, sl[1].start, sl[1].stop, lab))
    return boxes, labels


def reading_order(boxes):
    """Raggruppa in righe (centri verticali che si sovrappongono) e ordina sinistra→destra."""
    boxes = sorted(boxes, key=lambda b: (b[0] + b[1]) / 2)
    rows = []
    for b in boxes:
        cy = (b[0] + b[1]) / 2
        if rows and rows[-1]["y0"] <= cy <= rows[-1]["y1"]:
            rows[-1]["items"].append(b)
            rows[-1]["y0"] = min(rows[-1]["y0"], b[0])
            rows[-1]["y1"] = max(rows[-1]["y1"], b[1])
        else:
            rows.append({"y0": b[0], "y1": b[1], "items": [b]})
    return [b for r in rows for b in sorted(r["items"], key=lambda b: b[2])]


def cut(rgb, labels, box, margin):
    """Ritaglio dell'oggetto su verde pieno: gli altri oggetti vicini vengono coperti di verde."""
    y0, y1, x0, x1, lab = box
    h, w = labels.shape
    Y0, Y1 = max(0, y0 - margin), min(h, y1 + margin)
    X0, X1 = max(0, x0 - margin), min(w, x1 + margin)
    crop = rgb[Y0:Y1, X0:X1].copy()
    lab_crop = labels[Y0:Y1, X0:X1]
    other = (lab_crop != 0) & (lab_crop != lab)
    other = ndimage.binary_dilation(other, iterations=4)
    crop[other] = GREEN.astype(np.uint8)
    return crop


def make_pressed(rgb, shrink=0.97, darken=0.82, centre_dark=0.12):
    """Stato premuto: oggetto rimpicciolito del 3 %, più scuro, un po' di più al centro."""
    im = Image.fromarray(rgb)
    rgba = chroma_key(im)
    w, h = rgba.size
    small = rgba.resize((round(w * shrink), round(h * shrink)), Image.LANCZOS)
    a = np.asarray(small).astype(float)
    yy, xx = np.mgrid[0:a.shape[0], 0:a.shape[1]]
    r = np.hypot((yy - a.shape[0] / 2) / (a.shape[0] / 2), (xx - a.shape[1] / 2) / (a.shape[1] / 2))
    gain = darken * (1.0 - centre_dark * np.clip(1.0 - r, 0, 1))
    a[..., :3] *= gain[..., None]
    out = Image.new("RGB", (w, h), (0, 255, 0))
    layer = Image.fromarray(np.clip(a, 0, 255).astype(np.uint8))
    out.paste(layer, ((w - layer.width) // 2, (h - layer.height) // 2), layer)
    return np.asarray(out)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("sheet", help="immagine Gemini con tutti gli oggetti su verde")
    ap.add_argument("--names", nargs="+", required=True, help="nomi file in ordine di lettura (righe, poi colonne)")
    ap.add_argument("--out", default="src", help="cartella di uscita (default: src)")
    ap.add_argument("--pressed", action="store_true", help="genera anche *_down da ogni *_up")
    ap.add_argument("--margin", type=int, default=24, help="verde attorno all'oggetto, px")
    ap.add_argument("--force", action="store_true", help="sovrascrive i file già presenti")
    ap.add_argument("--preview", help="salva un'anteprima con i nomi sugli oggetti trovati")
    args = ap.parse_args()

    rgb = np.asarray(Image.open(args.sheet).convert("RGB"))
    boxes, labels = find_objects(rgb)
    boxes = reading_order(boxes)

    if args.preview:
        from PIL import ImageDraw
        prev = Image.fromarray(rgb.copy())
        d = ImageDraw.Draw(prev)
        for i, (y0, y1, x0, x1, _) in enumerate(boxes):
            d.rectangle([x0, y0, x1, y1], outline=(255, 0, 255), width=4)
            d.text((x0 + 6, y0 + 6), args.names[i] if i < len(args.names) else f"? {i + 1}", fill=(255, 0, 255))
        prev.save(args.preview)

    if len(boxes) != len(args.names):
        sys.exit(f"[ERRORE] trovati {len(boxes)} oggetti, attesi {len(args.names)}. "
                 f"Oggetti attaccati o un oggetto in più/in meno: rigenera il foglio "
                 f"chiedendo più spazio tra gli oggetti (usa --preview per vedere cosa ha trovato).")

    os.makedirs(args.out, exist_ok=True)
    written = []
    for name, box in zip(args.names, boxes):
        crop = cut(rgb, labels, box, args.margin)
        targets = [(name, crop)]
        if args.pressed and name.endswith("_up"):
            targets.append((name[:-3] + "_down", make_pressed(crop)))
        for fname, img in targets:
            path = os.path.join(args.out, fname + ".png")
            if os.path.exists(path) and not args.force:
                print(f"--  {path} esiste già, lasciato com'è (--force per sovrascrivere)")
                continue
            Image.fromarray(img).save(path)
            written.append(path)
            print(f"OK  {path}  {img.shape[1]}x{img.shape[0]}")
    print(f"{len(written)} file scritti in {args.out}.")


if __name__ == "__main__":
    main()
