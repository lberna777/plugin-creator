#!/usr/bin/env python3
"""Genera source/assets/layout.json dalla griglia di progetto (deterministico, versionato).

Non esiste una foto di riferimento: la UI è vettoriale, quindi la "misura" è questa griglia.
Tutte le coordinate vengono normalizzate 0..1 rispetto alla finestra base W x H.
Per spostare qualcosa: modifica qui, rilancia, ricompila (oppure hot-reload in Debug).

    python3 tools/gen_layout.py
"""
import json
from pathlib import Path

W, H = 1140, 720
M, GAP = 16, 12
HEADER_H = 62
MODULE_TITLE_H = 30

ROW_A = (70, 386)
ROW_B = (398, 704)

out = {
    "_comment": "GENERATO da tools/gen_layout.py — non modificare a mano. Coordinate normalizzate 0..1.",
    "window": {"w": W, "h": H, "minScale": 0.75, "maxScale": 1.5},
    "sections": {}, "knobs": {}, "steps": {}, "leds": {}, "combos": {}, "toggles": {}, "labels": {},
}


def nx(v): return round(v / W, 5)
def ny(v): return round(v / H, 5)


def section(name, title, x0, y0, x1, y1):
    out["sections"][name] = {"x0": nx(x0), "y0": ny(y0), "x1": nx(x1), "y1": ny(y1), "title": title}


def circle(group, key, cx, cy, d):
    out[group][key] = {"cx": nx(cx), "cy": ny(cy), "w": nx(d), "h": ny(d)}


def box(group, key, cx, cy, w, h):
    out[group][key] = {"cx": nx(cx), "cy": ny(cy), "w": nx(w), "h": ny(h)}


def columns(x0, x1, n):
    cw = (x1 - x0) / n
    return [x0 + cw * (i + 0.5) for i in range(n)]


KNOB = 60
SMALL = 52
STEP = 48

# ---- header ----
out["header"] = {
    "title":   {"x0": nx(28), "y0": ny(10), "x1": nx(520), "y1": ny(44)},
    "tagline": {"x0": nx(30), "y0": ny(40), "x1": nx(620), "y1": ny(58)},
    "version": {"x0": nx(600), "y0": ny(18), "x1": nx(780), "y1": ny(44)},
    "scope":   {"x0": nx(800), "y0": ny(12), "x1": nx(W - M), "y1": ny(56)},
}

# ---- riga A ----
ya0, ya1 = ROW_A
r1 = ya0 + MODULE_TITLE_H + 72
r2 = r1 + 143

osc = (M, 396); filt = (408, 608); env = (620, 820); mod = (832, W - M)
section("osc", "OSCILLATORS", osc[0], ya0, osc[1], ya1)
section("filter", "LADDER FILTER", filt[0], ya0, filt[1], ya1)
section("env", "ENVELOPE / VCA", env[0], ya0, env[1], ya1)
section("mod", "MODULATION", mod[0], ya0, mod[1], ya1)

c = columns(*osc, 4)
for i, pid in enumerate(["oscWave", "oscTune", "osc2Detune", "glide"]): circle("knobs", pid, c[i], r1, KNOB)
for i, pid in enumerate(["oscSub", "oscNoise", "fold", "drift"]):       circle("knobs", pid, c[i], r2, KNOB)

c = columns(*filt, 2)
circle("knobs", "cutoff", c[0], r1, KNOB); circle("knobs", "reso", c[1], r1, KNOB)
circle("knobs", "filterEnv", c[0], r2, KNOB); circle("knobs", "filterDrive", c[1], r2, KNOB)

c = columns(*env, 2)
circle("knobs", "attack", c[0], r1, KNOB); circle("knobs", "decay", c[1], r1, KNOB)
circle("knobs", "sustain", c[0], r2, KNOB)
box("toggles", "drone", c[1], r2 - 6, 80, 30)

c = columns(*mod, 3)
circle("knobs", "lfoRate", c[0], r1, KNOB)
box("combos", "lfoShape", c[1], r1 - 6, 84, 26)
circle("knobs", "chaos", c[2], r1, KNOB)
circle("knobs", "lfoPitch", c[0], r2, KNOB); circle("knobs", "lfoCutoff", c[1], r2, KNOB)
circle("knobs", "lfoFold", c[2], r2, KNOB)

# ---- riga B ----
yb0, yb1 = ROW_B
seq = (M, 760); fx = (772, W - M)
section("seq", "SEQUENCER  ·  8 STEP MACHINE", seq[0], yb0, seq[1], yb1)
section("fx", "TAPE  ·  SPACE  ·  OUT", fx[0], yb0, fx[1], yb1)

c = columns(*seq, 8)
led_y = yb0 + MODULE_TITLE_H + 20
step_y = led_y + 58
ctl_y = yb1 - 64
for i in range(8):
    box("leds", f"step{i + 1}", c[i], led_y, 14, 14)
    circle("steps", f"step{i + 1}", c[i], step_y, STEP)

box("toggles", "seqOn", c[0], ctl_y - 6, 70, 30)
box("combos", "seqRate", c[1], ctl_y - 6, 84, 26)
box("combos", "seqScale", c[2], ctl_y - 6, 88, 26)
for i, pid in enumerate(["seqLength", "seqProb", "seqMutate", "seqRatchet", "seqSwing"]):
    circle("knobs", pid, c[3 + i], ctl_y, SMALL)

out["labels"]["seqDivider"] = {"x0": nx(seq[0] + 14), "y0": ny(ctl_y - 50), "x1": nx(seq[1] - 14), "y1": ny(ctl_y - 49)}

c = columns(*fx, 4)
fr1 = yb0 + MODULE_TITLE_H + 72
fr2 = fr1 + 140
for i, pid in enumerate(["delayTime", "delayFb", "delayMix", "width"]): circle("knobs", pid, c[i], fr1, 56)
circle("knobs", "spaceSize", c[0], fr2, 56); circle("knobs", "spaceMix", c[1], fr2, 56)
circle("knobs", "volume", c[3], fr2, 56)
out["meter"] = {"cx": nx(c[2]), "cy": ny(fr2 - 4), "w": nx(22), "h": ny(72)}

# ---- stile (tutto relativo all'altezza della finestra) ----
out["style"] = {
    "titleFont": ny(30), "taglineFont": ny(12), "versionFont": ny(12),
    "sectionFont": ny(12.5), "sectionTitleH": ny(MODULE_TITLE_H),
    "labelFont": ny(11.5), "labelGap": ny(8), "labelH": ny(16),
    "stepLabelFont": ny(11), "cornerRadius": ny(6), "screwRadius": ny(4.5), "screwInset": ny(10),
}

path = Path(__file__).resolve().parent.parent / "source" / "assets" / "layout.json"
path.write_text(json.dumps(out, indent=2) + "\n")
print(f"scritto {path}")
