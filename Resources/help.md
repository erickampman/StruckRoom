<div style="display: flex; align-items: center; gap: 0.75rem; margin: 1.5rem 0 0.5rem;">
  <a href="https://www.unlikelyware.com" target="_blank" rel="noopener noreferrer">
    <img src="Unlikelyware.png" alt="Unlikelyware" style="width: 56px; height: auto; border-radius: 10px; margin: 0; display: block;">
  </a>
  <h1 style="margin: 0;">StruckRoom</h1>
</div>

<p>
  <em>StruckRoom is a CLAP and VST® 3 instrument on macOS and Linux, and an AUv2 instrument on macOS. Fine adjustment uses the Control key on both: on the Mac that is Control, not Cmd.</em>
</p>

::: {.callout .callout-note .card}
## On This Page
[Overview](#overview) · [Getting Started](#getting-started) · [The Panel](#panel)

**The Object tab —** [The Spectrum](#spectrum) · [Strike](#strike) · [Object](#object) · [Decay](#decay) · [Global](#global) · [Timbre](#timbre) · [Expression](#expression)

**The Room tab —** [The Room Map](#room-map) · [Room](#room) · [Placement](#placement) · [Mix](#mix)

**The Walls tab —** [The Walls](#walls) · [Wall Effects](#wall-effects) · [The Room Decay Plot](#room-decay)

**Playing —** [Voices and MIDI](#voices-midi) · [Formats and microtonal tuning](#formats) · [Presets](#presets) · [Tips](#tips) · [Troubleshooting](#troubleshooting)
:::

::: {.callout .callout-note .card}
## Overview {#overview}
StruckRoom is a **modal-synthesis instrument in a room**: it sounds like a struck object (a bar, a bell, a tine, a block) rather than an oscillator through a filter, and it puts that object in a room that **scales with the note**. Every length in the room is measured in wavelengths of the note being played, so the object, the walls, where the object sits and the standing waves all transpose together. It sits somewhere between a struck instrument and a reverb.

Each note goes through four stages:
1. **Strike** makes a short excitation burst: a smooth mallet bump or a burst of noise, from a single sample long (the hardest, brightest hit) to about 20 ms or 1.6 periods of the note (the softest).
2. **ModalBank** is a bank of up to 64 tuned resonators, one per vibrational **mode** of the object. The burst sets them ringing. Which frequencies they ring at comes from the **Material**; how long each one rings comes from **Decay** and **Damp**.
3. **Room** is a two-dimensional room with two pairs of walls (side walls and front/back), heard from one point by a Mid/Side microphone pair. It adds the object's **direct** sound, four **first reflections** (one off each wall), the walls' own **ringing** if you let them ring, and the room's **standing waves** (its modes), which decay as the walls absorb. Each voice has a room of its own.
4. **Final** sends the room's stereo output to the host.

**There is no amplitude envelope.** The resonators' own decay is the envelope, so a note rings on after you let go of the key, for as long as the decay says, and the voice frees itself once it has rung down to silence. Note-off doesn't stop a ringing note; it only ends your control of it.
:::

::: {.callout .callout-note .card}
## Getting Started {#getting-started}
1. **Load StruckRoom** on an instrument track in your host (REAPER, Bitwig, StageHand, or any CLAP, VST3 or AU host). For microtonal playing, use the CLAP version if your host has it: see [Formats and microtonal tuning](#formats)..
2. **Play.** The default patch (the same as the **Init** preset) is a free-free metal bar with a 2-second decay, in a room 3 × 2.2 wavelengths of the note.
3. **Pick a preset** from the **Preset** menu at the top right: Glass Bell, Init, Marimba, Tine EP or Wood Block.
4. **Change the object** with **Material** in the Object panel (on the **Object** tab), and watch the spectrum at the top left redraw.
5. **Change how long it rings** with **Decay**, and how quickly its upper modes die away with **Damp**.
6. **Change the mallet** with **Hardness** in the Strike panel: lower is harder and brighter.
7. **Change the room** on the **Room** tab: drag the object and the listener around the map, and balance the object's own sound against the room's with **Direct**, **Early** and **Late**.
8. **Change the walls** on the **Walls** tab: how much they absorb, and whether they ring.

The host saves every parameter with its project.
:::

::: {.callout .callout-note .card}
## The Panel {#panel}
**The header:** the Unlikelyware logo (a click opens unlikelyware.com), the title, the three tabs **Object**, **Room** and **Walls** in the middle, then **Save** and **Load** (see [Presets](#presets)), **Help**, and the **Preset** menu. Click a tab to show its panels; the header stays the same on all three.

**The Object tab** (the struck object), in three rows:
- **Top:** the [spectrum](#spectrum), and the **Strike** panel.
- **Middle:** **Object** and **Decay**.
- **Bottom:** **Global**, **Timbre** and **Expression**.

**The Room tab:** the [room map](#room-map) on the left, at full height; on the right, one above the other, the **Room**, **Placement** and **Mix** panels.

**The Walls tab:** the **Side walls** and **Front / back walls** panels side by side, each three rows of four controls, over the [room decay plot](#room-decay) across the bottom.

Each panel has its own colour: steel for Global, amber for Strike, green for Object, violet for Decay, copper for Timbre and teal for Expression; blue for Room, sand for Placement and rose for Mix; coral for the side walls and mint for the front and back walls. Each knob shows its name above it and its value below.

**Resizing:** drag the host window's corner. The panel keeps its shape (1080 × 640) and scales everything in it; the smallest size is 864 × 512.
:::

<details class="disclosure">
<summary class="disclosure-title subtle">Knobs and menus</summary>

::: disclosure-content
| Gesture | Result |
|---|---|
| Drag up or down | Change the value. About 200 points of drag covers the whole range. |
| Control-drag | Ten times finer (Control on the Mac too) |
| Scroll wheel | One step is 5% of the range; with Control, 0.5% |
| Shift-click | Back to the default value |
| Click a menu | Opens its list; click a choice, or click elsewhere to close it |
| Drag the object or listener on the room map | Moves it (see [The Room Map](#room-map)) |

Knobs with a time (Glide, Decay, Air, and the walls' Decay) work on a logarithmic scale, so the short end gets as much travel as the long end; so do Room W, Room D, Detail and the walls' Ratio. Times under a second read in ms, longer ones in s.
:::

</details>

::: {.callout .callout-note .card}
## The Spectrum {#spectrum}
The display at the top left of the Object tab draws the current object's modes as bars:
- **Across:** each mode's frequency, on a logarithmic scale, so octaves are equal widths.
- **Height:** how long that mode rings, relative to the longest. A taller, more opaque bar outlasts a shorter, fainter one.

It's drawn for a nominal C3 (moved by **Pitch**), and redraws whenever **Material**, **Modes**, **Stretch**, **Pitch**, **Decay**, **Damp** or the **Bell** knobs change. It's a picture of the decays, not of loudness: **Strike Pos** and **Brightness** change how loud each mode is, which it doesn't show. It holds no controls.
:::

::: {.callout .callout-note .card}
## Strike {#strike}
The exciter: what hits the object, and how hard.
- **Strike** (Mallet / Noise): **Mallet** is a smooth bump, like a mallet's contact; **Noise** is a burst of white noise of the same length, for a scraped or brushed attack. Default Mallet.
- **Hardness** (0–1, default 0.30): how long the contact lasts. **0** is a single-sample impulse, the hardest and brightest strike, which sets every mode ringing equally. Higher values lengthen the contact, which rolls off the upper modes, as a softer mallet does. Hardness changes the tone, not the level: the burst is scaled so a softer hit isn't louder.
- **Vel>Level** (0–1, default 1.00): how much velocity sets loudness. At **1** loudness follows velocity fully; at **0** every note strikes at full level.
- **Vel>Hard** (0–1, default 0.50): how much velocity hardens the strike. A harder hit shortens the contact, by up to Hardness × Vel>Hard at full velocity, so harder playing is brighter as well as louder.
- **Contact** (Note-Rel / Wall-Clk): how the contact length is measured.
  - **Note-Rel** (default): in periods of the played note. At Hardness 1 the contact spans 1.6 periods, so the strike sits at the same place relative to the fundamental on every key, and level and tone stay even across the keyboard. (Capped at 100 ms, which matters only below about 25 Hz.)
  - **Wall-Clk**: in fixed time, up to 20 ms at Hardness 1, like a real mallet: low notes come out fuller and high notes brighter and more percussive.
:::

::: {.callout .callout-note .card}
## Object {#object}
What is struck: its modes and its pitch.
- **Pitch** (−4 to +4 oct, default 0): transposes the instrument, in octaves. It's continuous, so 1/12 of an octave is a semitone.
- **Material** (26 choices, default Bar): the set of mode frequencies. See below.
- **Modes** (1–64, default 64): how many modes sound, counting from the lowest. A material has only so many modes (see below), so a higher setting than that plays them all. Fewer modes is a simpler, purer tone. Modes that would land above the Nyquist frequency are muted.
- **Stretch** (0–19, default 9): inharmonicity. It steps through a table of values: **9 is none**, higher steps push the upper modes sharp (as stiffness does in a real bar or piano string), lower steps pull them flat. The steps are −0.5, −0.2, −0.1, −0.05, −0.02, −0.01, −0.005, −0.002, −0.001, **0**, 0.001, 0.002, 0.005, 0.01, 0.02, 0.05, 0.1, 0.2, 0.5, 1.0, and each mode's frequency ratio is raised to the power 1 + that value. The knob shows the step number, not the value.
:::

<details class="disclosure">
<summary class="disclosure-title subtle">The materials</summary>

::: disclosure-content
The first twelve are tables of real objects, worked out from their physics. The number is how many modes each has.

| Material | Modes | What it is |
|---|---|---|
| String | 16 | An ideal string: exactly harmonic, a clear pitch |
| Bar | 8 | A free bar (an untuned glockenspiel bar): strongly inharmonic, the struck-metal sound |
| Tuned Bar | 6 | A marimba or vibraphone bar, undercut so its first overtones sit at 4× and 10× |
| Membrane | 16 | An ideal drum head: inharmonic (Stretch can pull it toward a pitch) |
| Open Tube | 12 | A tube open at both ends |
| Closed Tube | 10 | A tube closed at one end |
| Tine | 6 | A tine, as in an electric piano |
| Bell | 12 | A bell, whose hum tone outlasts the others |
| Guitar Body | 12 | The resonances of a guitar's body |
| Violin Body | 14 | The resonances of a violin's body |
| Soundboard | 18 | A piano soundboard |
| Generic Body | 16 | A general instrument body |

The other fourteen are **generated series**: mathematical sets of ratios that don't come from any real object. They produce up to 64 modes, fewer for series that grow fast. Integer series (Harmonic, Octave, Fibonacci, Prime, Triangular, Tetrahedral, Partition) fuse into a pitch; irrational ones (Golden, Silver) don't, and clang.

| Material | Ratios |
|---|---|
| Harmonic | 1, 2, 3, 4 … |
| Octave | Powers of 2: an octave stack |
| Fibonacci | 1, 2, 3, 5, 8 …: harmonics with gaps |
| Prime | 1, 2, 3, 5, 7 …: different gaps, hollower |
| Triangular | 1, 3, 6, 10 …: sparse |
| Tetrahedral | 1, 4, 10, 20 …: starts on the marimba tuning |
| Partition | The partition numbers 1, 2, 3, 5, 7, 11 … |
| Golden | Powers of φ: the most irrational |
| Silver | Powers of 1 + √2: a sparser cousin of Golden |
| Piano | A slightly stiff string, its upper harmonics a little sharp |
| Power | n^1.5, between a string and a bar |
| Sqrt | √n: slow-growing and dense, like a plate |
| Box 3D | The modes of a rectangular room: dense |
| Harmonic # | The harmonic numbers 1, 1.5, 1.83 …: logarithmic, lots of beating |
:::

</details>

::: {.callout .callout-note .card}
## Decay {#decay}
How long the object rings, and which modes die first.
- **Decay** (20 ms to 60 s, default 2.00 s): the T60 of the lowest mode, the time it takes to fall by 60 dB. This is the length of the note.
- **Damp** (0–3, default 0.90): how much faster the upper modes die than the lowest. Each mode's decay is Decay × ratio^−Damp, where ratio is its frequency over the fundamental's. At **0** every mode rings as long as the lowest, like metal; around **2** the upper modes die quickly, like wood.
- **Bell Amt** (−4 to +4 oct, default 0): adds a bump to the decay curve, so a band of modes rings longer (positive) or shorter (negative) than Damp alone gives. It's in octaves of decay time: +2 rings 4× longer at the band's centre, −2 4× shorter. **0** turns the bump off.
- **Bell Ctr** (0–7 oct, default 2.00): the centre of that band, in octaves above the fundamental.
- **Bell Wid** (0.1–4 oct, default 0.40): the width of the band, in octaves.

The Bell knobs do nothing while Bell Amt is 0. The spectrum shows their effect as a hump or a dip in the bar heights.
:::

::: {.callout .callout-note .card}
## Global {#global}
- **Voices** (Mono / Poly 4 / Poly 10 / Poly 20 / Poly 32, default Poly 4): how many notes can sound at once. Every voice has a room of its own, so voices cost a lot of CPU. See [Voices and MIDI](#voices-midi).
- **Glide** (0.1 ms to 2 s, default 10 ms): the time to slide from one note's pitch to the next. **Only in Mono**; in the Poly modes each note starts on its own pitch.
- **Level** (0–2, default 0.50): the output level, as a gain.
- **Width** (0–1, default 1.00): the listener's stereo width, the level of the side signal in its Mid/Side pair. **0** is mono; at **1**, sound arriving from the left wall lands on the left. A centred source keeps its level at any width.
- **MPE** (Off / On, default Off): treats the input as an MPE controller (a lower zone). See [Voices and MIDI](#voices-midi). Presets don't change it: it describes your controller, not the sound.
:::

::: {.callout .callout-note .card}
## Timbre {#timbre}
How the strike sets each mode ringing. Both are read **at the strike**, so turning them while a note rings changes nothing until the next note.
- **Strike Pos** (0–1, default 0.25): where on the object it's struck. Striking at a mode's node leaves that mode silent; **0.5** cancels every even mode, as plucking a string in the middle does, for a hollow tone. Near **0** (the edge) the low modes are weak and the sound is thin.
- **Brightness** (−1 to +1, default 0): tilts the modes' levels. Positive favours the upper modes, negative the lower. The overall level stays the same, so Brightness is a tone control, not a volume control.
:::

::: {.callout .callout-note .card}
## Expression {#expression}
What a controller's **pressure** and **timbre** do to a note. Each knob is 0 by default, which does nothing, so a controller that sends no pressure or timbre sounds the same with these up.
- **Press>Decay** (−4 to +4 oct, default 0): pressure changes the decay time of the ringing note, by this many octaves at full pressure (+2 rings 4× longer). Positive: press to keep it ringing, ease off to damp it. Negative: pressing damps it, like a hand on a bar. It works all through the note.
- **Tmb>Tilt** (−3 to +3, default 0): timbre adds to **Damp**, by this much at full timbre. Positive darkens the tail, negative brightens it. It works all through the note.
- **Tmb>Pos** (−1 to +1, default 0): timbre adds to **Strike Pos**, by this much at full timbre, so where your finger sits is where the mallet hits. It's read **at the strike** only.

**Pressure** is channel pressure, poly pressure, or a CLAP Pressure expression. **Timbre** is CC 74 on the note's channel, MIDI 2.0 per-note controller 74, or a CLAP Brightness expression. A per-note value wins over the channel's. Changes are smoothed over 2 ms.
:::

::: {.callout .callout-note .card}
## The Room Map {#room-map}
The Room tab's left half shows the room from above, at its width-to-depth shape, with a grid one wavelength apart. The side walls are its left and right edges, the front and back walls its bottom and top. A wall is drawn heavier the more it absorbs, and glows when it rings.
- **The object** is the green dot; **the listener** is the white one, with the two lobes of its figure-8 microphone pointing at the side walls. A faint line between them is the direct path.
- **Drag either one** to move it. The map *is* the **Src X/Y** and **Mic X/Y** knobs in the [Placement](#placement) panel: dragging moves them, and the host records it as automation.
- Left on the map is the left channel; Y runs from the front wall at the bottom to the back wall at the top.
- The room's size, in wavelengths, is written along the bottom.
:::

::: {.callout .callout-note .card}
## Room {#room}
The room's size, and how it follows the note.
- **Room W** (0.25–16 wl, default 3.00) and **Room D** (0.25–16 wl, default 2.20): the room's width and depth, in wavelengths of the note (of middle C, note 60, when Room Track is below 1).
- **Room Track** (0–1, default 1.00): at **1** the room scales with the note, so every note sits in the same room relative to its pitch. At **0** it is a fixed physical room, sized at middle C, so higher notes fit more wavelengths in it.
- **Decay Track** (0–1, default 0.00): at **1** every decay time (the object's, the walls' and the air's) scales with the note's period, so the whole scene transposes like tape speed: high notes ring shorter, low notes longer. At **0** decay times stay in seconds.
- **Air** (0.5–30 s, default 12.0 s): the decay time of the air, which keeps a room with perfectly reflective walls from ringing for ever. In seconds at middle C.
- **Detail** (2–64 × f0, default 12.00): the room's standing waves are modelled up to this multiple of the note's frequency. Higher is more realistic and costs more CPU (see [Tips](#tips)). In a large room at high Detail the limit is lowered to keep it to about 15,500 modes.

The room is tuned at each strike and then left alone: pitch bend and glide move the object, not the room.
:::

::: {.callout .callout-note .card}
## Placement {#placement}
Where the object and the listener stand, as fractions of the room (0 to 1). The same as dragging them on the [room map](#room-map).
- **Src X** (default 0.31) and **Src Y** (default 0.37): the object.
- **Mic X** (default 0.62) and **Mic Y** (default 0.71): the listener.

Placement decides which standing waves are driven and heard: a mode is silent where the object or the listener sits on its node, as **Strike Pos** does for the object's own modes. The overall level is kept steady as they move, while the balance of direct sound, reflections and room changes with the distance between them, as it would in a real room.
:::

::: {.callout .callout-note .card}
## Mix {#mix}
- **Direct** (0–2, default 1.00): the object's own sound, delayed by the distance from the object to the listener.
- **Early** (0–2, default 1.00): the four first reflections, one off each wall, filtered by that wall's absorption. (The walls' ringing has its own level: **Ring**, on the [Walls](#walls) tab.)
- **Late** (0–2, default 1.00): the standing waves, the room's modes.

Direct 1, Early 0 and Late 0 is the object alone, with no room.
:::

::: {.callout .callout-note .card}
## The Walls {#walls}
The **Side walls** panel sets the left and right walls together, the **Front / back walls** panel the front and back. They have the same twelve controls, captioned **Side …** and **F/B …**; the first eight are below, the last four under [Wall Effects](#wall-effects).
- **Absorb** (0–1, default 0.15): how much of the sound the walls absorb at each reflection. More absorption makes the reflections quieter and the room's modes die faster.
- **Bright** (0–8 oct, default 5.00): where the walls start to absorb the high frequencies, in octaves above the note. Lower is a duller wall; **8** is nearly flat. Like Absorb, it filters the reflections and shortens the high modes.
- **Ring** (0–2, default 0): the walls as resonators of their own, struck where the sound meets them. **0** is a wall that doesn't ring.
- **Mat** (26 choices, default Membrane): the ringing wall's material, from the same list as the object's (see *The materials* under [Object](#object)).
- **Ratio** (0.0625–4, default 0.50): the wall's fundamental, relative to the note.
- **Decay** (50 ms to 20 s, default 1.50 s): how long the wall rings (its T60).
- **Damp** (0–3, default 1.00): how much faster the wall's upper modes die, as the object's **Damp**.
- **Link** (Off / On, default Off): **On** rings the wall with the energy it absorbed, rather than the full sound that reached it.

Ring, Mat, Ratio, Decay and Damp do nothing while Ring is 0.
:::

::: {.callout .callout-note .card}
## Wall Effects {#wall-effects}
The last row of each walls panel.
- **Shift** (0–10 %, default 0): shifts the frequency of the reflections and the wall's ring, by this share of the note's frequency. The near wall shifts up and the far wall down, so the two sides drift against each other: small values beat slowly; 10% clangs.
- **Flex** (0–1, default 0): the wall gives under the sound, so its reflection's path, and its pitch, bend with the level: soft notes barely, loud ones more.
- **Rattle** (0–1, default 0): the wall resonator buzzes once it rings past the gap, and stops as the ring decays inside it. It needs **Ring** above 0.
- **Gap** (0.02–0.95, default 0.15): that gap, as a share of a full-velocity ring. Lower and soft notes rattle too; higher and only the loudest hits do.
:::

::: {.callout .callout-note .card}
## The Room Decay Plot {#room-decay}
The plot across the bottom of the Walls tab shows every one of the room's modes as a dot, for a note at middle C (C4):
- **Across:** the mode's frequency, on a logarithmic scale, from a quarter of the note up to the **Detail** limit, with lines at the note (f0) and its octaves.
- **Up:** how long that mode rings, from 0 at the bottom to the longest, whose time is written at the top of the axis.
- **Colour:** which pair of walls the mode mostly reflects from, coral for the side walls and mint for the front and back, so you can see each pair's share.
- **Triangles** along the bottom mark each ringing wall pair's fundamental.

Absorption and **Bright** pull the dots down, and **Air** caps them. A dense room is thinned to a few thousand dots. It holds no controls.
:::

::: {.callout .callout-note .card}
## Voices and MIDI {#voices-midi}
**Voices.** In the Poly modes each note gets its own voice, up to 4, 10, 20 or 32; past that, a sounding voice is taken for the new note. Because notes ring after note-off, a fast passage with a long Decay can use many voices: choose Poly 10, 20 or 32 for that, if your CPU allows (each voice has its own room; see [Tips](#tips)). **Mono** plays one voice: a new note while another is held strikes the same resonators again and glides them to the new pitch (see **Glide**), so the old note's ring carries into the new one. Letting go of the new note goes back to the note still held.

**Sustain pedal** (CC 64) holds released notes in the Poly modes. Since a struck note rings on anyway, the pedal mostly matters for what still controls the note: see *Released notes* below.

**MIDI 2.0 first.** In the CLAP version, StruckRoom prefers MIDI 2.0 from the host, so a note's exact pitch (MIDI 2.0 Pitch 7.9) reaches the resonators unchanged. MIDI 1.0 works too; its values are scaled to MIDI 2.0's, so velocity 127, CC 127 and a full pitch bend are full scale. In MIDI 2.0, a note-on with velocity 0 is a note-on; **Vel>Level** decides how loud it is.

**A note is note number + channel + group.** The same key on two channels is two independent notes.
:::

<details class="disclosure">
<summary class="disclosure-title subtle">Pitch bend</summary>

::: disclosure-content
- **Channel pitch bend** (MIDI 1.0 or 2.0) is **±2 semitones** by default. Each note follows the bend of the channel it was played on, so with one note per channel it's per-note bend.
- **RPN 0** (pitch bend sensitivity) sets a channel's range.
- **MIDI 2.0 per-note pitch bend** bends only the note it names, over the same range as its channel's bend. A controller can give per-note bend a range of its own with MIDI 2.0 registered controller 7, as KeyboardEDO does.
- **Pitch 7.25** (MIDI 2.0 per-note controller 3) sets an absolute pitch for a key, and the bends offset from it. It retunes the sounding note and stays with the key: later notes on that key start at it, unless the note-on carries its own Pitch 7.9 pitch. Per-Note Management Reset clears it.
- Bends come after **Glide**, so glide never smears a bend, and they bend a ringing tail smoothly rather than re-striking it. They bend the object only: the room keeps the tuning it took at the strike. Bend changes are smoothed over 2 ms, and a note starts at its bent pitch.
- Per-note bend resets on each note-on. One sent before its note-on is ignored.
:::

</details>

<details class="disclosure">
<summary class="disclosure-title subtle">MPE</summary>

::: disclosure-content
With **MPE** on, StruckRoom treats its input as an MPE **lower zone**: channel 1 is the master channel, 2–16 are the member channels, one per note. StruckRoom only receives MPE; it never sends it.
- A member channel's bend range is **±48 semitones** by default, and the master channel's bend (±2) is added to every note, so the controller's pitch wheel moves everything on top of each note's own slide.
- RPN 0 on any member channel sets the range of all of them.
- Pressure and timbre (CC 74) drive the [Expression](#expression) knobs. A member note takes the higher of its own value and the master channel's.
- Ranges set by RPN 0 go back to their defaults when MPE is switched on or off, and on an MPE Configuration Message.
:::

</details>

<details class="disclosure">
<summary class="disclosure-title subtle">Released notes</summary>

::: disclosure-content
A struck note rings for seconds after its note-off. Once it's released (or held only by the sustain pedal), its own per-note messages no longer reach it, as the MPE specification says: so when an MPE controller re-centres a member channel for the next note, the ringing tail stays where it was. The master channel's bend still moves it.

With MPE off, an ordinary pitch wheel still bends released notes. MIDI 2.0 per-note messages stop at note-off. CLAP note expressions sent by note id keep working until the note has rung out.
:::

</details>

<details class="disclosure" id="clap-expressions">
<summary class="disclosure-title subtle">CLAP note expressions and note end</summary>

::: disclosure-content
Hosts such as Bitwig send per-note expression as CLAP note expressions:
- **Tuning** becomes an exact per-note pitch (as Pitch 7.25), so any amount of tuning is exact rather than limited to the bend range. It lasts for its note only.
- **Pressure** and **Brightness** drive the [Expression](#expression) knobs' pressure and timbre.
- Volume, Pan, Vibrato and Expression (the CLAP expression of that name) are ignored.

For a note the host sends as a CLAP note, StruckRoom reports the note's end when it actually stops sounding: when it has rung down to silence, when its voice is taken, or (in Mono) when it leaves the held notes. So the host keeps that note's modulation running through its tail.
:::

</details>

<details class="disclosure" id="formats">
<summary class="disclosure-title subtle">Formats and microtonal tuning</summary>

::: disclosure-content
The VST3 and AUv2 versions are the CLAP version inside a wrapper, so they sound the same. What differs is what reaches them from the host, and that matters most for microtonal scales.

**CLAP** passes MIDI 2.0 through as it is. A note can carry its exact pitch (Pitch 7.9), a key can be retuned (Pitch 7.25), and per-note pitch bend reaches just its note, all at full resolution and timed to the sample. This is the version to use for microtonal tuning.

**VST3 is problematic for microtonal scales**, because of how the format itself carries notes and controllers:
- **A note is a whole key number.** VST3 has no MIDI 2.0, so Pitch 7.9, Pitch 7.25 and MIDI 2.0 per-note pitch bend never arrive. A tuning can only come as a note's *tuning* offset in cents, or as a **Tuning** note expression, and only if the host sends them. Most hosts don't: they send the plain key, and the note plays in 12-tone equal temperament.
- **Pitch bend and controllers are not MIDI events.** VST3 turns them into parameter changes, and the wrapper turns those back into MIDI 1.0. They come through at MIDI 1.0 resolution, and as one value per channel per audio block, so a bend or a CC moving within a block arrives late, and only its last value counts.
- **Retuning by pitch bend is unreliable.** Microtonal tools that tune each note by putting it on its own channel and bending that channel (MPE, or "multichannel" tuning) depend on the bend arriving before its note. As a parameter change it is not ordered against the note-on, so a note can start at the old bend and slide. Setting a channel's bend range with RPN 0 is spread over several controllers, which can arrive merged or out of order in the same block.

With a host that sends VST3 Tuning note expressions (or note-on tuning), StruckRoom plays them exactly, as it does CLAP's Tuning ([CLAP note expressions](#clap-expressions)). Pressure and Brightness note expressions work in VST3 too.

**AUv2** receives MIDI 1.0 as events, in order and timed to the sample, so pitch bend, RPN 0 and MPE work as they do in MIDI 1.0. It has no MIDI 2.0 and no per-note expression, so microtonal tuning is limited to bend-based methods at MIDI 1.0 resolution.
:::

</details>

<details class="disclosure">
<summary class="disclosure-title subtle">All Notes Off and All Sound Off</summary>

::: disclosure-content
- **All Notes Off (CC 123)** releases every note on the channel it arrives on, pedal-held ones included. They ring out like any released note.
- **All Sound Off (CC 120)** silences that channel's notes at once, ringing tails included.
:::

</details>

::: {.callout .callout-note .card}
## Presets {#presets}
The **Preset** menu at the top right holds the factory presets, in alphabetical order:

| Preset | Material | Decay | Character |
|---|---|---|---|
| Glass Bell | Bell, 48 modes | 8 s | Hard strike, very little damping: long and bright |
| Init | Bar, 64 modes | 2 s | The default patch |
| Marimba | Tuned Bar, 24 modes | 0.7 s | Heavy damping, a little dark |
| Tine EP | Tine, 32 modes | 3 s | A bell bump (+1 oct at 3 oct up) for an electric-piano shimmer |
| Wood Block | Closed Tube, 12 modes, an octave up | 0.12 s | Very hard strike, heavy damping: a short knock |

All five use Poly 4 and the default room and walls: 3 × 2.2 wavelengths, walls that absorb 0.15 and don't ring.

- **Choosing a preset sets every sound parameter**, Voices, the Expression knobs, the room and the walls included. It leaves the **MPE** switch alone.
- The menu shows **--** until you choose one, and again when the editor is reopened. Changing a knob after choosing a preset doesn't change the name shown.

**Your own patches:** **Save** and **Load**, left of Help, write the patch to a file and read one back, through the system's file dialog. The first time, the dialog opens on **Documents/StruckRoom**; after that, on the last folder you used.
- A saved patch has every sound parameter, like a factory preset, and leaves out **MPE**. Loading one sets those parameters and leaves MPE alone.
- The menu then shows the patch's name, dimmed, in place of a factory preset's. If a file can't be written or isn't a StruckRoom patch, the menu says **Couldn't save** or **Couldn't load** instead.
- The file is the same JSON as the factory presets, so a patch you save can be put in the source's `Presets` folder to build it in as a factory preset.
- The host still saves the patch with its project, whether or not you saved it to a file.
:::

::: {.callout .callout-note .card}
## Tips {#tips}
- **Start from the material, then the decay.** Material decides what the object is; Decay and Damp decide what it's made of. The same **Tuned Bar** is a marimba with Damp around 1.6 and a vibraphone with Damp near 0.
- **Damp is the material knob.** Low Damp sounds like metal or glass, high Damp like wood or skin.
- **Hardness for tone, Brightness for balance.** Hardness changes the attack (a soft mallet takes the click off). Brightness changes the balance of the whole ring.
- **Strike Pos 0.5** on String or Harmonic cancels the even modes, for a hollow, clarinet-like tone.
- **Stretch** above 9 makes a harmonic material stiffer, its upper modes sharp, as in a piano string: try String with Stretch at step 11–13.
- **Fewer Modes** makes a purer, simpler sound, and costs less CPU.
- **For an even keyboard**, keep **Contact** on Note-Rel. Wall-Clk makes the bass boomier and the top pingier, as real mallets do.
- **Long decays need voices.** With a Decay of several seconds, Poly 10, 20 or 32 keeps older notes from being cut off, at a cost in CPU.
- **CPU.** Every voice carries its own room, so voices are expensive, and **Detail** is the main cost of each. On an Apple M4 at 48 kHz, a 4-note chord takes about 8% of a core at Detail 12, 24% at Detail 21 and 41% at Detail 32. Lower Detail, or use fewer voices, if the host struggles.
- **To hear the object alone**, set Early and Late to 0 and Direct to 1.
- **Bigger rooms** (higher Room W and D) have more, denser modes, and cost more CPU at the same Detail.
- **For a hand-damped bar on a pressure controller**, set Press>Decay negative: press into a note to stop it.
:::

::: {.callout .callout-note .card}
## Troubleshooting {#troubleshooting}
- **Notes keep ringing after I let go.** That's the design: a struck object rings until it dies away. Shorten **Decay**, raise **Damp**, or use a negative **Press>Decay** to damp it by pressure.
- **Notes cut off in fast passages.** They're running out of voices: the default is Poly 4. Choose Poly 10, 20 or 32, or shorten Decay.
- **Too much CPU, or crackles.** Lower **Detail** (the room's main cost), choose fewer voices, or shrink the room.
- **The wall's Mat, Ratio, Decay, Damp or Rattle do nothing.** That pair's **Ring** is at 0, so the wall doesn't ring.
- **Glide does nothing.** It only works in **Mono**.
- **Raising Modes changes nothing.** The material has fewer modes than that (see *The materials* under [Object](#object)).
- **Strike Pos or Brightness don't change a held note.** They're read at the strike; play the next note.
- **The Expression knobs do nothing.** The controller isn't sending pressure or CC 74, or the knobs are at 0.
- **Bends are huge.** MPE is on and the controller isn't an MPE controller: member channels bend ±48 semitones. Turn **MPE** off.
- **Distortion.** Lower **Level**. Many loud notes ringing together add up.
- **A rebuilt StruckRoom doesn't show its changes.** Quit and restart the host: a running host keeps the plugin it loaded first.
:::

VST is a registered trademark of Steinberg Media Technologies GmbH.
