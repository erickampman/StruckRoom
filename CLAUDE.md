# StruckRoom

A CLAP instrument that blurs a struck instrument and a reverb: Strike → ModalBank → **Room** → Final
on the vendored PolygraphModular engine (`src/Common`, `src/Module`, `src/Voice`), a hand-written CLAP
entry point (`src/plugin.cpp`), and DPF used only for the GUI (`src/UI/`). Start with README.md and
TESTING.md.

**Cloned from StruckSynth on 2026-10-22** (its history is this repo's history up to `ddae6d2`), then
renamed: CLAP id `com.unlikelyware.struckroom`, AUv2 `aumu StRm nLKL`. Everything below about MIDI 2.0,
MPE, CLAP and the wrappers came across from StruckSynth unchanged, and so do its known follow-ups.

## The room (what is new here)

`src/Module/Room.hpp/.cpp` is a framework-free core, like ModalBank; `RoomModule` wraps it. One per
voice: a 2-D rectangular room whose lengths are in **wavelengths of the note**, so the whole scene
transposes with the pitch. Direct path, four first-order reflections (one-pole wall absorption), four
wall resonators (a ModalBank each), and up to 16384 room modes (standing waves), heard by a coincident
M/S pair. It is tuned at each note-on, never per block: bends and glides move only the object.
- Two stages: settings changes rebuild the mode list (throttled to 30 ms); a strike only recomputes
  coefficients. A retune carries each ringing mode's amplitude and phase over; skipping that blows up
  (a resonator's state read at a much lower frequency is a huge amplitude). Wall banks reset instead.
- Gain staging: the room's whole output is referenced to the direct path's spreading (placement moves
  the direct/room balance, not the loudness), the M/S decode is equal-power, and `kOutputTrim` 0.35
  leaves chord headroom. The engine's Final clamps each voice at 1, but voices sum after it, so
  `softKnee` in plugin.cpp (linear to 0.7, tanh above) rounds off coincident chord transients.
- **Detail** (room modes up to N × f0) is the CPU control. The mode loop is register-blocked (16 modes
  in vector registers for a whole chunk, accumulating into per-sample vectors in L1) and the strike
  retune uses branch-free polynomial sin/exp, so both vectorise. Measured on an M4, 48 kHz, 64-sample
  blocks, a 4-note chord struck every 2 s: Detail 12 8% of a core (worst block 0.19 ms), 21 24%
  (0.51 ms), 32 41% (0.86 ms of 1.33). The first version streamed every mode's arrays per sample and
  xrunned from Detail ~21 (79%, worst block 2.8 ms). Above ~28 the 16384-mode cap binds.
- The editor has three tabs (header): **Object** (StruckSynth's page, unchanged), **Room** (a
  `RoomMap` — drag the object and listener, which are Src/Mic X/Y — beside Room, Placement and Mix),
  **Walls** (both pairs, two rows of four via `GroupBox::setColumns`, over `RoomDecay`, every room
  mode's T60 at C4 from the engine's own formula). Tabs only show/hide widgets; all are built up front,
  before the PopupMenu, which must stay last to draw on top. Spread became Width (the M/S side level);
  the bank itself runs at Spread 0.
- `tools/roomlab/` is the offline renderer the room was ported from, with its README of findings.
- `test/ss_room_test.cpp` fuzzes the core (random settings, every range corner) for non-finite or
  runaway output. clap-validator's param-fuzz-bounds caught a 0/0 when source and mic share a wall.

Sibling synths, on the same engine and the same raw-CLAP layout. Paths: `~/Dev-Audio/<name>` on the Mac,
`~/dev/dev-clap/<name>` on the Linux PC (as `docs/BuildNotes.md` in IFSynth and StruckRoom shows):
- **StruckSynth** (`~/Dev-Audio/StruckSynth`, branch `main`), this plugin without the room
- **PlainOldSynth** (`~/Dev-Audio/PlainOldSynth`, branch `main`), where most MIDI 2.0 work starts
- **IFSynth** (`~/Dev-Audio/IFSynth`, branch **`master`**), ported from StruckSynth on 2026-10-12

## Working with the author

- Commit to `main` directly. Never open pull requests. Ask before pushing.
- Ask before anything UI-visible: new parameters, panel controls, dropdown entries.
- Parameters are **appended**, never inserted: CLAP parameter ids are the indexes. Each new one needs
  its `symbolIs` check in `plugin/StruckRoomParams.h`. Sound parameters get a preset key and a line in
  every `Presets/*.json`, added as text; controller setup (MPE) stays out of presets.
- Before each commit: `ctest --test-dir build` and `clap-validator validate build/StruckRoom.clap`.
- After a rebuild the author must **quit and restart the host** (StageHand, REAPER, Bitwig): a running
  host keeps the binary it first loaded. Say so when asking them to listen.

## Cross-project policy

- **MPE**: the synths accept MPE as a compatibility input (an Off/On switch, lower zone) and never send
  it. Nothing in StageHand, Umpire or KeyboardEDO originates MPE. MIDI 2.0 per-note messages and CLAP
  note expressions are the preferred path.
- MIDI 2.0 spec on the Mac: `~/Documents/MIDI/MIDI_2-0_Specification_Version_1-0 2/` (M2-104 is v1.0,
  Feb 2020; registered controller 7 is from a later revision and not in it).

## The MIDI 2.0 / MPE / CLAP port (2026-10-11): done

PlainOldSynth's changes of 2026-10-09..11, brought here one commit each, in this order:
`53e642f` raw CLAP entry point · `a256ccc` kParams order guard · `4ed5f93` Retina / multi-monitor
editor size · `586cee7` per-note bend, registered controller 7, note + channel + group matching ·
`7baf056` per-note bend range = the channel's · `c32681d` CLAP note ids and note expressions, Pitch
7.25 · `3cf2f03` NOTE_END + voice-info · `d7258a2` MPE + RPN 0 · `76d53ca` MIDI 1.0 → 2.0 scaling,
velocity-0 note-on · `30a1ca6` All Notes Off / All Sound Off · `b43e91b` released notes (MPE §3.3.3) ·
`72b75ae` Expression panel.

Where StruckRoom differs from PlainOldSynth:
- No VCA: the bank's T60 decay is the envelope, so a voice rings on after note-off and NOTE_END comes
  when it falls silent.
- Bend enters ModalBank through an appended second pitch input (`ModalBankParamItemPitchMod2Input`),
  after the Slew. There is no `Module::legatoContinuation` here, so the bend snaps on every note-on.
- No Velocity loudness dropdown: Strike already does velocity → level and hardness.
- Pressure and timbre (item 3) are a fixed Expression panel, the author's choice: Press > Decay,
  Tmb > Tilt, Tmb > Pos, with PlainOldSynth's ChannelPressure and CCConverter vendored as sources.

Known follow-up: `state_load` (in `src/plugin.cpp`) calls `applyAllParams` on the main thread, which
can rewire the voice graph under a running `process()`. IFSynth (`9cab332`) only stores the values
there and lets the next block apply them; port that.

## VST3 and AUv2 (2026-10-21)

Both come from clap-wrapper (`external/clap-wrapper`, v0.16.0) wrapping the same CLAP, following
`~/Dev-Tools/lockstep-git/SYNTH_WRAPPERS.md` and PlainOldSynth. AUv2 is `aumu StRm nLKL`.
`patches/clap-wrapper--auv2-gui-show.patch` makes the AUv2 view call `gui->show()`, which starts the
render timer; without it the AU editor stays blank. `kAsVst3Ext` in `src/plugin.cpp` offers VST3 hosts
Tuning and Brightness note expressions. Before each commit, also `auval -v aumu StRm nLKL` after
`ninja -C build install-auv2`.

## On Linux

The Mac is where this was developed; on Linux only CI has built it. Its ubuntu/gcc job failed from the
raw-CLAP commit until `5bf099a`, which builds every static library with `-fPIC`. Since then CI is green
on ubuntu gcc and clang, including the `ss-clap` host test, which loads the built `.clap` and plays it
headless; so the plugin loads and sounds right on Linux, but nobody has seen its editor.

A checkout from before 2026-10-11 is far behind. To catch up:

1. `git pull` and `tools/fetch_deps.sh` (pinned submodules, only four of the VST3 SDK's, and the
   clap-wrapper patch in `patches/`).
2. **Configure a fresh build directory** (`rm -rf build`): the move off `dpf_add_plugin` renamed the
   targets and moved the output to `build/StruckRoom.clap`. Then
   `cmake --build build --target install-clap` re-points `~/.clap/StruckRoom.clap` at it (it is a
   separate target, so an ordinary build never writes outside the build tree).
3. Packages: README's Build section (X11 and GL development headers). If configure or link asks for
   more, PlainOldSynth's README lists the full set its Linux CI installs.
4. `ctest --test-dir build --output-on-failure`, then `clap-validator validate build/StruckRoom.clap`.
5. The VST3 is `build/Release/StruckRoom.vst3` on Linux; `install-vst3` links it into `~/.vst3`.

Never checked on Linux, so check these by hand and record what you find in this file:
- The editor under X11 through the raw CLAP gui extension (`CLAP_WINDOW_API_X11` in
  `src/UI/StruckRoomUI.cpp`): correct size on load, resizing, clicks landing on the right controls.
- The Presets menu and the logo link.
- Sound and MIDI in **StageHand** (`~/dev/dev-audio/StageHand`; Linux-first: JACK through PipeWire, the
  ALSA sequencer) and REAPER.

macOS-only pieces: `src/UI/BackingScale_mac.mm` (Retina scale; built only on Apple).

**NanoVG stencil (seen on Linux, 2026-10-16).** NanoVG fills non-convex paths through the stencil
buffer and assumes it starts at zero, but DPF's `puglOnDisplayPrepare` clears only colour and depth.
On Linux the stale stencil showed through as a speckled pattern across PlainOldSynth's envelope
graphs (fixed there in `bfaa5e5`, `4ea736c`: `EnvelopeGraph::onNanoDisplay` sets `glStencilMask(0xff)`
and clears the stencil before drawing); ModicumFM hit the same thing (`5d36246`, cleared in its
top-level `onNanoDisplay`). This synth has no non-convex fills today, so it is unaffected, but any
new widget that fills a non-convex shape (a filled waveform, a polygon) needs the same clear. Convex
fills (rects, rounded rects, circles, triangles) and plain strokes don't use the stencil.

Test MIDI sources: mpe-sender (`~/Dev-MIDI/mpe-sender` on the Mac) is macOS-only (CoreMIDI), and KeyboardEDO has no Linux MIDI
output until uw-core's ALSA output (its roadmap milestone 3b). On Linux use a hardware controller,
StageHand, or the headless `ss-clap` and `ss-bend` tests, which send MIDI 2.0, MPE and CLAP events.
