// ss_bend_test — MIDI 2.0 per-note decode, channel + per-note pitch bend, and channel-aware voice
// matching, end to end through the VoiceManager (no plugin, no host).
//
// To measure pitch, the bank is set to a single mode of the Harmonic series: each voice is then one
// decaying sine at its fundamental, so a zero-crossing count (one note) or a single-bin DFT (several
// notes) reads the pitch directly. The T60 is long, so the tone barely decays over a measurement.
//
// A struck voice keeps ringing after note-off (the bank's decay is the envelope), so "this note was
// released" is checked through activeCount() after a short ring-out, not by listening for silence.

#include "VoiceManager.hpp"
#include "clap_ump.h"
#include "pm_addresses.h"
#include "pm_midi.h"
#include "pm_params.h"

#include <cmath>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

namespace {

constexpr double   kSampleRate = 48000.0;
constexpr uint32_t kBlock      = 512;

int g_fails = 0;
void check(bool ok, const std::string& what) {
   std::printf("  %s: %s\n", ok ? "ok  " : "FAIL", what.c_str());
   if (!ok) ++g_fails;
}

void setVM(VoiceManager& vm, MIDIParamItem item, float v) {
   vm.setVoiceManagerParameter(MODULE_PARAM_ADDRESS(ModuleParamKindMIDI, ModuleInstance1, item), v);
}
void setBank(VoiceManager& vm, ModalBankParamItem item, float v) {
   vm.setParameter(MODULE_PARAM_ADDRESS(ModuleParamKindModalBank, ModuleInstance1, item), v);
}

// Poly, one Harmonic mode per voice, instant pitch slew (as the plugin sets it in poly).
void configure(VoiceManager& vm, bool mono = false) {
   setVM(vm, MIDIParamItemVoiceCount, mono ? 1.0f : 10.0f);
   setVM(vm, MIDIParamItemLegato, mono ? 1.0f : 0.0f);
   setVM(vm, MIDIParamItemChannelFilterValue, 0);
   setVM(vm, MIDIParamItemGroupFilterValue, 0);
   setVM(vm, MIDIParamItemVoiceStealAlgorithm, 0);
   setVM(vm, MIDIParamItemFreeRun, 0);
   vm.setParameter(slew1RiseAddress, 1e-8f);
   vm.setParameter(slew1FallAddress, 1e-8f);
   setBank(vm, ModalBankParamItemMaterial, float(ModalBankMaterialGen0));
   setBank(vm, ModalBankParamItemModeCount, 1.0f);
   setBank(vm, ModalBankParamItemDecayT60, 60.0f);
   setBank(vm, ModalBankParamItemSpread, 0.0f);
   // These tests measure the object: hear it through the room's direct path alone, in mono.
   for (const auto& [item, v] : { std::pair{RoomParamItemEarly, 0.0f}, std::pair{RoomParamItemLate, 0.0f},
                                  std::pair{RoomParamItemMicWidth, 0.0f} })
      vm.setParameter(MODULE_PARAM_ADDRESS(ModuleParamKindRoom, ModuleInstance1, item), v);
}

std::vector<float> render(VoiceManager& vm, uint32_t frames) {
   std::vector<float> out;
   out.reserve(frames);
   float L[kBlock], R[kBlock];
   while (out.size() < frames) {
      const uint32_t n = std::min<uint32_t>(kBlock, frames - uint32_t(out.size()));
      std::fill_n(L, n, 0.0f);
      std::fill_n(R, n, 0.0f);
      vm.process(n, L, R);
      out.insert(out.end(), L, L + n);
   }
   return out;
}

// Frequency from interpolated upward zero crossings over the settled part of the buffer.
double estimateFrequency(const std::vector<float>& s) {
   const size_t start = s.size() / 10;
   double first = -1.0, last = -1.0;
   int crossings = 0;
   for (size_t i = start + 1; i < s.size(); ++i) {
      if (s[i - 1] < 0.0f && s[i] >= 0.0f) {
         const double t = double(i - 1) + double(-s[i - 1]) / double(s[i] - s[i - 1]);
         if (first < 0.0) first = t; else ++crossings;
         last = t;
      }
   }
   return crossings > 0 ? kSampleRate * crossings / (last - first) : 0.0;
}

// Hann-windowed single-bin DFT magnitude over the settled part — tells which of several
// simultaneous notes are sounding, where a zero-crossing count cannot.
double toneLevel(const std::vector<float>& s, double hz) {
   const size_t start = s.size() / 4;
   const size_t n = s.size() - start;
   const double w = 2.0 * M_PI * hz / kSampleRate;
   double re = 0.0, im = 0.0;
   for (size_t i = 0; i < n; ++i) {
      const double hann = 0.5 - 0.5 * std::cos(2.0 * M_PI * double(i) / double(n - 1));
      const double x = double(s[start + i]) * hann;
      re += x * std::cos(w * double(i));
      im -= x * std::sin(w * double(i));
   }
   return 2.0 * std::sqrt(re * re + im * im) / (0.5 * double(n));
}

bool near(double measured, double expected) { return std::fabs(measured - expected) / expected < 0.001; }
double semitonesUp(double hz, double semis) { return hz * std::pow(2.0, semis / 12.0); }
std::string hz(double v) { return std::to_string(v) + " Hz"; }
std::string num(double v) { return std::to_string(v); }
std::string num(uint32_t v) { return std::to_string(v); }

// Any MIDI 2.0 channel-voice UMP; word0's low byte is the attribute type / per-note controller
// index / per-note management flags depending on the status.
pm::UmpMessage makeUmp(uint8_t status, uint8_t channel, uint8_t note, uint8_t lowByte, uint32_t word1) {
   const uint32_t word0 = (0x4u << 28) | (uint32_t(status & 0xF) << 20) | (uint32_t(channel & 0xF) << 16)
                        | (uint32_t(note) << 8) | uint32_t(lowByte);
   const uint32_t words[2] = {word0, word1};
   return pm::decodeUmp(words);
}
pm::UmpMessage noteOn(uint8_t note, uint8_t channel = 0) {
   return makeUmp(pm::kStatusNoteOn, channel, note, 0, 0xC000u << 16);
}
pm::UmpMessage noteOff(uint8_t note, uint8_t channel = 0) {
   return makeUmp(pm::kStatusNoteOff, channel, note, 0, 0);
}
uint32_t bendWord(double semitones, double range) {
   const double word = std::round(2147483648.0 + semitones / range * 2147483648.0);
   return word >= 4294967295.0 ? 0xFFFFFFFFu : word <= 0.0 ? 0u : uint32_t(word);   // full scale up / down
}
// Most checks bend further than the ±2 default, so they first announce ±48 with perNoteRange().
pm::UmpMessage perNoteBend(uint8_t note, double semitones, uint8_t channel = 0, double range = 48.0) {
   return makeUmp(pm::kStatusPerNotePitchBend, channel, note, 0, bendWord(semitones, range));
}
// Registered controller bank 0 / index 7 (per-note pitch bend sensitivity), encoded as KeyboardEDO
// does: semitones in bits 31-25, cents in bits 24-18.
pm::UmpMessage perNoteRange(double semitones, uint8_t channel = 0) {
   const uint32_t whole = uint32_t(semitones);
   const uint32_t cents = uint32_t(std::lround((semitones - whole) * 100.0));
   return makeUmp(pm::kStatusRegController, channel, 0, 7, (whole << 25) | (cents << 18));
}

void testDecode() {
   std::printf("MIDI 2.0 per-note decode:\n");
   const pm::UmpMessage bend = makeUmp(pm::kStatusPerNotePitchBend, 3, 61, 0, 0xC0000000u);
   check(pm::isPerNoteMessage(bend) && bend.note == 61 && bend.channel == 3 && bend.data32 == 0xC0000000u,
         "per-note pitch bend keeps note, channel and 32-bit value");
   check(pm::pitchBendToUnit(0x80000000u) == 0.0 && pm::pitchBendToUnit(0xC0000000u) == 0.5
             && pm::pitchBendToUnit(0) == -1.0,
         "bend centre is exactly 0, a quarter of the range up is 0.5, the bottom is -1");
   const pm::UmpMessage rpnc = makeUmp(pm::kStatusRegPerNoteController, 0, 60, 74, 123u);
   check(pm::isPerNoteMessage(rpnc) && rpnc.ccIndex == 74 && rpnc.note == 60 && rpnc.ccData == 123u,
         "registered per-note controller: index from the low byte, note from the note byte");
   const pm::UmpMessage cc = makeUmp(pm::kStatusControlChange, 0, 74, 0, 5u);
   check(!pm::isPerNoteMessage(cc) && cc.ccIndex == 74, "control change still takes its index from the note byte");
   const pm::UmpMessage mgmt = makeUmp(pm::kStatusPerNoteManagement, 0, 60, pm::kPerNoteMgmtReset, 0);
   check(pm::isPerNoteMessage(mgmt) && mgmt.optionFlags == pm::kPerNoteMgmtReset,
         "per-note management carries its option flags");
   const pm::UmpMessage rc7 = perNoteRange(2.5, 4);
   check(rc7.status == pm::kStatusRegController && rc7.note == 0 && rc7.ccIndex == 7 && rc7.channel == 4
             && !pm::isPerNoteMessage(rc7),
         "registered controller: bank from the note byte, index from the low byte, channel-wide");
   const pm::UmpMessage m1bend = pm::fromMidi1Bytes(0xE2, 0x00, 0x40);
   check(m1bend.status == pm::kStatusPitchBend && m1bend.channel == 2 && m1bend.data32 == 0x80000000u,
         "MIDI 1.0 pitch bend decodes to a centred 32-bit bend");
}

void testUnbentPitch() {
   std::printf("Reference pitch (no bend):\n");
   VoiceManager vm(kSampleRate, kBlock);
   configure(vm);
   vm.handleMIDIMessage(noteOn(60));
   const double measured = estimateFrequency(render(vm, uint32_t(kSampleRate)));
   check(near(measured, pm::noteToFrequency(60)),
         "a single Harmonic mode rings at the note's fundamental (" + hz(measured) + ")");
}

void testChannelBend() {
   std::printf("Channel pitch bend:\n");
   VoiceManager vm(kSampleRate, kBlock);
   configure(vm);
   vm.handleMIDIMessage(pm::fromMidi1Bytes(0xE0, 0x00, 0x60));   // MIDI 1.0, half way up: +1 semitone
   vm.handleMIDIMessage(pm::fromMidi1Bytes(0x90, 60, 100));      // a bend sent before the note applies
   const double expected = semitonesUp(pm::noteToFrequency(60), 1.0);
   const double measured = estimateFrequency(render(vm, uint32_t(kSampleRate)));
   check(near(measured, expected),
         "+1 semitone channel bend at the default ±2 range (" + hz(measured) + " vs " + hz(expected) + ")");

   VoiceManager other(kSampleRate, kBlock);
   configure(other);
   other.handleMIDIMessage(pm::fromMidi1Bytes(0x90, 60, 100));
   other.handleMIDIMessage(pm::fromMidi1Bytes(0xE1, 0x7F, 0x7F));
   const double unbent = estimateFrequency(render(other, uint32_t(kSampleRate)));
   check(near(unbent, pm::noteToFrequency(60)), "a channel-2 bend leaves a channel-1 note alone (" + hz(unbent) + ")");

   // The bend reaches a note that is already ringing (de-zippered, so measure after it settles).
   VoiceManager live(kSampleRate, kBlock);
   configure(live);
   live.handleMIDIMessage(noteOn(60));
   render(live, 4096);
   live.handleMIDIMessage(makeUmp(pm::kStatusPitchBend, 0, 0, 0, bendWord(-2.0, 2.0)));
   const double down = estimateFrequency(render(live, uint32_t(kSampleRate)));
   check(near(down, semitonesUp(pm::noteToFrequency(60), -2.0)),
         "a MIDI 2.0 channel bend retunes a ringing note, full range down (" + hz(down) + ")");
}

void testPerNoteBend() {
   std::printf("MIDI 2.0 per-note pitch bend:\n");
   const double c4 = pm::noteToFrequency(60), g4 = pm::noteToFrequency(67);
   const double cSharp4 = semitonesUp(c4, 1.0);

   VoiceManager vm(kSampleRate, kBlock);
   configure(vm);
   vm.handleMIDIMessage(perNoteRange(48.0));
   vm.handleMIDIMessage(noteOn(60));
   vm.handleMIDIMessage(noteOn(67));
   vm.handleMIDIMessage(perNoteBend(60, 1.0));
   const std::vector<float> chord = render(vm, uint32_t(kSampleRate));
   const double bent = toneLevel(chord, cSharp4), old = toneLevel(chord, c4), other = toneLevel(chord, g4);
   check(bent > 0.01 && old < 0.1 * bent,
         "+1 semitone on C4 moves it to C#4 (C#4 " + num(bent) + ", C4 " + num(old) + ")");
   check(other > 0.5 * bent, "the other held note (G4) is untouched (" + num(other) + ")");

   // Two notes on the SAME key but different channels are different notes.
   VoiceManager same(kSampleRate, kBlock);
   configure(same);
   same.handleMIDIMessage(perNoteRange(48.0, 1));
   same.handleMIDIMessage(noteOn(60, 0));
   same.handleMIDIMessage(noteOn(60, 1));
   check(same.activeCount() == 2, "C4 on channels 1 and 2 takes two voices (got " + num(same.activeCount()) + ")");
   same.handleMIDIMessage(perNoteBend(60, 12.0, 1));
   const std::vector<float> pair = render(same, uint32_t(kSampleRate));
   const double c5 = pm::noteToFrequency(72);
   check(toneLevel(pair, c4) > 0.01 && toneLevel(pair, c5) > 0.01,
         "a per-note bend on channel 2's C4 leaves channel 1's C4 in place (C4 " + num(toneLevel(pair, c4))
             + ", C5 " + num(toneLevel(pair, c5)) + ")");
   // Short decay so a released note rings out quickly; the held one stays Active regardless.
   setBank(same, ModalBankParamItemDecayT60, 0.05f);
   same.handleMIDIMessage(noteOff(60, 1));
   render(same, uint32_t(kSampleRate));
   check(same.activeCount() == 1, "a note-off on channel 2 releases only channel 2's note (active="
                                      + num(same.activeCount()) + ")");

   // Per-note bend must not survive into the voice's next note.
   VoiceManager reuse(kSampleRate, kBlock);
   configure(reuse);
   setVM(reuse, MIDIParamItemVoiceCount, 1.0f);   // the next note lands on the same voice
   reuse.handleMIDIMessage(perNoteRange(48.0));
   reuse.handleMIDIMessage(noteOn(60));
   reuse.handleMIDIMessage(perNoteBend(60, 2.0));
   render(reuse, 4096);
   reuse.handleMIDIMessage(noteOff(60));
   reuse.handleMIDIMessage(noteOn(60));
   const double fresh = estimateFrequency(render(reuse, uint32_t(kSampleRate)));
   check(near(fresh, c4), "per-note bend resets on the next note-on (" + hz(fresh) + ")");

   // Per-note management with the Reset flag recentres the bend.
   VoiceManager mgmt(kSampleRate, kBlock);
   configure(mgmt);
   mgmt.handleMIDIMessage(perNoteRange(48.0));
   mgmt.handleMIDIMessage(noteOn(60));
   mgmt.handleMIDIMessage(perNoteBend(60, 3.0));
   render(mgmt, 4096);
   mgmt.handleMIDIMessage(makeUmp(pm::kStatusPerNoteManagement, 0, 60, pm::kPerNoteMgmtReset, 0));
   const double reset = estimateFrequency(render(mgmt, uint32_t(kSampleRate)));
   check(near(reset, c4), "per-note management Reset recentres the bend (" + hz(reset) + ")");

   // Mono: a bend for a held-but-silent note in the stack must not move the sounding note.
   VoiceManager mono(kSampleRate, kBlock);
   configure(mono, true);
   mono.handleMIDIMessage(perNoteRange(48.0));
   mono.handleMIDIMessage(noteOn(60));
   mono.handleMIDIMessage(noteOn(64));
   mono.handleMIDIMessage(perNoteBend(60, 5.0));
   const double e4 = pm::noteToFrequency(64);
   const double sounding = estimateFrequency(render(mono, uint32_t(kSampleRate)));
   check(near(sounding, e4), "mono: bending the held-but-silent C4 leaves the sounding E4 alone (" + hz(sounding) + ")");
   mono.handleMIDIMessage(perNoteBend(64, -4.0));
   render(mono, 4096);
   const double bentE = estimateFrequency(render(mono, uint32_t(kSampleRate)));
   check(near(bentE, c4), "mono: bending the sounding E4 down 4 semitones (" + hz(bentE) + ")");
}

// Per-note bend range: with nothing set it is the channel's bend range (±2), per M2-104 v1.0's
// "acts like Pitch Bend in every way"; MIDI 2.0 registered controller 7 sets it per channel.
void testPerNoteBendRange() {
   std::printf("Per-note pitch bend range:\n");
   const double c4 = pm::noteToFrequency(60);

   VoiceManager vm(kSampleRate, kBlock);
   configure(vm);
   vm.handleMIDIMessage(noteOn(60));
   vm.handleMIDIMessage(perNoteBend(60, 1.0, 0, 2.0));   // half way up
   const double bent = estimateFrequency(render(vm, uint32_t(kSampleRate)));
   check(near(bent, semitonesUp(c4, 1.0)),
         "with no RC 7, per-note bend uses the channel range: half way at ±2 is +1 semitone (" + hz(bent) + ")");

   VoiceManager wide(kSampleRate, kBlock);
   configure(wide);
   wide.handleMIDIMessage(perNoteRange(12.0));
   wide.handleMIDIMessage(noteOn(60));
   wide.handleMIDIMessage(perNoteBend(60, 6.0, 0, 12.0));   // half way up at ±12
   const double wideBent = estimateFrequency(render(wide, uint32_t(kSampleRate)));
   check(near(wideBent, semitonesUp(c4, 6.0)), "RC 7 = 12: half way is +6 semitones (" + hz(wideBent) + ")");

   VoiceManager other(kSampleRate, kBlock);
   configure(other);
   other.handleMIDIMessage(perNoteRange(12.0, 5));          // a different channel
   other.handleMIDIMessage(noteOn(60));
   other.handleMIDIMessage(perNoteBend(60, 1.0, 0, 2.0));
   const double untouched = estimateFrequency(render(other, uint32_t(kSampleRate)));
   check(near(untouched, semitonesUp(c4, 1.0)),
         "a range set on channel 6 leaves channel 1 at the ±2 default (" + hz(untouched) + ")");

   VoiceManager cents(kSampleRate, kBlock);
   configure(cents);
   cents.handleMIDIMessage(perNoteRange(2.5));
   cents.handleMIDIMessage(noteOn(60));
   cents.handleMIDIMessage(makeUmp(pm::kStatusPerNotePitchBend, 0, 60, 0, 0xFFFFFFFFu));   // full up
   const double full = estimateFrequency(render(cents, uint32_t(kSampleRate)));
   check(std::fabs(full / c4 - std::pow(2.0, 2.5 / 12.0)) < 0.001,
         "the cents field counts: full scale at 2 semitones 50 cents (" + hz(full) + ")");

   VoiceManager kept(kSampleRate, kBlock);
   configure(kept);
   kept.handleMIDIMessage(perNoteRange(12.0));
   kept.reset();                                          // a host reset, e.g. on transport stop
   kept.handleMIDIMessage(noteOn(60));
   kept.handleMIDIMessage(perNoteBend(60, 6.0, 0, 12.0));
   const double afterReset = estimateFrequency(render(kept, uint32_t(kSampleRate)));
   check(near(afterReset, semitonesUp(c4, 6.0)), "the RC 7 range survives a reset (" + hz(afterReset) + ")");
}

// Registered per-note controller #3, Pitch 7.25, for `semitones` (absolute, as a note number).
pm::UmpMessage pitch725(uint8_t note, double semitones, uint8_t channel = 0) {
   return makeUmp(pm::kStatusRegPerNoteController, channel, note, pm::kRegPerNotePitch7_25,
                  uint32_t(std::llround(semitones * 33554432.0)));
}

void testPitch725() {
   std::printf("Pitch 7.25 (registered per-note controller #3):\n");
   const double c4 = pm::noteToFrequency(60);

   VoiceManager vm(kSampleRate, kBlock);
   configure(vm);
   vm.handleMIDIMessage(noteOn(60));
   vm.handleMIDIMessage(pitch725(60, 67.25));
   const double g4 = estimateFrequency(render(vm, uint32_t(kSampleRate)));
   check(near(g4, semitonesUp(c4, 7.25)), "C4 retuned to 67.25 sounds there (" + hz(g4) + ")");
   vm.handleMIDIMessage(perNoteBend(60, 1.0, 0, 2.0));
   render(vm, 4096);
   const double bentG = estimateFrequency(render(vm, uint32_t(kSampleRate)));
   check(near(bentG, semitonesUp(c4, 8.25)), "a per-note bend offsets from the Pitch 7.25 pitch (" + hz(bentG) + ")");

   // Persistent per key: the next note-on on that key starts at the stored pitch.
   VoiceManager next(kSampleRate, kBlock);
   configure(next);
   setVM(next, MIDIParamItemVoiceCount, 1.0f);
   next.handleMIDIMessage(noteOn(60));
   next.handleMIDIMessage(pitch725(60, 72.0));
   render(next, 4096);
   next.handleMIDIMessage(noteOff(60));
   next.handleMIDIMessage(noteOn(60));
   const double kept = estimateFrequency(render(next, uint32_t(kSampleRate)));
   check(near(kept, semitonesUp(c4, 12.0)), "the next note-on on that key starts at its Pitch 7.25 (" + hz(kept) + ")");

   // Per-Note Management Reset (KeyboardEDO sends one before each note-on) clears the key.
   next.handleMIDIMessage(noteOff(60));
   next.handleMIDIMessage(makeUmp(pm::kStatusPerNoteManagement, 0, 60, pm::kPerNoteMgmtReset, 0));
   next.handleMIDIMessage(noteOn(60));
   const double fresh = estimateFrequency(render(next, uint32_t(kSampleRate)));
   check(near(fresh, c4), "after a Per-Note Management Reset the key starts untuned (" + hz(fresh) + ")");

   // Relative to a Pitch 7.9 note-on, Pitch 7.25 still lands on the absolute pitch it names.
   VoiceManager micro(kSampleRate, kBlock);
   configure(micro);
   micro.handleMIDIMessage(makeUmp(pm::kStatusNoteOn, 0, 60, pm::kAttrPitch7_9, (0xC000u << 16) | (60u << 9 | 256u)));
   micro.handleMIDIMessage(pitch725(60, 64.0));
   const double e4 = estimateFrequency(render(micro, uint32_t(kSampleRate)));
   check(near(e4, semitonesUp(c4, 4.0)), "on a 7.9 note (60.5), Pitch 7.25 = 64 sounds E4 (" + hz(e4) + ")");
}

// Pitch 7.25 as a tuning table: sent with no note sounding, kept per key and channel, through reset().
void testKeyTuning() {
   std::printf("Pitch 7.25 per-key tuning table:\n");
   const double c4 = pm::noteToFrequency(60);

   VoiceManager vm(kSampleRate, kBlock);
   configure(vm);
   vm.handleMIDIMessage(pitch725(60, 63.5));   // before any note: no voice owns key 60 yet
   vm.handleMIDIMessage(noteOn(60));
   const double tuned = estimateFrequency(render(vm, uint32_t(kSampleRate)));
   check(near(tuned, semitonesUp(c4, 3.5)), "a Pitch 7.25 sent before the note-on tunes it (" + hz(tuned) + ")");

   vm.handleMIDIMessage(noteOff(60));
   vm.reset();
   vm.handleMIDIMessage(noteOn(60));
   const double afterReset = estimateFrequency(render(vm, uint32_t(kSampleRate)));
   check(near(afterReset, semitonesUp(c4, 3.5)), "the table survives a reset (" + hz(afterReset) + ")");

   VoiceManager chan(kSampleRate, kBlock);
   configure(chan);
   chan.handleMIDIMessage(pitch725(60, 67.0, 1));   // channel 2's key 60 only
   chan.handleMIDIMessage(noteOn(60, 0));
   const double ch1 = estimateFrequency(render(chan, uint32_t(kSampleRate)));
   check(near(ch1, c4), "a channel-2 entry leaves channel 1's key alone (" + hz(ch1) + ")");

   // A Pitch 7.9 attribute names the note's pitch, so it wins over the table.
   VoiceManager micro(kSampleRate, kBlock);
   configure(micro);
   micro.handleMIDIMessage(pitch725(60, 67.0));
   micro.handleMIDIMessage(makeUmp(pm::kStatusNoteOn, 0, 60, pm::kAttrPitch7_9, (0xC000u << 16) | (62u << 9)));
   const double d4 = estimateFrequency(render(micro, uint32_t(kSampleRate)));
   check(near(d4, semitonesUp(c4, 2.0)), "a Pitch 7.9 note-on ignores the table (" + hz(d4) + ")");

   // CLAP tuning lasts for its note only.
   VoiceManager clap(kSampleRate, kBlock);
   configure(clap);
   setVM(clap, MIDIParamItemVoiceCount, 1.0f);
   clap.handleMIDIMessage(noteOn(60));
   clap_event_note_expression_t ev{};
   ev.header.size = sizeof(ev);
   ev.header.type = CLAP_EVENT_NOTE_EXPRESSION;
   ev.expression_id = CLAP_NOTE_EXPRESSION_TUNING;
   ev.note_id = -1;
   ev.port_index = 0;
   ev.channel = 0;
   ev.key = 60;
   ev.value = 5.0;
   clap.handleMIDIMessage(*pm::fromClapNoteExpression(ev, 60));
   const double f4 = estimateFrequency(render(clap, uint32_t(kSampleRate)));
   check(near(f4, semitonesUp(c4, 5.0)), "CLAP tuning +5 retunes the sounding note (" + hz(f4) + ")");
   clap.handleMIDIMessage(noteOff(60));
   clap.handleMIDIMessage(noteOn(60));
   const double back = estimateFrequency(render(clap, uint32_t(kSampleRate)));
   check(near(back, c4), "...and the next note-on on that key is untuned (" + hz(back) + ")");
}

// CLAP note ids: when both messages carry one, the id decides which note is meant.
void testNoteIds() {
   std::printf("CLAP note ids:\n");
   const double c4 = pm::noteToFrequency(60);
   auto withId = [](pm::UmpMessage m, int32_t id) { m.noteId = id; return m; };

   VoiceManager vm(kSampleRate, kBlock);
   configure(vm);
   vm.handleMIDIMessage(withId(noteOn(60), 1));
   vm.handleMIDIMessage(withId(noteOn(60), 2));          // the same key, struck again as a new note
   check(vm.activeCount() == 2, "the same key with two ids takes two voices (got " + num(vm.activeCount()) + ")");
   check(vm.keyForNoteId(2) == 60 && vm.keyForNoteId(9) == -1, "keyForNoteId finds a sounding id, and only that");
   vm.handleMIDIMessage(withId(pitch725(60, 72.0), 2));
   const std::vector<float> pair = render(vm, uint32_t(kSampleRate));
   const double c5 = pm::noteToFrequency(72);
   check(toneLevel(pair, c4) > 0.01 && toneLevel(pair, c5) > 0.01,
         "an expression for id 2 moves only id 2 (C4 " + num(toneLevel(pair, c4)) + ", C5 " + num(toneLevel(pair, c5)) + ")");
   setBank(vm, ModalBankParamItemDecayT60, 0.05f);
   vm.handleMIDIMessage(withId(noteOff(60), 1));
   render(vm, uint32_t(kSampleRate));
   check(vm.activeCount() == 1 && vm.keyForNoteId(2) == 60, "a note-off for id 1 releases only id 1");
}

void testClapNoteExpressionConversion() {
   std::printf("CLAP note expression conversion:\n");
   auto expr = [](clap_note_expression id, int32_t noteId, int16_t key, double value) {
      clap_event_note_expression_t e{};
      e.header.size = sizeof(e);
      e.header.type = CLAP_EVENT_NOTE_EXPRESSION;
      e.expression_id = id; e.note_id = noteId; e.port_index = 0; e.channel = 0; e.key = key; e.value = value;
      return e;
   };
   const auto tuning = pm::fromClapNoteExpression(expr(CLAP_NOTE_EXPRESSION_TUNING, 5, 60, 7.5), 60);
   check(tuning && tuning->status == pm::kStatusRegPerNoteController && tuning->ccIndex == pm::kRegPerNotePitch7_25
             && tuning->ccData == uint32_t(67.5 * 33554432.0) && tuning->noteId == 5,
         "tuning +7.5 on key 60 becomes Pitch 7.25 = 67.5, keeping the note id");
   const auto big = pm::fromClapNoteExpression(expr(CLAP_NOTE_EXPRESSION_TUNING, 5, 60, 100.0), 60);
   check(big && big->ccData == 0xFFFFFFFFu, "tuning past the top of Pitch 7.25's range clamps to it");
   check(!pm::fromClapNoteExpression(expr(CLAP_NOTE_EXPRESSION_TUNING, 5, -1, 2.0), -1),
         "id-only tuning with no known key is dropped");
   const auto pressure = pm::fromClapNoteExpression(expr(CLAP_NOTE_EXPRESSION_PRESSURE, 5, -1, 1.0), -1);
   check(pressure && pressure->status == pm::kStatusPolyPressure && pressure->data32 == 0xFFFFFFFFu,
         "pressure becomes poly pressure, and routes by id alone");
   const auto bright = pm::fromClapNoteExpression(expr(CLAP_NOTE_EXPRESSION_BRIGHTNESS, -1, 64, 0.5), 64);
   check(bright && bright->status == pm::kStatusRegPerNoteController && bright->ccIndex == pm::kRegPerNoteBrightness,
         "brightness becomes registered per-note controller #74");
   check(!pm::fromClapNoteExpression(expr(CLAP_NOTE_EXPRESSION_VOLUME, 5, 60, 1.0), 60),
         "volume has no destination");
}

// holdsNote: what the plugin's CLAP_EVENT_NOTE_END reporting relies on.
void testHoldsNote() {
   std::printf("Note lifetime (holdsNote, for CLAP note end):\n");
   VoiceManager ring(kSampleRate, kBlock);
   configure(ring);
   setBank(ring, ModalBankParamItemDecayT60, 0.3f);
   ring.handleMIDIMessage(noteOn(60));
   ring.handleMIDIMessage(noteOff(60));
   render(ring, 2048);
   check(ring.holdsNote(noteOn(60)), "a released note is still held while it rings");
   render(ring, uint32_t(2.0 * kSampleRate));
   check(!ring.holdsNote(noteOn(60)), "...and not once it has rung down");

   VoiceManager mono(kSampleRate, kBlock);
   configure(mono, true);
   mono.handleMIDIMessage(noteOn(60));
   mono.handleMIDIMessage(noteOn(64));
   render(mono, 2048);
   check(mono.holdsNote(noteOn(60)) && mono.holdsNote(noteOn(64)),
         "mono: a held-but-silent note in the stack is still held (it can come back)");
   mono.handleMIDIMessage(noteOff(60));
   check(!mono.holdsNote(noteOn(60)) && mono.holdsNote(noteOn(64)), "mono: releasing it from the stack ends it");

   VoiceManager steal(kSampleRate, kBlock);
   configure(steal);
   setVM(steal, MIDIParamItemVoiceCount, 1.0f);           // poly with one voice: the next note steals
   steal.handleMIDIMessage(noteOn(60));
   render(steal, 2048);
   steal.handleMIDIMessage(noteOn(67));
   check(steal.holdsNote(noteOn(67)), "a stealing note is held from its note-on");
   render(steal, 4096);
   check(!steal.holdsNote(noteOn(60)) && steal.holdsNote(noteOn(67)), "the stolen note ends once the steal completes");
}

// --- MPE (lower zone: master channel 1, members 2..16) and RPN 0 --------------------------------
// Ported from PlainOldSynth's smoke test (bend only: StruckRoom has no pressure / timbre sources).

void setMpe(VoiceManager& vm, bool on) {
   vm.setParameter(MODULE_PARAM_ADDRESS(ModuleParamKindPitchBendConverter, ModuleInstance1,
                                        PitchBendConverterParamItemMPE), on ? 1.0f : 0.0f);
}
pm::UmpMessage midi1Bend(uint8_t channel, uint32_t value14) {
   return pm::fromMidi1Bytes(uint8_t(0xE0 | channel), uint8_t(value14 & 0x7F), uint8_t(value14 >> 7));
}
pm::UmpMessage midi1CC(uint8_t channel, uint8_t cc, uint8_t value) {
   return pm::fromMidi1Bytes(uint8_t(0xB0 | channel), cc, value);
}
void rpn0(VoiceManager& vm, uint8_t channel, uint8_t semis) {
   vm.handleMIDIMessage(midi1CC(channel, 101, 0)); vm.handleMIDIMessage(midi1CC(channel, 100, 0));
   vm.handleMIDIMessage(midi1CC(channel, 6, semis)); vm.handleMIDIMessage(midi1CC(channel, 38, 0));
   vm.handleMIDIMessage(midi1CC(channel, 101, 127)); vm.handleMIDIMessage(midi1CC(channel, 100, 127));
}
void mpeConfiguration(VoiceManager& vm) {   // RPN 6 on the master channel, 15 member channels
   vm.handleMIDIMessage(midi1CC(0, 101, 0)); vm.handleMIDIMessage(midi1CC(0, 100, 6));
   vm.handleMIDIMessage(midi1CC(0, 6, 15));
   vm.handleMIDIMessage(midi1CC(0, 101, 127)); vm.handleMIDIMessage(midi1CC(0, 100, 127));
}
// Play C4 on `channel` and return how far it sounds from C4, in semitones.
double semitonesFromC4(VoiceManager& vm, uint8_t channel) {
   vm.handleMIDIMessage(pm::fromMidi1Bytes(uint8_t(0x90 | channel), 60, 100));
   const double f = estimateFrequency(render(vm, uint32_t(kSampleRate)));
   return 12.0 * std::log2(f / pm::noteToFrequency(60));
}

void testMpe() {
   std::printf("MPE and RPN 0:\n");
   auto close = [](double a, double b) { return std::fabs(a - b) < 0.01; };
   const uint8_t kMaster = 0, kMember = 1, kMember2 = 2;   // channels 1, 2, 3
   auto fresh = [](bool mpe) {
      auto vm = std::make_unique<VoiceManager>(kSampleRate, kBlock);
      configure(*vm);
      setMpe(*vm, mpe);
      return vm;
   };

   {  // A member-channel bend meant as +3 at ±48 (8192 + 512).
      auto off = fresh(false);
      off->handleMIDIMessage(midi1Bend(kMember, 8192 + 512));
      const double o = semitonesFromC4(*off, kMember);
      check(close(o, 0.125), "MPE off: that bend is read at ±2, i.e. +0.125 st (" + num(o) + ")");
      auto on = fresh(true);
      on->handleMIDIMessage(midi1Bend(kMember, 8192 + 512));
      const double n = semitonesFromC4(*on, kMember);
      check(close(n, 3.0), "MPE on: a member channel bends at ±48, i.e. +3 st (" + num(n) + ")");
   }
   {  // The master channel's bend (±2) adds to every member note's own.
      auto vm = fresh(true);
      vm->handleMIDIMessage(midi1Bend(kMaster, 8192 + 4096));   // +1 st at ±2
      vm->handleMIDIMessage(midi1Bend(kMember, 8192 + 512));    // +3 st at ±48
      const double both = semitonesFromC4(*vm, kMember);
      check(close(both, 4.0), "MPE on: master +1 st plus member +3 st = +4 st (" + num(both) + ")");
      auto plain = fresh(false);
      plain->handleMIDIMessage(midi1Bend(kMaster, 8192 + 4096));
      const double none = semitonesFromC4(*plain, kMember);
      check(close(none, 0.0), "MPE off: a channel-1 bend leaves a channel-2 note alone (" + num(none) + ")");
   }
   {  // RPN 0 outside MPE: ±12, then half way up.
      auto vm = fresh(false);
      rpn0(*vm, kMaster, 12);
      vm->handleMIDIMessage(midi1Bend(kMaster, 8192 + 4096));
      const double half = semitonesFromC4(*vm, kMaster);
      check(close(half, 6.0), "RPN 0 sets the bend range: half way up at ±12 is +6 st (" + num(half) + ")");
   }
   {  // MPE: RPN 0 on one member channel sets every member channel.
      auto vm = fresh(true);
      rpn0(*vm, kMember, 24);
      vm->handleMIDIMessage(midi1Bend(kMember2, 8192 + 1024));   // +3 st at ±24
      const double other = semitonesFromC4(*vm, kMember2);
      check(close(other, 3.0), "MPE on: RPN 0 = 24 sent on channel 2 also sets channel 3 (" + num(other) + ")");
   }
   {  // MIDI 2.0 RPN 0 (registered controller, bank 0 index 0).
      auto vm = fresh(false);
      vm->handleMIDIMessage(makeUmp(pm::kStatusRegController, kMaster, 0, 0, 12u << 25));
      vm->handleMIDIMessage(midi1Bend(kMaster, 8192 + 4096));
      const double half = semitonesFromC4(*vm, kMaster);
      check(close(half, 6.0), "MIDI 2.0 RPN 0 sets the bend range too (" + num(half) + ")");
   }
   {  // RPN 0 is configuration: it survives a host reset, like RC 7.
      auto vm = fresh(false);
      rpn0(*vm, kMaster, 12);
      vm->reset();
      vm->handleMIDIMessage(midi1Bend(kMaster, 8192 + 4096));
      const double half = semitonesFromC4(*vm, kMaster);
      check(close(half, 6.0), "the RPN 0 range survives a reset (" + num(half) + ")");
   }
   {  // The MPE Configuration Message puts set ranges back to default.
      auto vm = fresh(false);
      rpn0(*vm, kMaster, 12);
      mpeConfiguration(*vm);
      vm->handleMIDIMessage(midi1Bend(kMaster, 8192 + 4096));
      const double half = semitonesFromC4(*vm, kMaster);
      check(close(half, 1.0), "the MPE Configuration Message resets the range to ±2 (" + num(half) + ")");
   }
   {  // So does flipping the switch: a range set under MPE must not leak into non-MPE play.
      auto vm = fresh(true);
      rpn0(*vm, kMember, 48);
      render(*vm, 512);
      setMpe(*vm, false);
      vm->handleMIDIMessage(midi1Bend(kMember2, 8192 + 4096));
      const double half = semitonesFromC4(*vm, kMember2);
      check(close(half, 1.0), "turning MPE off drops member ranges back to ±2 (" + num(half) + ")");
   }
   {  // mpe-sender's sequence: MPE-on "rpn", its reset, switch off, then "samekey" and "rpn".
      auto vm = fresh(true);
      rpn0(*vm, kMember, 24);
      render(*vm, 2048);
      mpeConfiguration(*vm);
      setMpe(*vm, false);
      render(*vm, 2048);
      vm->handleMIDIMessage(midi1Bend(5, 8192 + 2048));
      const double samekey = semitonesFromC4(*vm, 5);
      check(close(samekey, 0.5), "sender sequence, MPE off: samekey's bend reads at ±2 (" + num(samekey) + ")");
      // Let samekey's note ring out (a struck note outlives its note-off) before measuring the next.
      setBank(*vm, ModalBankParamItemDecayT60, 0.05f);
      vm->allNotesOff();
      render(*vm, uint32_t(kSampleRate));
      setBank(*vm, ModalBankParamItemDecayT60, 60.0f);
      rpn0(*vm, kMember, 24);
      vm->handleMIDIMessage(midi1Bend(kMember2, 8192 + 4096));
      const double rpnOff = semitonesFromC4(*vm, kMember2);
      check(close(rpnOff, 1.0), "sender sequence, MPE off: rpn leaves channel 3 at ±2 (" + num(rpnOff) + ")");
   }
   {  // A MIDI 2.0 per-note bend on a member channel follows that channel's range: ±48 under MPE.
      auto vm = fresh(true);
      vm->handleMIDIMessage(noteOn(60, kMember));
      vm->handleMIDIMessage(perNoteBend(60, 3.0, kMember, 48.0));
      const double f = estimateFrequency(render(*vm, uint32_t(kSampleRate)));
      const double st = 12.0 * std::log2(f / pm::noteToFrequency(60));
      check(close(st, 3.0), "MPE on: a per-note bend on a member channel is at ±48 (" + num(st) + ")");
   }
}

// --- velocity ------------------------------------------------------------------------------------
float peakOf(const std::vector<float>& s) {
   float p = 0.0f;
   for (float x : s) p = std::max(p, std::fabs(x));
   return p;
}
void setStrike(VoiceManager& vm, StrikeParamItem item, float v) {
   vm.setParameter(MODULE_PARAM_ADDRESS(ModuleParamKindStrike, ModuleInstance1, item), v);
}

void testVelocity() {
   std::printf("Velocity:\n");
   {  // The spec's Min-Center-Max upscaling: MIDI 1.0 velocity 127 is MIDI 2.0 full scale exactly.
      VoiceManager a(kSampleRate, kBlock), b(kSampleRate, kBlock);
      configure(a); configure(b);
      a.handleMIDIMessage(pm::fromMidi1Bytes(0x90, 60, 127));
      b.handleMIDIMessage(makeUmp(pm::kStatusNoteOn, 0, 60, 0, 0xFFFFu << 16));
      const float pa = peakOf(render(a, 4096)), pb = peakOf(render(b, 4096));
      check(pa > 0.01f && pa == pb, "MIDI 1.0 velocity 127 strikes exactly as MIDI 2.0 velocity 0xFFFF ("
                                        + num(double(pa)) + " vs " + num(double(pb)) + ")");
   }
   {  // MIDI 2.0: velocity 0 is a note-on, not a note-off. With Vel>Level at 0 it strikes at full level.
      VoiceManager vm(kSampleRate, kBlock);
      configure(vm);
      setStrike(vm, StrikeParamItemVelToLevel, 0.0f);
      vm.handleMIDIMessage(makeUmp(pm::kStatusNoteOn, 0, 60, 0, 0));
      const float p = peakOf(render(vm, 4096));
      check(vm.activeCount() == 1 && p > 0.01f,
            "a MIDI 2.0 velocity-0 note-on holds a voice and, at Vel>Level 0, sounds (" + num(double(p)) + ")");
      setBank(vm, ModalBankParamItemDecayT60, 0.05f);
      vm.handleMIDIMessage(noteOff(60));
      render(vm, uint32_t(kSampleRate));
      check(vm.activeCount() == 0, "...and its note-off releases it");
   }
   {  // At Vel>Level 1, velocity 0 is silent, but still a held note (not a release of anything).
      VoiceManager vm(kSampleRate, kBlock);
      configure(vm);
      setStrike(vm, StrikeParamItemVelToLevel, 1.0f);
      vm.handleMIDIMessage(noteOn(60));
      render(vm, 2048);
      vm.handleMIDIMessage(makeUmp(pm::kStatusNoteOn, 0, 64, 0, 0));
      render(vm, 2048);
      check(vm.activeCount() == 2 && vm.holdsNote(noteOn(60)),
            "at Vel>Level 1, a velocity-0 note-on is silent but still takes a voice of its own");
   }
}

// --- after note-off: MPE §3.3.3, "control of a note ceases once Note Off has occurred" -----------
// A struck note rings for seconds after its note-off. Senders (mpe-sender included) centre a member
// channel's bend right after the note-off to ready it for the next note; the ringing tail must not
// follow. The master channel's bend still applies. Measured on the tail, after the change.
void testReleasedNotes() {
   std::printf("Released notes (MPE §3.3.3):\n");
   auto close = [](double a, double b) { return std::fabs(a - b) < 0.01; };
   auto tailSemitones = [](VoiceManager& vm) {
      const double f = estimateFrequency(render(vm, uint32_t(kSampleRate)));
      return 12.0 * std::log2(f / pm::noteToFrequency(60));
   };
   auto fresh = [](bool mpe) {
      auto vm = std::make_unique<VoiceManager>(kSampleRate, kBlock);
      configure(*vm);
      setMpe(*vm, mpe);
      return vm;
   };
   {  // samekey's ending: up an octave, note-off, then the channel is centred.
      auto vm = fresh(true);
      vm->handleMIDIMessage(pm::fromMidi1Bytes(0x95, 60, 100));
      vm->handleMIDIMessage(midi1Bend(5, 8192 + 2048));             // +12 at ±48
      render(*vm, 4096);
      vm->handleMIDIMessage(pm::fromMidi1Bytes(0x85, 60, 0));
      vm->handleMIDIMessage(midi1Bend(5, 8192));                    // same instant as the note-off
      const double tail = tailSemitones(*vm);
      check(close(tail, 12.0), "MPE on: centring the member channel after note-off leaves the tail at +12 ("
                                   + num(tail) + ")");
      vm->handleMIDIMessage(midi1Bend(0, 8192 + 4096));             // master: +1 at ±2
      render(*vm, 4096);
      const double withWheel = tailSemitones(*vm);
      check(close(withWheel, 13.0), "...and the master channel's bend still moves it (+13: " + num(withWheel) + ")");
   }
   {  // MPE off: an ordinary pitch wheel bends a releasing note.
      auto vm = fresh(false);
      vm->handleMIDIMessage(pm::fromMidi1Bytes(0x90, 60, 100));
      vm->handleMIDIMessage(midi1Bend(0, 8192 + 4096));             // +1 at ±2
      render(*vm, 4096);
      vm->handleMIDIMessage(pm::fromMidi1Bytes(0x80, 60, 0));
      vm->handleMIDIMessage(midi1Bend(0, 8192));
      render(*vm, 4096);
      const double tail = tailSemitones(*vm);
      check(close(tail, 0.0), "MPE off: the tail follows its channel's bend back to centre (" + num(tail) + ")");
   }
   {  // A note the pedal is holding has had its note-off: it freezes too.
      auto vm = fresh(true);
      vm->handleMIDIMessage(midi1CC(0, 64, 127));
      vm->handleMIDIMessage(pm::fromMidi1Bytes(0x92, 60, 100));
      vm->handleMIDIMessage(midi1Bend(2, 8192 + 2048));             // +12 at ±48
      render(*vm, 4096);
      vm->handleMIDIMessage(pm::fromMidi1Bytes(0x82, 60, 0));       // deferred by the pedal
      vm->handleMIDIMessage(midi1Bend(2, 8192));
      const double tail = tailSemitones(*vm);
      check(close(tail, 12.0), "MPE on: a pedal-held note keeps its bend when the channel is centred (" + num(tail) + ")");
   }
   {  // MIDI 2.0: a sender centring a note's per-note bend after its note-off must not swoop the tail.
      auto vm = fresh(false);
      vm->handleMIDIMessage(noteOn(60));
      vm->handleMIDIMessage(perNoteBend(60, 1.0, 0, 2.0));          // +1
      render(*vm, 4096);
      vm->handleMIDIMessage(noteOff(60));
      vm->handleMIDIMessage(perNoteBend(60, 0.0, 0, 2.0));          // centred, after the note-off
      const double tail = tailSemitones(*vm);
      check(close(tail, 1.0), "MIDI 2.0: a per-note bend after note-off leaves the tail at +1 (" + num(tail) + ")");
   }
   {  // Re-striking a ringing key strikes the same bank again (one voice), so that note takes the bend.
      auto vm = fresh(false);
      vm->handleMIDIMessage(noteOn(60));
      vm->handleMIDIMessage(perNoteBend(60, 1.0, 0, 2.0));
      render(*vm, 4096);
      vm->handleMIDIMessage(noteOff(60));
      vm->handleMIDIMessage(noteOn(60));
      vm->handleMIDIMessage(perNoteBend(60, 2.0, 0, 2.0));          // full scale: +2
      const double restruck = tailSemitones(*vm);
      check(vm->activeCount() == 1 && close(restruck, 2.0),
            "re-striking the ringing key reuses its voice, which follows the new note's bend (" + num(restruck) + ")");
   }
   {  // A CLAP note addressed by its note id still takes expression after its note-off.
      auto vm = fresh(false);
      pm::UmpMessage on = noteOn(60);  on.noteId = 9;
      pm::UmpMessage off = noteOff(60); off.noteId = 9;
      pm::UmpMessage tune = pitch725(60, 67.0); tune.noteId = 9;
      vm->handleMIDIMessage(on);
      render(*vm, 4096);
      vm->handleMIDIMessage(off);
      vm->handleMIDIMessage(tune);
      const double tail = tailSemitones(*vm);
      check(close(tail, 7.0), "CLAP: an id-addressed tuning still reaches the released note (" + num(tail) + ")");
   }
}

// --- Expression: Pressure -> Decay, Timbre (CC74) -> Damp Tilt and Strike Position -------------------
// The plugin's Expression knobs are ModalBank's mod levels; set them directly here.

// T60 of a decaying tone from its RMS in two windows `gap` seconds apart (60 dB / the fall rate).
double measureT60(VoiceManager& vm, double gapSeconds) {
   const uint32_t win = 4800;
   auto rmsOf = [](const std::vector<float>& s) {
      double e = 0.0; for (float x : s) e += double(x) * x; return std::sqrt(e / double(s.size()));
   };
   const double a = rmsOf(render(vm, win));
   render(vm, uint32_t(gapSeconds * kSampleRate) - win);
   const double b = rmsOf(render(vm, win));
   const double dbPerSecond = 20.0 * std::log10(a / b) / gapSeconds;
   return 60.0 / dbPerSecond;
}

void testExpression() {
   std::printf("Expression (Pressure / Timbre):\n");
   auto fresh = [](float t60) {
      auto vm = std::make_unique<VoiceManager>(kSampleRate, kBlock);
      configure(*vm);
      setBank(*vm, ModalBankParamItemDecayT60, t60);
      return vm;
   };
   auto near = [](double measured, double expected) { return std::fabs(measured / expected - 1.0) < 0.05; };
   {  // Press>Decay, acting on a ringing note: full pressure at +2 octaves quadruples the decay time.
      auto vm = fresh(0.5f);
      setBank(*vm, ModalBankParamItemDecayModLevel, 2.0f);
      vm->handleMIDIMessage(pm::fromMidi1Bytes(0x90, 60, 100));
      render(*vm, 2048);
      vm->handleMIDIMessage(pm::fromMidi1Bytes(0xD0, 127, 0));     // full channel pressure, after the strike
      render(*vm, 1024);
      const double held = measureT60(*vm, 0.5);
      check(near(held, 2.0), "Press>Decay +2 oct: full pressure stretches T60 0.5 s to 2 s (" + num(held) + " s)");
      vm->handleMIDIMessage(pm::fromMidi1Bytes(0xD0, 0, 0));       // ease off
      render(*vm, 1024);
      const double eased = measureT60(*vm, 0.3);
      check(near(eased, 0.5), "easing off damps it back to 0.5 s (" + num(eased) + " s)");
   }
   {  // Negative: pressing damps the note, like a hand on a bar.
      auto vm = fresh(2.0f);
      setBank(*vm, ModalBankParamItemDecayModLevel, -2.0f);
      vm->handleMIDIMessage(pm::fromMidi1Bytes(0x90, 60, 100));
      vm->handleMIDIMessage(pm::fromMidi1Bytes(0xD0, 127, 0));
      render(*vm, 2048);
      const double damped = measureT60(*vm, 0.3);
      check(near(damped, 0.5), "Press>Decay -2 oct: pressing shortens T60 2 s to 0.5 s (" + num(damped) + " s)");
   }
   {  // At 0 (the default) pressure changes nothing.
      auto vm = fresh(0.5f);
      vm->handleMIDIMessage(pm::fromMidi1Bytes(0x90, 60, 100));
      vm->handleMIDIMessage(pm::fromMidi1Bytes(0xD0, 127, 0));
      render(*vm, 2048);
      const double t = measureT60(*vm, 0.5);
      check(near(t, 0.5), "Press>Decay 0: pressure leaves T60 alone (" + num(t) + " s)");
   }

   // Two harmonic modes, C4 and C5, equal decay at Damp 0: tilt then shortens the upper one.
   auto twoModes = [&](float t60) {
      auto vm = fresh(t60);
      setBank(*vm, ModalBankParamItemModeCount, 2.0f);
      setBank(*vm, ModalBankParamItemDampTilt, 0.0f);
      return vm;
   };
   const double c4 = pm::noteToFrequency(60), c5 = pm::noteToFrequency(72);
   auto upperShare = [&](const std::vector<float>& s) { return toneLevel(s, c5) / toneLevel(s, c4); };
   {  // Tmb>Tilt, acting on a ringing note: CC74 after the strike darkens the tail.
      auto plain = twoModes(4.0f), tilted = twoModes(4.0f);
      setBank(*tilted, ModalBankParamItemDampTiltModLevel, 2.0f);
      for (VoiceManager* vm : { plain.get(), tilted.get() }) {
         vm->handleMIDIMessage(pm::fromMidi1Bytes(0x90, 60, 100));
         render(*vm, 2048);
         vm->handleMIDIMessage(pm::fromMidi1Bytes(0xB0, 74, 127));   // after the strike
         render(*vm, uint32_t(kSampleRate));
      }
      const double p = upperShare(render(*plain, 9600)), t = upperShare(render(*tilted, 9600));
      check(t < 0.5 * p, "Tmb>Tilt +2: CC74 after the strike makes the upper mode die faster (upper/fundamental "
                             + num(t) + " vs " + num(p) + " untilted)");
   }
   {  // Tmb>Pos, read at the strike: strike position 0.25 + 0.25 = 0.5 cancels the even (upper) mode.
      auto vm = twoModes(4.0f);
      setBank(*vm, ModalBankParamItemStrikeModLevel, 0.25f);
      vm->handleMIDIMessage(pm::fromMidi1Bytes(0xB0, 74, 127));      // CC74 before the note, as MPE sends it
      vm->handleMIDIMessage(pm::fromMidi1Bytes(0x90, 60, 100));
      const double atHalf = upperShare(render(*vm, 9600));
      auto plain = twoModes(4.0f);
      setBank(*plain, ModalBankParamItemStrikeModLevel, 0.25f);
      plain->handleMIDIMessage(pm::fromMidi1Bytes(0x90, 60, 100));
      const double atQuarter = upperShare(render(*plain, 9600));
      check(atHalf < 0.01 && atQuarter > 0.3, "Tmb>Pos +0.25: CC74 at the strike moves it to 0.5 and cancels the even mode ("
                                                  + num(atHalf) + " vs " + num(atQuarter) + ")");
      plain->handleMIDIMessage(pm::fromMidi1Bytes(0xB0, 74, 127));   // after the strike: too late to matter
      render(*plain, 2048);
      const double later = upperShare(render(*plain, 9600));
      check(std::fabs(later / atQuarter - 1.0) < 0.05, "CC74 after the strike leaves the ringing note's modes alone ("
                                                           + num(later) + ")");
   }
}

// --- channel mode messages: CC 123 All Notes Off, CC 120 All Sound Off ------------------------------
void testChannelModeMessages() {
   std::printf("All Notes Off / All Sound Off:\n");
   {  // CC 123 releases only its channel's notes; they ring out like any released note.
      VoiceManager vm(kSampleRate, kBlock);
      configure(vm);
      setBank(vm, ModalBankParamItemDecayT60, 0.05f);
      vm.handleMIDIMessage(pm::fromMidi1Bytes(0x91, 60, 100));   // channel 2
      vm.handleMIDIMessage(pm::fromMidi1Bytes(0x92, 64, 100));   // channel 3
      vm.handleMIDIMessage(midi1CC(1, 123, 0));
      check(vm.holdsNote(noteOn(60, 1)), "right after CC 123 the released note is still ringing");
      render(vm, uint32_t(kSampleRate));
      check(!vm.holdsNote(noteOn(60, 1)) && vm.holdsNote(noteOn(64, 2)),
            "CC 123 on channel 2 releases channel 2's note and leaves channel 3's held");
   }
   {  // ...including a note the sustain pedal is holding.
      VoiceManager vm(kSampleRate, kBlock);
      configure(vm);
      setBank(vm, ModalBankParamItemDecayT60, 0.05f);
      vm.handleMIDIMessage(midi1CC(0, 64, 127));
      vm.handleMIDIMessage(pm::fromMidi1Bytes(0x90, 60, 100));
      vm.handleMIDIMessage(pm::fromMidi1Bytes(0x80, 60, 0));
      render(vm, uint32_t(kSampleRate / 2));
      check(vm.activeCount() == 1, "the pedal is holding the note");
      vm.handleMIDIMessage(makeUmp(pm::kStatusControlChange, 0, 123, 0, 0));   // MIDI 2.0 CC 123
      render(vm, uint32_t(kSampleRate));
      check(vm.activeCount() == 0, "MIDI 2.0 CC 123 releases a pedal-held note");
   }
   {  // CC 120 cuts its channel's voices at once, ringing tails included; other channels play on.
      VoiceManager vm(kSampleRate, kBlock);
      configure(vm);
      vm.handleMIDIMessage(noteOn(60, 1));
      vm.handleMIDIMessage(noteOn(67, 2));
      vm.handleMIDIMessage(noteOff(60, 1));                     // channel 2's note is now just a tail
      render(vm, 4096);
      vm.handleMIDIMessage(midi1CC(1, 120, 0));
      check(!vm.holdsNote(noteOn(60, 1)) && vm.holdsNote(noteOn(67, 2)) && vm.activeCount() == 1,
            "CC 120 on channel 2 drops its ringing note immediately, channel 3's stays");
      const std::vector<float> after = render(vm, uint32_t(kSampleRate));
      check(toneLevel(after, pm::noteToFrequency(60)) < 1e-4 && toneLevel(after, pm::noteToFrequency(67)) > 0.01,
            "C4 is silent at once, G4 still sounds (C4 " + num(toneLevel(after, pm::noteToFrequency(60))) + ")");
      vm.handleMIDIMessage(midi1CC(2, 120, 0));
      check(peakOf(render(vm, 4096)) == 0.0f, "CC 120 on the other channel leaves total silence");
   }
   {  // CC 120 must not make a voice forget other channels' bends (a full Voice::reset would).
      VoiceManager vm(kSampleRate, kBlock);
      configure(vm);
      setVM(vm, MIDIParamItemVoiceCount, 1.0f);                  // one voice: the next note reuses it
      vm.handleMIDIMessage(midi1Bend(4, 8192 + 4096));            // channel 5: +1 semitone at ±2
      vm.handleMIDIMessage(noteOn(60, 1));
      render(vm, 2048);
      vm.handleMIDIMessage(midi1CC(1, 120, 0));
      const double bent = semitonesFromC4(vm, 4);
      check(std::fabs(bent - 1.0) < 0.01, "after CC 120 the voice still knows channel 5's bend (" + num(bent) + " st)");
   }
}

// Note-offs the engine generates itself (sustain pedal, all-notes-off, panic) must carry the
// note's channel, or they never match the note they are meant to release.
void testGeneratedNoteOffsMatchChannel() {
   std::printf("Engine note-offs on other channels:\n");
   VoiceManager pedal(kSampleRate, kBlock);
   configure(pedal);
   setBank(pedal, ModalBankParamItemDecayT60, 0.05f);
   pedal.handleMIDIMessage(pm::fromMidi1Bytes(0xB5, 64, 127));   // pedal down, channel 6
   pedal.handleMIDIMessage(pm::fromMidi1Bytes(0x95, 60, 100));
   render(pedal, 2048);
   pedal.handleMIDIMessage(pm::fromMidi1Bytes(0x85, 60, 0));     // deferred by the pedal
   render(pedal, uint32_t(kSampleRate / 2));
   check(pedal.activeCount() == 1, "the pedal holds a channel-6 note after its note-off");
   pedal.handleMIDIMessage(pm::fromMidi1Bytes(0xB5, 64, 0));
   render(pedal, uint32_t(kSampleRate));
   check(pedal.activeCount() == 0, "pedal up releases the channel-6 note (active=" + num(pedal.activeCount()) + ")");

   VoiceManager all(kSampleRate, kBlock);
   configure(all);
   setBank(all, ModalBankParamItemDecayT60, 0.05f);
   all.handleMIDIMessage(pm::fromMidi1Bytes(0x9A, 64, 100));
   render(all, 2048);
   all.allNotesOff();
   render(all, uint32_t(kSampleRate));
   check(all.activeCount() == 0, "all-notes-off releases a channel-11 note (active=" + num(all.activeCount()) + ")");

   VoiceManager panic(kSampleRate, kBlock);
   configure(panic);
   setBank(panic, ModalBankParamItemDecayT60, 0.05f);
   panic.handleMIDIMessage(makeUmp(pm::kStatusNoteOn, 7, 50, 0, 0xC000u << 16));
   panic.handleMIDIMessage(makeUmp(pm::kStatusNoteOn, 9, 72, 0, 0xC000u << 16));
   render(panic, 2048);
   panic.panic();
   render(panic, uint32_t(kSampleRate));
   check(panic.activeCount() == 0, "panic releases notes on channels 8 and 10 (active=" + num(panic.activeCount()) + ")");
}

} // namespace

int main() {
   std::printf("StruckRoom pitch-bend test (%.0f Hz)\n", kSampleRate);
   testDecode();
   testUnbentPitch();
   testChannelBend();
   testPerNoteBend();
   testPerNoteBendRange();
   testPitch725();
   testKeyTuning();
   testNoteIds();
   testClapNoteExpressionConversion();
   testHoldsNote();
   testMpe();
   testVelocity();
   testChannelModeMessages();
   testReleasedNotes();
   testExpression();
   testGeneratedNoteOffsMatchChannel();
   if (g_fails == 0) std::printf("ALL PASS\n");
   else std::printf("%d FAILED\n", g_fails);
   return g_fails ? 1 : 0;
}
