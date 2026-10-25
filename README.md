# StruckRoom

A cross-platform **CLAP instrument** from Unlikelyware that blurs the line between a struck
instrument and a reverb. It is StruckSynth (a struck object: a shaped excitation burst into a bank
of tuned resonant modes) placed inside a **room that scales with the note**. Every length in the
room is measured in wavelengths of the note being played, so the object, the walls, where the
object sits and the standing waves all transpose together, the way the object itself already did.

## The room

Each voice is **Strike → ModalBank (the object) → Room → Final**. The room is two-dimensional, with
two wall pairs (side walls and front/back), and it is heard from one point by a coincident
Mid/Side pair:

- **Direct sound**, delayed by the distance from the object to the listener.
- **Four first-order reflections**, one off each wall, filtered by that wall's absorption.
- **Four wall resonators**: a wall can ring like a struck object of its own, struck where the
  sound meets it.
- **Room modes**, the standing waves: thousands of resonators at
  f = f0/2 · √((nx/W)² + (ny/D)²). Where the object and listener sit decides which ones are driven
  and heard (a mode is silent at its nodes, as Strike Pos does for the object). Their decay comes
  from the walls' absorption.

The room is tuned at each strike and then left alone: pitch bend and glide move the object, not the
room.

| Group | Parameters | Notes |
|---|---|---|
| Room | Room W, Room D | size in wavelengths (at note 60 when Room Track is below 1) |
| | Room Track | 1: the room scales with the note; 0: a fixed physical room, so notes fit more wavelengths as they rise |
| | Decay Track | 1: every T60 (object, walls, air) scales as 1/f0, so the whole scene transposes like tape speed; 0: decays stay in seconds |
| | Air | the air's T60, which keeps a perfectly reflective room finite |
| | Detail | room modes up to this multiple of f0: realism against CPU (default 12) |
| Placement | Src X/Y, Mic X/Y | where the object and the listener sit, 0..1 of the room |
| Mix | Direct, Early, Late | the object's own sound, the reflections, the standing waves. Direct 1, Early 0, Late 0 is plain StruckSynth |
| Walls (Side, F/B) | Absorb, Bright | broadband absorption and the high-frequency corner (octaves above f0). They filter the reflections and set the room modes' decay |
| | Ring, Mat, Ratio, Decay, Damp | the wall as a resonator: level (0 = doesn't ring), material (the object's list, generators included), fundamental relative to the note, T60, damping tilt |
| | Link | On: the wall rings with the energy it absorbed, not the full incident sound |
| | Shift | frequency shift of the reflections and the wall's ring, % of f0. The near wall shifts up and the far wall down, so the two sides drift against each other. Small values beat slowly; 10% clangs |
| | Flex | the wall gives under the sound, so its reflection's path, and pitch, bend with the level: soft notes barely, loud ones more |
| | Rattle, Gap | the wall resonator buzzes once it rings past the gap (needs Ring above 0), and stops as the ring decays inside it. Gap is a share of a full-velocity ring: lower and soft notes rattle too, higher and only the loudest hits do |
| Global | Width | the M/S pair's side level (was Spread) |

The editor has three tabs. **Object** is the struck object, as in StruckSynth. **Room** shows the
room from above: drag the object and the listener around it, beside the Room, Placement and Mix
panels. **Walls** has the two wall pairs side by side over a plot of every room mode's decay against
frequency, coloured by which pair of walls it mostly reflects from. `tools/roomlab/` is the offline renderer the room was
prototyped in, with notes on what its first experiments showed.

**CPU.** Every voice carries its own room, so voices are expensive: **Poly 4** is the default
(Mono, Poly 10, 20 and 32 are still there). Measured on an M4 at 48 kHz with 64-sample buffers, a
4-note chord costs about 8% of a core at Detail 12, 24% at Detail 21 and 41% at Detail 32.

## Status

**Polyphonic struck-resonator synth in a pitch-scaled room; the custom GUI covers the object.** The
PM framework drives a `VoiceManager` (polyphony, note-stealing, mono/legato) rendering the voice
**Strike → ModalBank → Room → Final**, with pitch tracking via **PitchConverter → Slew** (glide) into
the bank's pitch input. There is no VCA envelope: the bank's own T60 decay is the amplitude
envelope, and a voice frees itself once it rings down to silence, so the tail survives
note-off.

- **Strike** (exciter): Mallet or Noise burst, hardness, velocity → level / hardness, contact
  mode. MIDI 1.0 values are scaled to MIDI 2.0 with the spec's Min-Center-Max method, so velocity
  127 is full scale (as are CC 127 and a full pitch bend), and 64 / 8192 the exact centre. A MIDI 2.0
  note-on with velocity 0 is a note-on, as the spec says; Vel>Level decides how loud it is.
