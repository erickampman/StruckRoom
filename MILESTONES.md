# StruckRoom milestones

Cloned from StruckSynth on 2026-10-22; M1-M4 below are StruckSynth's, inherited with its history.

## M5: The room
- [x] roomlab offline renderer and four experiments (tools/roomlab)
- [x] Room core + RoomModule per voice: direct, 4 reflections, 4 wall resonators, room modes, M/S pair
- [x] 29 host parameters (room, placement, mix, two wall pairs), presets, Poly 4 default, Width = M/S side
- [x] ss-room fuzz test; ctest 13/13; clap-validator 35 pass, 0 fail
- [ ] By hand: play it in a host and decide which controls matter
- [x] Editor tabs: Object / Room (draggable map) / Walls (decay plot)
- [ ] By hand: check the tabs at several window sizes, Retina and non-Retina
- [ ] Rename leftovers: help page still describes StruckSynth
- [x] roomlab exp5: wall effects (frequency shift, flex, soft clip, rattle) rendered, measured and timed
- [x] Listened to exp5 (2026-10-25): all four go in, one knob each
- [x] Wall effects in the plugin: Shift, Flex, Rattle, Gap (Rattle's) per wall pair (8 params, appended),
      third row of the Walls tab. Shift Spread (the far wall's shift from opposite to the same way) was
      tried in Clip's slot and dropped: the direction barely changes the beating, and F/B barely at all.
      Clip was tried and dropped before release: walls don't clip, it read as distortion rather than a
      material, it barely followed velocity, and Flex already bends the reflection with level
- [x] Wall materials: the object's full list (26), generators included
- [x] By hand: the Walls tab's three rows checked (2026-10-25)
- [ ] By hand: listen to the wall effects in a host

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
- [x] Move onto the DPF fork (erickampman/DPF) with own DGL_NAMESPACE (2026-10-23)
