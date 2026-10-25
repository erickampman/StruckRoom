# Testing StruckRoom

The project ships a suite of in-process self-tests (via CTest) plus the CLAP conformance
validator. Everything runs headless on Linux/macOS — no DAW or audio device required.

## Prerequisites

Everything [BUILDING.md](BUILDING.md) lists: the tools and libraries to install, and
`tools/fetch_deps.sh` for the submodules.

## Build and run all tests

```sh
tools/fetch_deps.sh
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
clap-validator validate build/StruckRoom.clap
ninja -C build install-auv2 && auval -v aumu StRm nLKL      # macOS: AU VALIDATION SUCCEEDED
```

Steinberg's VST3 validator builds from `external/vst3sdk` (`ninja validator` in a separate build of
that folder with `-DSMTG_ENABLE_VSTGUI_SUPPORT=OFF`); its CMake rejects an Xcode path with spaces, so
point `DEVELOPER_DIR` at a symlink without them. Then `validator build/StruckRoom.vst3`.

Add `-DSTRUCKROOM_WERROR=ON` at configure time to treat warnings as errors for the framework
and the `pm-selftest` glue (this is what CI keys on). The tests that pull in vendored
PolygraphModular headers stay on that code's own warning style.

## What each test covers

Eleven CTest cases exercise the `pm_framework` engine end-to-end; `ss-clap` drives the built plugin
itself, and `clap-validator` (below) checks CLAP conformance:

| CTest name       | Executable               | Verifies |
|------------------|--------------------------|----------|
| `pm`             | `pm-selftest`            | UMP MIDI layer: predicates (a MIDI 2.0 velocity-0 note-on is a note-on), raw-word decode, MIDI 1.0 → 2.0 Min-Center-Max scaling, microtonal pitch attribute, CLAP→UMP decoders |
| `pm-module`      | `pm-module-test`         | `Module` + `ModuleMap` core: param read/write, process proc, map add/get |
| `pm-sinosc`      | `pm-sinosc-test`         | The vendored sine oscillator produces an ~A440 sine |
| `pm-ifscore`     | `pm-ifscore-test`        | IFSCore's per-point smoothed eval matches its wavetable (the IFS modules inherited from IFSynth) |
| `pm-adsr`        | `pm-adsr-test`           | ADSR envelope: note-on drives attack, note-off drives release to ~0 |
| `pm-modules`     | `pm-modules-test`        | Combine / Final / Slew compile+run; PitchConverter maps note→pitch |
| `pm-factory`     | `pm-factory-test`        | ModuleFactory + the init-specs build all 21 registered modules |
| `pm-voice`       | `pm-voice-test`          | One full voice renders a note end-to-end and releases |
| `pm-modroute`    | `pm-modroute-test`       | Modulation routing at the voice level: connecting a mod source switches the destination's path |
| `pm-voicemanager`| `pm-voicemanager-test`   | Polyphony: a 3-note chord allocates 3 voices and frees them on release |
| `ss-bend`        | `ss-bend-test`           | MIDI 2.0 per-note decode; channel and per-note pitch bend (pitch measured on a single-mode bank); note matching on note + channel + group; per-note bend range (RC 7); Pitch 7.25, and as a per-key tuning table (sent before the note, kept per channel and through reset, cleared by Per-Note Management Reset, ignored by Pitch 7.9 note-ons and by CLAP tuning); CLAP note ids; CLAP note expression conversion; note lifetime (ring-out, mono stack, steal) for note end; MPE member range, master bend, RPN 0 (MIDI 1.0 and 2.0, kept across reset), MPE Configuration Message and switch-flip resets; velocity (MIDI 1.0 127 = MIDI 2.0 full scale; velocity-0 note-ons hold a voice); CC 123 / CC 120 per channel (pedal-held notes, tails cut, other channels' bends kept); Expression (Press>Decay ±2 oct stretches / damps a ringing note's T60 and eases back, 0 does nothing; Tmb>Tilt darkens a ringing tail; Tmb>Pos at the strike cancels the even mode, CC74 after the strike changes nothing); released notes (MPE member bend frozen at note-off, master still applies, pedal-held, MPE off follows, MIDI 2.0 per-note after note-off ignored, CLAP id-addressed still applies); pedal / all-notes-off / panic release notes on any channel |
| `ss-clap`        | `ss-clap-host-test`      | The built `.clap` over the CLAP C API: note-port dialects, params + state, 7.9-pitch notes, per-note and channel bend, RC 7, CLAP tuning expressions (with key, and by note id alone) reach the resonator; NOTE_END comes once, after the ring-out, for CLAP notes only; voice-info; the MPE switch is an Off/On enum that changes how a member bend reads; MIDI 1.0 velocity 127 strikes exactly as CLAP velocity 1.0; the Expression knobs default to 0 and a CLAP Pressure expression with Press>Decay keeps a note ringing; pitch is measured to a fraction of a sample |

## Running a single test

```sh
ctest --test-dir build -R pm-voice --output-on-failure   # by name (regex)
./build/pm-voice-test                                     # or run the binary directly
```

Each test binary prints diagnostic values (e.g. `Voice: peak=0.87 tail=0.0000`) and ends with
`ALL PASS`; CTest keys success on that string.

## CLAP conformance

Validate the built plugin against the CLAP spec with
[`clap-validator`](https://github.com/free-audio/clap-validator):

```sh
clap-validator validate build/StruckRoom.clap
```

## Continuous integration

`.github/workflows/ci.yml` runs the configure + build + `ctest` on every push across
Linux (gcc/clang) and macOS (clang) with `-DSTRUCKROOM_WERROR=ON`.