- **ModalBank** (resonator): up to 64 modes, material presets, stretch, decay (T60), damping
  tilt + bell, strike position, brightness, stereo spread.
- **Voices**: Poly 4 (default), Poly 10, Poly 20, Poly 32, or Mono (legato, with glide).
- **Presets**: factory presets in `Presets/*.json`, baked into the plugin at build time.

MIDI notes are sample-accurate: `process()` splits rendering at each event's frame offset.
Parameter changes are applied per host block, and the note port advertises (and prefers) the MIDI
2.0 dialect, so a 7.9-pitch NoteOn reaches the resonator unchanged.

### Pitch bend: channel and per-note

Every voice has a `PitchBendConverter` (`src/Module/PitchBendConverterModule.cpp`, ported from
PlainOldSynth) feeding ModalBank's second pitch input. That input comes **after** the Slew, so glide
never smears a bend, and it is scaled on its own, so a fixed-pitch body (Pitch Mod Level 0) still
bends. It sums two bends:

- **Channel pitch bend** (MIDI 1.0 or 2.0), **±2 semitones** by default. Each voice remembers the
  latest bend of every channel and applies the one its own note was played on: ordinary pitch bend
  with everything on one channel, and per-note bend when each note has its own channel. **RPN 0**
  (pitch bend sensitivity, as MIDI 1.0 CC 101/100/6/38 or a MIDI 2.0 registered controller) sets a
  channel's range.
- **MIDI 2.0 per-note pitch bend** (UMP status `0x6`). It bends only the note it names, over the same
  range as its channel's bend (**±2** by default): M2-104 v1.0 says per-note bend "acts like Pitch
  Bend in every way" and gives it no range of its own. A controller can set a separate per-note range
  per channel with MIDI 2.0 **registered controller 7** (per-note pitch bend sensitivity, from a later
  revision of M2-104), as KeyboardEDO does; that range survives a host `reset()`, since it is
  configuration the controller sends once.

Bend changes ramp over 2 ms, and a note starts at its bent pitch. ModalBank retunes its modes once per
block without resetting them, so a bend glides the ringing tail rather than re-striking it. Per-note
bend resets on every note-on and on a per-note management Reset; one sent *before* its note-on is
dropped, because no voice owns that note yet. The default range is not a host parameter.

### MPE

The **MPE** switch (Global panel, off by default) treats input as an MPE lower zone: master channel
1, member channels 2–16. MPE is accepted as an input only; StruckRoom never sends it.

- A member channel's bend range defaults to **±48**, and the master channel's bend (±2) is **added**
  to every member note, so the controller's pitch wheel moves the whole zone on top of each note's
  own slide.
- RPN 0 on any member channel sets every member channel's range, as MPE specifies.
- **Released notes keep their pitch.** MPE §3.3.3: "control of a note ceases once Note Off has
  occurred". A struck note rings for seconds after its note-off, so when a sender centres the member
  channel for its next note, the tail stays where it was; only the master channel's bend still moves
  it. A pedal-held note counts as released. With MPE off, an ordinary pitch wheel still bends
  releasing notes. The same goes for MIDI 2.0 per-note messages: a released note stops taking them,
  except CLAP expressions addressed by note id, which last until the note's `CLAP_EVENT_NOTE_END`.
