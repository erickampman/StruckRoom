# Contributing to StruckRoom

Bug reports and pull requests are welcome. Build steps are in [BUILDING.md](BUILDING.md) and the tests
in [TESTING.md](TESTING.md); this page is the rules the code depends on.

StruckRoom began as a copy of StruckSynth (2026-10-22; their history is shared up to `ddae6d2`): a
struck object (Strike → ModalBank) played into a **Room**, then Final. Everything about MIDI 2.0, MPE,
CLAP and the wrappers is StruckSynth's.

## Before each commit

```sh
ctest --test-dir build --output-on-failure
clap-validator validate build/StruckRoom.clap
```

The tests are the `pm-*` engine tests, `ss-room`, `ss-bend` and `ss-clap`. `ss-room` fuzzes the room
core (random settings, every range corner) for non-finite or runaway output. `ss-clap` loads the built
`.clap` and plays it headless, sending MIDI 2.0, MPE and CLAP events itself, so it needs no MIDI
hardware. They are silent. On macOS, if you touched the AUv2 build, also run
`cmake --build build --target install-auv2` and `auval -v aumu StRm nLKL`.

After rebuilding, quit and restart the host before listening: a running host keeps the plugin binary
it loaded first.

## Parameters: append, never insert

The CLAP parameter id is the parameter's index, and hosts save sessions and automation by id. So a
new parameter goes at the **end** of the list, and existing ones are never reordered or removed: that
would silently rewire everyone's saved projects. For each new parameter:
- add it to the `symbolIs` order guard in `plugin/StruckRoomParams.h`;
- if it shapes the sound, give it a preset key (`src/UI/PresetKeys.hpp`) and add a line for it to
  every `Presets/*.json`, edited as text so the files aren't reformatted.

MIDI controller setup (MPE on/off and the like) stays out of presets.

## MIDI 2.0 and MPE

MIDI 2.0 per-note messages and CLAP note expressions are the main path. MPE is accepted as a
compatibility input (an Off/On switch, lower zone); nothing in this family of projects sends MPE.

## Threads

`state_load` runs on the main thread while `process()` may be running. It must store the loaded
values only and let the audio thread apply them at the start of the next block (or `activate()`), as
StruckSynth's does. Never rebuild the voice graph from the main thread. Not yet true here:
`state_load` still calls `applyAllParams` directly (ReleaseUMP R1-31).

## The room

`src/Module/Room.hpp/.cpp` is a framework-free core, like ModalBank; `RoomModule` wraps it, one per
voice. The room's lengths are in wavelengths of the note, so the scene transposes with the pitch.
- **Tuned at note-on, never per block.** Bends and glides move only the object. A settings change
  rebuilds the mode list (throttled to 30 ms); a strike only recomputes coefficients.
- **A retune carries each ringing mode's amplitude and phase over.** Skipping that blows up: a
  resonator's state read at a much lower frequency is a huge amplitude. The wall banks reset instead.
- **Gain staging.** The room's output is referenced to the direct path's spreading (placement moves
  the direct/room balance, not the loudness), the M/S decode is equal-power, and `kOutputTrim` leaves
  chord headroom. Voices sum after Final's per-voice clamp, so `softKnee` in `src/plugin.cpp` rounds
  off coincident chord transients.
- **Detail is the CPU control.** The mode loop is register-blocked and the strike retune uses
  branch-free polynomial sin/exp, so both vectorise. On x86-64 both are built twice (`ROOM_HOT` in
  `Room.cpp`: `target_clones` for x86-64-v3 and the baseline), because plain x86-64's 16 SSE registers
  spill the loop's 8-wide vectors. Under clang, `ROOM_HOT` goes on every declaration (header and
  definition), and no plain function in the clones may return a `v8` by value (an AVX ABI change in
  the baseline clone: gcc warns, clang rejects it).
- **The editor's tabs** (Object, Room, Walls) only show and hide widgets. All are built up front,
  before the PopupMenu, which must stay last so it draws on top.
- `tools/roomlab/` is the offline renderer the room was ported from, with its README of findings.

## Dependencies

The submodules are pinned (`tools/fetch_deps.sh`). Don't commit changes inside them. A fix needed in
clap-wrapper goes in `patches/` as a patch file, which `fetch_deps.sh` applies, so `external/clap-wrapper`
showing a local change is expected. DPF and pugl are Unlikelyware's forks; changes go to those repos
first and come here as a new pin.

## Drawing (NanoVG)

