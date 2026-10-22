# roomlab

The offline renderer StruckRoom's room was prototyped in (2026-10-22, before the plugin existed):
a struck object in a "room" that scales with the note. The plugin's `src/Module/Room.cpp` is the
real-time port; this stays as a quick way to render sweeps and look at spectra.

`roomlab` renders one struck note through the scene and writes a float WAV. The object is
the plugin's own `ModalBank`, compiled from `../../src/Module` in place, so the object here is the
object the plugin plays.

## Build and run

From this folder:

```bash
cmake -S . -B build -G Ninja && ninja -C build
```

```bash
python3 -I experiments.py
```

That renders every sweep into `renders/<experiment>/`: 24-bit WAVs and a `spectrograms.png`. Give
`experiments.py` an experiment number (`2`, `4`, `4b`) to render only that. Within a sweep every file
gets the **same** gain, so loudness differences are real. `stats.py file.wav ...` prints peak,
RMS, a T60 estimate and L/R correlation.

One-off renders: `./build/roomlab out.wav key=value ...` (`--help` lists the keys).

## The scene

All lengths are in **wavelengths of the note** (λ0 = c/f0), so the same numbers describe the same
scene at any pitch. The room is 2-D: two wall pairs, side walls (x) and front/back (y).

```
strike -> object (ModalBank) -> direct path            delay, 1/sqrt(r)
                             -> 4 first-order images    delay, sqrt(1 - alpha(f)) FIR per wall pair
                             -> 4 wall resonators       ModalBank per wall, struck where the path meets it
                             -> room modes (nx, ny)     f = f0/2 * sqrt((nx/W)^2 + (ny/D)^2)
```

| Control | Keys | What it does |
|---|---|---|
| Object | `obj.mat obj.t60 obj.tilt obj.pos obj.bright obj.hardness` | the plugin's ModalBank and a note-relative strike |
| Room | `room.w room.d` | size in wavelengths at note 60 |
| Placement | `src.x src.y mic.x mic.y` | 0..1 of the room. A room mode's gain is cos(nx·π·x)·cos(ny·π·y) at the source times the same at the mic: Strike Pos, one level up |
| Wall absorb | `side.absorb side.tilt` (`fb.*`, `walls.*` for both) | α(f) = absorb·(f/f0)^tilt. Filters each reflection **and** sets each room mode's T60 (a mode loses α at every wall hit; its angle sets how often it hits each pair) |
| Wall ring | `side.ring side.ring.mat side.ring.ratio side.ring.t60 side.ring.tilt` | a ModalBank in the wall; `ratio` = wall fundamental / note |
| Energy link | `side.link=1` | the wall rings with the energy it absorbed, sqrt(α(f)), not with the full incident sound |
| Pitch tracking | `room.track` (1 = room scales with the note, 0 = fixed physical room), `decay.track` (1 = T60s scale as 1/f0 too, 0 = seconds, as in StruckSynth) | |
| Listener | `mic.width` | coincident Mid/Side pair: omni + figure-8 along x. Coincident because a spaced pair would be a different number of wavelengths apart on every note |
| Mix | `mix.direct mix.early mix.late mix.ring` | |

## Experiments

1. **Room size**, 0.5 → 8 wavelengths, plus the object alone.
2. **Placement**: object at the centre, at a side wall, in a corner. The `b` files are the standing
   waves alone (no direct or early sound). The centre kills every odd mode: 5,391 modes against the
   corner's 20,985.
3. **Walls**: mirror, absorber, ringer, absorber + ringer (linked and free).
4. **Pitch**, C2 / C4 / C6: (a) everything scales, decays included; (b) the room scales, decays stay
   in seconds; (c) the room stays put and only the object scales.

## First observations (from the measurements, before anyone has listened)

- **Full similitude (4a) is tape-speed transposition.** C2's scene lasts 16× as long as C6's. Every
  room time scales with the note, and nothing else in the model can tell the notes apart.
- **When room dimensions are whole or half wavelengths, the room is harmonic.** Axial modes sit at
  f0·n/(2W), so at W = 2 a mode lands exactly on the fundamental and the room sings along with the
  note. Rational dimensions make a harmonic room and irrational ones an inharmonic room. That is a
  "material" for the room, like the object's.
- **A standing wave can put the mic at a pressure node.** In exp1 `03_w2` the mic sits at a pressure
  node and a velocity antinode of that on-pitch mode, so the figure-8 dominates and L/R come out nearly
  inverted (correlation −0.82). That is real physics, but it sounds phasey; move `mic.x` or lower
  `mic.width` to hear the other side of it.
- **A mirror room (absorb 0) rings at every mode for the air's 12 s**, including modes below f0 that
  the object's resonance skirts excite.

## Simplifications to know about

- **The room-mode sum and the image sources overlap.** A full modal sum would contain the direct sound
  and every reflection. Here the modes are driven from the direct sound's arrival time and stand for
  the late field, with the early paths added on top; `mix.*` balances them.
- **Only first-order reflections.** Higher orders exist only through the modes.
- **Wall resonators radiate to the listener but don't feed the room modes.**
- **Room modes stop at `room.maxharm` × f0** (default 32), capped at 0.45·fs. A pitch-relative cap keeps
  the mode count, and the cost, the same on every note (about 21k modes for the default room, ~0.1 s per
  4 s render on 14 cores). C6 hits the Nyquist cap, so its room is thinner.
- **`kLateGain` and `kRingGain` are calibrated by measurement**, so that at the default scene each path
  alone lands near the direct sound's RMS. They are not derived from physical constants.
- **Delays use linear interpolation; reflection filters are 511-tap linear-phase FIRs**, which are
  coarse below about 100 Hz.

## Where the plugin differs

The real-time port in `src/Module/Room.cpp` keeps the model and changes these:

- **Wall absorption is a one-pole**, |R|² = (1 − absorb) / (1 + (f/fc)²) with fc = f0·2^Bright, not
  `absorb·(f/f0)^tilt` through a 511-tap FIR. The room modes' T60s use the same formula.
- **The late level is normalised by √area, not 1/area**, so Late means the same at every room size;
  the two agree at this README's default 3 × 2.2 room.
- **Modes below 20 Hz are silent**, and **Detail defaults to 12 × f0** (here 32) for CPU.
- **The room is tuned at each strike** and carries ringing modes over to the new pitch.
