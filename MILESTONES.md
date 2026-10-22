# StruckRoom milestones

Cloned from StruckSynth on 2026-10-22; M1-M4 below are StruckSynth's, inherited with its history.

## M5: The room
- [x] roomlab offline renderer and four experiments (tools/roomlab)
- [x] Room core + RoomModule per voice: direct, 4 reflections, 4 wall resonators, room modes, M/S pair
- [x] 29 host parameters (room, placement, mix, two wall pairs), presets, Poly 4 default, Width = M/S side
- [x] ss-room fuzz test; ctest 13/13; clap-validator 35 pass, 0 fail
- [ ] By hand: play it in a host and decide which controls matter
- [ ] GUI panels for the room (after the above)
- [ ] Rename leftovers: help page still describes StruckSynth

## Tests
- all: cmake --build build -j && ctest --test-dir build --output-on-failure

## M1: Core struck-resonator synth
- [x] Strike exciter: Mallet / Noise burst, hardness, velocity → level / hardness, contact mode
- [x] ModalBank: up to 64 modes, material presets, stretch, T60 decay, damp tilt+bell, strike position, brightness, stereo spread
- [x] PitchConverter + Slew (glide); poly/mono with legato note stack
- [x] Expression panel: Pressure→Decay, Timbre→Damp Tilt, Timbre→Strike Position
- [x] Factory presets baked in; DPF/NanoVG UI

## M2: MIDI 2.0 and MPE port
- [x] Raw CLAP entry point (DPF as GUI only, MIDI 2.0 dialect available)
- [x] Per-note UMP decode; note matching on note + channel + group; RC 7 bend range; Pitch 7.25
- [x] CLAP note ids; note expressions (tuning as Pitch 7.25; pressure/brightness decoded)
- [x] `CLAP_EVENT_NOTE_END` after ring-out; voice-info extension
- [x] MPE lower zone: RPN 0 bend range, released-note control freeze, MPE on/off switch
- [x] Pressure and Timbre (CC 74) as per-note modulation sources
- [x] MIDI 1.0→2.0 Min-Center-Max upscale; MIDI 2.0 velocity-0 note-on treated as note-on
- [x] All Notes Off (CC 123) and All Sound Off (CC 120), per channel
- [x] Linux CI (Ubuntu gcc + clang with -Werror)

## M3: VST3 / AUv2 via clap-wrapper
- [x] VST3 (macOS + Linux) and AUv2 (macOS, `aumu StRm nLKL`) through clap-wrapper v0.16.0, wrapping the same CLAP
- [x] VST3 note expressions: Tuning + Brightness offered via `CLAP_PLUGIN_AS_VST3` (Pressure as before)
- [x] clap-wrapper AUv2 GUI patch carried in `patches/` (calls `gui->show()`, which starts the render timer)
- [x] Validated on macOS (Xcode 26.3, Ninja, 2026-10-21): ctest 12/12; clap-validator 35 pass, 0 fail;
      auval passes; Steinberg's VST3 validator 47 pass, 0 fail (lists Tuning + Brightness)
- [ ] By hand: GUI, notes, automation and state recall in Logic (AUv2), REAPER (VST3 + AUv2), Bitwig (VST3);
      Bitwig per-note Tuning in the VST3
- [ ] Linux: VST3 build in CI; not yet loaded in a Linux host

## M4: Later
- [x] Pitch 7.25 as persistent per-key tuning table (CLAP tuning stays per-note)
- [ ] Move onto the DPF fork (erickampman/DPF) with own DGL_NAMESPACE