NanoVG fills non-convex paths through the stencil buffer and assumes it starts at zero, but DPF clears
only colour and depth. On Linux a stale stencil shows as speckles. Any widget that fills a non-convex
shape (a filled waveform, a polygon) must set `glStencilMask(0xff)` and clear the stencil before
drawing. No widget here fills a non-convex shape today. Convex fills (rectangles, rounded rectangles,
circles) and strokes are unaffected.

## Rules learned the hard way

- **Panels before their controls.** Declare each `GroupBox` member before the controls it parents.
  Members are destroyed in reverse order, so a panel freed first crashes its children's destructors
  when the editor closes. The host test only calls the GUI's `create()`, so only a real host catches
  this.
- **Every sound parameter in every preset.** The preset loader leaves keys a preset doesn't mention
  alone, so a missing key keeps the previous preset's value.
- **Output events stay in time order** within a block.
- **Controller setup survives `reset()`.** Configuration a controller sends once (RPN 0, registered
  controller 7) must outlive a reset: some hosts reset on transport stop.
- **Test helpers:** a full-scale bend computes 2³², which wraps to 0 in a `uint32_t`; clamp it.

## Platform-specific code

- `src/UI/BackingScale_mac.mm` (Retina scale) and the AUv2 target are macOS-only.
- There is no VCA: the modal bank's T60 decay is the envelope, so a voice rings on after note-off and
  sends `CLAP_EVENT_NOTE_END` when it falls silent.

## Layout

The engine (`src/`) mirrors PolygraphModular's structure; the CLAP plugin + GUI live in `src/`:

```
plugin/               the shared plugin definition
                        DistrhoPluginInfo.h (UI flags; the plugin identity macros are inert),
                        StruckRoomParams.h (shared param table + dropdown labels)
src/plugin.cpp        the raw CLAP plugin: entry point, note/audio/params/state/gui extensions,
                        and the DSP glue (MIDI + params → VoiceManager)
src/plugin_gui.h      the C seam between plugin.cpp and the GUI shell
src/UI/               the plugin GUI (reusable NanoVG widgets + the panel)
                        Knob, Slider, Toggle, Dropdown, PopupMenu, TabBar, GroupBox, TitleText,
                        Logo; PresetKeys / PresetDump (preset menu + dev-only JSON dumper);
                        StruckRoomUI.cpp — composes the controls into labelled groups and hosts
                        them through DPF's UIExporter
src/Common/           shim + MIDI + params + utilities
                        pm_compat.h (Apple→portable), pm_midi.* (spec UMP + MIDI-1 decode),
                        clap_ump.h (CLAP↔UMP), pm_params.h / pm_param_data.* / pm_addresses.h,
                        ParamAddressMacros.h, CPPTypes.hpp, CPPProcs.hpp, KPBuffPairHelper.hpp
src/Module/           Module core + modules
                        StrikeModule.*, ModalBank.* / ModalBankModule.* (+ ModalMaterials,
                        ModalGenerators, ModalDiscretizer), Slew, PitchConverter, Final; the
                        IFS / Sine / ADSR / LPF / CVMixer / Level / Combine modules inherited
                        from IFSynth stay instantiated but unconnected
src/Voice/            voice engine
                        Voice.*, VoiceStateManager.*, VoiceManager.*, ModuleMap.*, ModuleGraph.hpp,
                        ModuleInitSpecs.hpp, pm_module_specs.cpp, ConnectionCommandQueue.hpp
Presets/              factory presets (JSON), compiled into GeneratedPresets.h by
                        tools/gen_presets.py
Resources/            font + logo, embedded into the binary at build time
test/                 in-process self-tests (see TESTING.md)
dpf/                  DISTRHO Plugin Framework (submodule, + nested pugl) — the GUI layer only
external/clap         CLAP headers (submodule, pinned 1.2.9) — the plugin and the tests
external/clap-wrapper free-audio's clap-wrapper (submodule, v0.16.0) — the VST3 and AUv2 builds
external/vst3sdk      Steinberg VST3 SDK (submodule; only base, cmake, pluginterfaces, public.sdk)
patches/              local patches to the submodules, applied by tools/apply_dep_patches.sh
```

Vendored PolygraphModular files keep their original `#include "X.hpp"` form; every framework
folder is on the include path so they resolve across `Common/` `Module/` `Voice/`. The plugin
links the `pm_framework` static lib; DPF's `UIExporter` hosts the editor inside the CLAP gui
extension.
