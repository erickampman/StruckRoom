"""Render the StruckRoom experiments with roomlab.

Each experiment is a sweep with one thing changing. Every file in a sweep gets
the SAME gain (peak of the loudest at -1 dBFS), so level differences between
files are real and A/B comparisons are fair. Writes 24-bit WAVs and one
spectrogram sheet per sweep into renders/.

    python3 -I tools/experiments.py            # all
    python3 -I tools/experiments.py 2 3        # just some
"""
import os
import subprocess
import sys
import time

import numpy as np
from scipy.io import wavfile
from scipy.signal import spectrogram
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt

HERE = os.path.dirname(os.path.abspath(__file__))
ROOMLAB = os.path.join(HERE, "build", "roomlab")
OUT = os.path.join(HERE, "renders")

# A medium room, the object a quarter of the way in, mic opposite.
BASE = dict(seconds=4)
OBJ_ONLY = dict(**{"mix.early": 0, "mix.late": 0})

EXPERIMENTS = {
    "1": ("exp1_room_size", "Room size, in wavelengths of the note", [
        ("00_object_alone", OBJ_ONLY),
        ("01_w0.5", {"room.w": 0.5, "room.d": 0.37}),
        ("02_w1",   {"room.w": 1,   "room.d": 0.73}),
        ("03_w2",   {"room.w": 2,   "room.d": 1.46}),
        ("04_w4",   {"room.w": 4,   "room.d": 2.92}),
        ("05_w8",   {"room.w": 8,   "room.d": 5.84}),
    ]),
    "2": ("exp2_placement", "Object placement (room 3 x 2.2); b = standing waves alone", [
        ("01_centre",      {"src.x": 0.5,  "src.y": 0.5}),
        ("02_side_wall",   {"src.x": 0.03, "src.y": 0.5}),
        ("03_corner",      {"src.x": 0.03, "src.y": 0.03}),
        ("01b_centre_late",    {"src.x": 0.5,  "src.y": 0.5,  "mix.direct": 0, "mix.early": 0}),
        ("02b_side_wall_late", {"src.x": 0.03, "src.y": 0.5,  "mix.direct": 0, "mix.early": 0}),
        ("03b_corner_late",    {"src.x": 0.03, "src.y": 0.03, "mix.direct": 0, "mix.early": 0}),
    ]),
    "3": ("exp3_walls", "Walls: absorb and ring", [
        ("01_mirror",         {"walls.absorb": 0,   "walls.ring": 0, "seconds": 6}),
        ("02_absorber",       {"walls.absorb": 0.5, "walls.tilt": 1, "walls.ring": 0, "seconds": 6}),
        ("03_ringer",         {"walls.absorb": 0,   "walls.ring": 1, "seconds": 6}),
        ("04_absorb_ring_linked", {"walls.absorb": 0.5, "walls.tilt": 1, "walls.ring": 1,
                                   "walls.link": 1, "seconds": 6}),
        ("05_absorb_ring_free",   {"walls.absorb": 0.5, "walls.tilt": 1, "walls.ring": 1, "seconds": 6}),
    ]),
    "4a": ("exp4a_pitch_similitude", "Everything scales, decays too (room.track 1, decay.track 1)", [
        ("01_C2", {"note": 36, "seconds": 9, "decay.track": 1}),
        ("02_C4", {"note": 60, "seconds": 4, "decay.track": 1}),
        ("03_C6", {"note": 84, "seconds": 4, "decay.track": 1}),
    ]),
    "4b": ("exp4b_pitch_decay_fixed", "Room scales, decays in seconds as in StruckSynth (decay.track 0)", [
        ("01_C2", {"note": 36}),
        ("02_C4", {"note": 60}),
        ("03_C6", {"note": 84}),
    ]),
    "4c": ("exp4c_pitch_fixed_room", "Room stays put; only the object scales (room.track 0)", [
        ("01_C2", {"note": 36, "room.track": 0}),
        ("02_C4", {"note": 60, "room.track": 0}),
        ("03_C6", {"note": 84, "room.track": 0}),
    ]),
}


def render(path, params):
    args = [ROOMLAB, path] + [f"{k}={v}" for k, v in {**BASE, **params}.items()]
    t = time.time()
    r = subprocess.run(args, capture_output=True, text=True)
    if r.returncode:
        sys.exit(r.stderr)
    return time.time() - t, r.stderr.strip().splitlines()


def run(key):
    folder, title, variants = EXPERIMENTS[key]
    d = os.path.join(OUT, folder)
    os.makedirs(d, exist_ok=True)
    print(f"== {folder}: {title}")
    raws = []
    for name, params in variants:
        tmp = os.path.join(d, f".{name}.f32.wav")
        secs, log = render(tmp, params)
        fs, x = wavfile.read(tmp)
        os.remove(tmp)
        info = next((l for l in log if l.startswith("room")), "")
        print(f"  {name:28s} {secs:5.1f}s  {info}")
        raws.append((name, fs, x.astype(np.float64)))

    peak = max(np.abs(x).max() for _, _, x in raws)
    gain = 10 ** (-1 / 20) / peak
    for name, fs, x in raws:
        y = np.clip(x * gain, -1, 1)
        wavfile.write(os.path.join(d, f"{name}.wav"), fs, (y * (2**31 - 1)).astype(np.int32))

    fig, axes = plt.subplots(len(raws), 1, figsize=(10, 2.3 * len(raws)), sharex=True)
    for ax, (name, fs, x) in zip(np.atleast_1d(axes), raws):
        f, t, S = spectrogram(x.mean(axis=1) * gain, fs, nperseg=4096, noverlap=3072)
        ax.pcolormesh(t, f, 10 * np.log10(S + 1e-14), vmin=-130, vmax=-30, shading="auto", cmap="magma")
        ax.set_yscale("symlog", linthresh=200)
        ax.set_ylim(40, 16000)
        ax.set_ylabel("Hz")
        ax.set_title(name, fontsize=9, loc="left")
    np.atleast_1d(axes)[-1].set_xlabel("seconds")
    fig.suptitle(title)
    fig.tight_layout()
    fig.savefig(os.path.join(d, "spectrograms.png"), dpi=90)
    plt.close(fig)


if __name__ == "__main__":
    keys = sys.argv[1:] or list(EXPERIMENTS)
    keys = [k for k in EXPERIMENTS if any(k == a or k.startswith(a) for a in keys)]
    for k in keys:
        run(k)