- Ranges set by RPN 0 go back to their defaults on an MPE Configuration Message (RPN 6 on channel 1
  or 16; only its reset is honoured, the zone layout is the switch's) and when the switch is flipped,
  so a range set in one mode does not leak into the other. Otherwise, like RC 7, they survive a host
  `reset()`.
- MPE's pressure and timbre (CC74) drive the **Expression** knobs (below). A member note takes the
  higher of its own value and the master channel's.
- Presets don't store the switch: it describes the controller, not the sound.

mpe-sender (an Unlikelyware test tool, not yet public) plays scripted MPE tests for listening.

### Expression: pressure and timbre

Most of a struck note is fixed at the strike: strike position and Brightness only shape how the strike
excites each mode, so moving them later changes nothing until the next strike. What a player can still
change on a ringing note is how it decays. The **Expression** panel's three knobs follow that split;
each is 0 by default, so nothing changes until you turn it, and a controller that sends no pressure or
CC74 sounds the same with the knobs up.

| Knob | Source | What it does | When |
|---|---|---|---|
| **Press>Decay** | pressure | decay time, ± octaves at full pressure (+2 = 4× longer). Positive: press to let it ring, ease off to damp it. Negative: pressing damps it, like a hand on a bar. | continuously, on the ringing note |
| **Tmb>Tilt** | timbre (CC74) | added to **Damp** at full timbre: positive darkens the tail, negative brightens it | continuously, on the ringing note |
| **Tmb>Pos** | timbre (CC74) | added to **Strike Pos** at full timbre: where your finger sits is where the mallet hits | at the strike |

Pressure is channel pressure, poly pressure, or CLAP's Pressure note expression; timbre is CC74 on the
note's channel, MIDI 2.0 per-note controller #74, or CLAP's Brightness. Per-note values win over the
channel's. MPE controllers send a note's starting CC74 before its note-on, which is what Tmb>Pos reads.
Both sources are PlainOldSynth's Pressure and Timbre modules, smoothed over 2 ms.

**A note is note number + channel + group.** The same key on two channels is two voices with
independent per-note state, and note-offs the engine generates itself (sustain-pedal release,
all-notes-off, panic) are built from the voice's own note-on so they match. In mono, per-note
expression for a held-but-silent note in the stack doesn't move the sounding note.

**All Notes Off (CC 123)** releases the notes of the channel it arrives on, pedal-held ones included;
they ring out like any released note. **All Sound Off (CC 120)** silences that channel's voices at
once, ringing tails included. When the host
tags notes with CLAP note ids, the ids decide instead, so a re-struck key is a new note.

### Pitch 7.25 and CLAP note expressions

**Pitch 7.25** (MIDI 2.0 registered per-note controller #3) sets an absolute pitch for the sounding
note, and both bends offset from it. Like the bends it bypasses the glide. KeyboardEDO's glide mode
sends it. As the spec makes it, it is also persistent: a tuning table, kept per key, channel and
group, that later notes on the same key inherit, whether or not a note was sounding when it arrived.
A note-on with a Pitch 7.9 attribute names its own pitch and ignores the table; Per-Note Management
with Reset (which KeyboardEDO sends before each note-on) clears the key. The table survives a host's
reset but is not saved with the plugin state: the controller sends it.

**CLAP note expressions** (`CLAP_EVENT_NOTE_EXPRESSION`, how Bitwig sends per-note expression) are
converted to MIDI 2.0 per-note messages:

- **Tuning** becomes Pitch 7.25 at key + tuning, so it is exact at any size within Pitch 7.25's
  0–128 semitone span rather than clamped to the ±2 bend range. It lasts for its note only and
  never enters the tuning table.
- **Pressure** becomes poly pressure and **Brightness** registered per-note controller #74, which
  drive the Expression knobs' pressure and timbre.
- Volume, Pan, Vibrato and Expression (the CLAP expression of that name, not the panel) are ignored.

An expression may name only a note id: tuning then looks up that note's key.

**Note end and voice info.** A struck note rings on after note-off, so the plugin reports
`CLAP_EVENT_NOTE_END` for a CLAP note when the engine actually stops playing it: when the voice rings
down to silence, is stolen, or (mono) the note leaves the held stack. Hosts such as Bitwig keep a
note's per-voice modulation and expressions running until then. Notes that arrive as MIDI events get
no note end, since a CLAP note end answers a CLAP note-on. The voice-info extension reports the voice
count (10, or 1 in Mono) and that notes may overlap on the same key, since note ids keep them apart. VST3 note expression
(PlainOldSynth's clap-wrapper extension) does not apply: StruckRoom has no VST3 build.

The [DISTRHO Plugin Framework (DPF)](https://github.com/DISTRHO/DPF) is used **only as the GUI
layer** (UIExporter + NanoVG + pugl). The CLAP entry point is hand-written in `src/plugin.cpp`,
because DPF's generated CLAP wrapper drops `CLAP_EVENT_MIDI2` and advertises only the MIDI/MPE note
dialects, which would make MIDI 2.0 7.9-pitch notes unreachable. This is the same raw-CLAP shape
PlainOldSynth uses.

## Installing

The download holds `StruckRoom.clap`. Put it in your CLAP folder (create the folder if it isn't there),
then rescan plugins in your host:

| | Linux | macOS |
|---|---|---|
| For you | `~/.clap` | `~/Library/Audio/Plug-Ins/CLAP` |
| For every user | `/usr/lib/clap` | `/Library/Audio/Plug-Ins/CLAP` |

On macOS, `~/Library` is hidden in Finder: Go ▸ Go to Folder opens it. The 0.9 previews aren't
notarized, so macOS won't load a downloaded plugin until its quarantine flag is cleared:

```bash
xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/CLAP/StruckRoom.clap
```

A host that is already running keeps the plugins it loaded: quit and restart it. To build StruckRoom
yourself instead, see below.

## Building from source

```bash
git clone https://github.com/erickampman/StruckRoom.git
cd StruckRoom
tools/fetch_deps.sh
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

[BUILDING.md](BUILDING.md) has what to install first on Linux and macOS, the tests, installing
the plugin for hosts, and the build options.

## License

MIT.
