//
//  PitchBendConverterModule.cpp
//
//  MIDI 2.0 pitch bend → a bipolar octaves signal (patched into ModalBank's second pitch input).
//  Vendored from PolygraphModularUnified's PitchBendConverterModule; the extensions below were made
//  in PlainOldSynth and ported here:
//
//  - Per-note pitch bend (UMP status 0x6). One instance lives in every voice, and the voice only
//    passes it per-note messages for the note it is sounding (VoiceStateManager::handleMIDIMessage),
//    so this module simply applies whatever per-note bend reaches it. Range: the bend range of the
//    note's channel (so ±48 on an MPE member channel), unless registered controller 7 set one (below). M2-104 v1.0 §4.2.12 says
//    per-note bend "acts like Pitch Bend in every way" and defines no range of its own; PlainOldSynth's
//    fixed ±48 was MPE's member-channel convention, which belongs to channel bend, not to this.
//  - Channel bend tracked PER CHANNEL. Channel bends reach every voice, including idle ones; each
//    voice remembers all sixteen channels (× groups) and applies the one its note was played on. With
//    everything on one channel that is the ordinary behaviour; with a note per channel (MPE, or a
//    MIDI 2.0 controller) each note follows its own channel's bend. Range: QuarterTones / 2 semitones
//    (default ±2).
//  - Per-note pitch bend sensitivity (MIDI 2.0 registered controller bank 0 / index 7) overrides the
//    per-note range for the channel it arrives on — a controller such as KeyboardEDO announces its
//    range this way and scales its bends to match. (RC 7 is not in M2-104 v1.0; it is from a later
//    revision.) Unlike the rest of this state it survives reset(): it is configuration the
//    controller sent once, not processing state, and some hosts call reset() on transport stop.
//  - MPE (the MPE param; lower zone, see pm::kMpeMasterChannel). A note on a member channel uses the
//    member bend range (±48 by default) and ADDS the master channel's bend (±2 by default): the
//    controller's own pitch wheel moves every note in the zone on top of each note's own slide.
//  - Pitch bend sensitivity (RPN 0), as MIDI 1.0 CC 101/100 + 6/38 or a MIDI 2.0 registered
//    controller, overrides a channel's range. With MPE on, RPN 0 on any member channel sets every
//    member channel, as MPE specifies. The overrides go back to the defaults when an MPE
//    Configuration Message arrives (RPN 6 on channel 1 or 16; only its reset is honoured, the zone
//    layout is the switch's) and when the MPE switch is flipped, so a range set for one mode does not
//    leak into the other. Like RC 7 they survive reset(). (MPE and RPN 0 ported from PlainOldSynth.)
//  - Release (MPE §3.3.3: "control of a note ceases once Note Off has occurred"). When the voice's
//    note is released (Module::noteReleased: note-off, or held only by the pedal), an MPE member
//    note's channel bend freezes at its value, so a sender that centres the channel for its next
//    note does not swoop the ringing tail. The master channel's bend still applies, as MPE says.
//    With MPE off nothing freezes: an ordinary pitch wheel bends releasing notes. Per-note messages
//    stop reaching a released note upstream (VoiceStateManager::handleMIDIMessage).
//  - Pitch 7.25 (registered per-note controller #3): an absolute pitch for the sounding note, which
//    both bends then offset, as M2-104 §4.2.14.2 specifies. Applied here as an offset from the note's
//    own pitch (its note number, or its Pitch 7.9 attribute), so it too bypasses the glide.
//    KeyboardEDO's glide mode sends it, and CLAP tuning note expressions arrive as it (clap_ump.h).
//    Here it lasts for the sounding note only and resets on the next note-on and on per-note
//    management Reset. The spec's persistent side — a tuning table that later notes on that key
//    inherit — lives upstream in VoiceManager::keyTuning, which hands a key's stored Pitch 7.25 to
//    the voice on its note-on (UmpMessage::keyPitch). CLAP tuning stays out of that table: it must
//    not outlive its note.
//  - De-zippering. Bend arrives as discrete events; a step in pitch is audible, so a change of target
//    ramps linearly over kRampSeconds. Every note-on snaps instead: the note must start at its bent
//    pitch, and in StruckRoom even a mono legato note re-strikes the bank, so there is no sounding
//    pitch worth gliding from. (PlainOldSynth ramps on legato via Module::legatoContinuation, which
//    StruckRoom does not have.)
//
//  Per-note bend resets to centre on every note-on (a new note, a steal or a mono retrigger) and on a
//  per-note management message with the Reset flag. Per-note bend sent BEFORE its note-on is
//  dropped: no voice owns that note yet.
//

#include "PitchBendConverterModule.hpp"
#include "pm_midi.h"
#include <algorithm>
#include <cmath>
#include <iterator>

namespace {

constexpr double kRampSeconds = 0.002;
constexpr int    kGroups      = 16;
constexpr int    kChannels    = 16;

// Registered controllers (RPN), bank 0.
constexpr uint8_t kRpnBankPitch = 0;
constexpr uint8_t kRpnPitchBendSensitivity = 0;
constexpr uint8_t kRpnMpeConfiguration = 6;
constexpr uint8_t kRpnPerNotePitchBendSensitivity = 7;

// MIDI 1.0 RPN selection state for one channel. 127/127 is the RPN "null" (nothing selected).
struct RpnState {
    uint8_t msb = 127, lsb = 127;
    uint8_t semitones = 0;   // the last data-entry MSB for RPN 0, so a later LSB (cents) can add to it
};

struct PitchBendConverterHelperStruct {
    PitchBendConverterHelperStruct() {
        std::fill_n(&rangeOverride[0][0], kGroups * kChannels, -1.0f);
        std::fill_n(&perNoteRange[0][0], kGroups * kChannels, -1.0f);
    }
    float   channelBend[kGroups][kChannels] = {};   // -1..+1, last channel bend per group/channel
    // Configuration a controller sent; kept across reset() (see ResetPitchBendConverter).
    float   rangeOverride[kGroups][kChannels];      // semitones from RPN 0; < 0 = not set (default)
    float   perNoteRange[kGroups][kChannels];       // semitones from RC 7; < 0 = not set (channel range)
    bool    lastMpe = false;                        // the MPE switch as last seen (syncMpeSwitch)

    RpnState rpn[kGroups][kChannels];
    float   perNoteBend = 0.0f;                     // -1..+1, for the note this voice is playing
    float   notePitch = 0.0f;                       // semitones: the note-on's note number or Pitch 7.9
    float   perNoteTuning = 0.0f;                   // semitones from Pitch 7.25, relative to notePitch
    uint8_t group = 0, channel = 0;                 // where this voice's note was played
    bool    released = false;                       // seen Module::noteReleased (syncRelease)
    float   releasedBend = 0.0f;                    // the note's own channel bend at release

    float    current = 0.0f;     // octaves, as output
    float    rampTarget = 0.0f;  // octaves the ramp is heading to
    float    rampStep = 0.0f;
    uint32_t rampRemaining = 0;
    bool     snap = true;        // jump straight to the target on the next process()
};

bool mpeOn(Module* module) { return module->paramByEnum(PitchBendConverterParamItemMPE) >= 0.5f; }

// A channel's bend range in semitones: RPN 0 if a controller set one, else the member default under
// MPE, else the ordinary channel range (the QuarterTones param is in quarter tones, so / 2).
float channelRange(const PitchBendConverterHelperStruct& st, Module* module, uint8_t group, uint8_t channel) {
    if (st.rangeOverride[group][channel] >= 0.0f) return st.rangeOverride[group][channel];
    if (pm::isMpeMemberChannel(mpeOn(module), channel)) return pm::kMpeMemberBendRangeSemitones;
    return module->paramByEnum(PitchBendConverterParamItemQuarterTones) * 0.5f;
}

float targetOctaves(const PitchBendConverterHelperStruct& st, Module* module) {
    const uint8_t g = st.group, c = st.channel;
    const bool member = pm::isMpeMemberChannel(mpeOn(module), c);
    const float range = channelRange(st, module, g, c);
    const float rc7 = st.perNoteRange[g][c];
    const float ownBend = (member && st.released) ? st.releasedBend : st.channelBend[g][c];
    float semis = ownBend * range
                + st.perNoteBend * (rc7 >= 0.0f ? rc7 : range)   // no RC 7: like channel bend
                + st.perNoteTuning;
    if (member) {
        const uint8_t m = pm::kMpeMasterChannel;
        semis += st.channelBend[g][m] * channelRange(st, module, g, m);   // the zone's pitch wheel
    }
    return semis / 12.0f;
}

void clearBendRanges(PitchBendConverterHelperStruct& st, uint8_t group) {
    std::fill_n(&st.rangeOverride[group][0], kChannels, -1.0f);
}

// Catch the voice's release: remember the note's own channel bend as it was, before any later bend
// message (this runs ahead of each one) can move it.
void syncRelease(PitchBendConverterHelperStruct& st, Module* module) {
    if (module->noteReleased == st.released) return;
    st.released = module->noteReleased;
    if (st.released) st.releasedBend = st.channelBend[st.group][st.channel];
}

// Flipping the MPE switch reconfigures the zone, so every RPN 0 override goes back to the defaults.
void syncMpeSwitch(PitchBendConverterHelperStruct& st, Module* module) {
    const bool on = mpeOn(module);
    if (on == st.lastMpe) return;
    st.lastMpe = on;
    std::fill_n(&st.rangeOverride[0][0], kGroups * kChannels, -1.0f);
}

// The MPE Configuration Message (RPN 6) is sent on a zone's master channel: 1 (lower) or 16 (upper).
bool isMpeConfigChannel(uint8_t channel) { return channel == 0 || channel == 15; }

// RPN 0 arrived for (group, channel): set its range, or every member channel's under MPE.
void setBendRange(PitchBendConverterHelperStruct& st, Module* module, uint8_t group, uint8_t channel,
                  float semitones) {
    if (pm::isMpeMemberChannel(mpeOn(module), channel)) {
        for (uint8_t c = 0; c < kChannels; ++c)
            if (c != pm::kMpeMasterChannel) st.rangeOverride[group][c] = semitones;
    } else {
        st.rangeOverride[group][channel] = semitones;
    }
}

// MIDI 1.0 RPN: CC 101/100 select, CC 6 = data entry MSB (semitones for RPN 0), CC 38 = LSB (cents).
void handleRpnCC(PitchBendConverterHelperStruct& st, Module* module, uint8_t group, uint8_t channel,
                 uint8_t cc, uint8_t value) {
    RpnState& rpn = st.rpn[group][channel];
    switch (cc) {
    case 101: rpn.msb = value; break;
    case 100: rpn.lsb = value; break;
    case 6:
        if (rpn.msb == kRpnBankPitch && rpn.lsb == kRpnMpeConfiguration && isMpeConfigChannel(channel)) {
            clearBendRanges(st, group);
            break;
        }
        if (rpn.msb != kRpnBankPitch || rpn.lsb != kRpnPitchBendSensitivity) break;
        rpn.semitones = value;
        setBendRange(st, module, group, channel, float(value));
        break;
    case 38:
        if (rpn.msb != kRpnBankPitch || rpn.lsb != kRpnPitchBendSensitivity) break;
        setBendRange(st, module, group, channel, float(rpn.semitones) + float(value) / 100.0f);
        break;
    default:
        break;
    }
}

// MIDI 2.0 registered-controller value with the MIDI 1.0 data-entry layout in its top 14 bits:
// semitones in bits 31-25, cents in bits 24-18.
float semitonesFromRegisteredValue(uint32_t value) {
    const uint32_t data14 = value >> 18;
    return float(data14 >> 7) + float(data14 & 0x7F) / 100.0f;
}

} // namespace

const void InitPitchBendConverter(uint32_t /*count*/, Module* module)
{
    module->state = new PitchBendConverterHelperStruct();
}

const void DeinitPitchBendConverter(Module* module)
{
    delete static_cast<PitchBendConverterHelperStruct*>(module->state);
    module->state = nullptr;
}

const void ResetPitchBendConverter(Module* module)
{
    auto* st = static_cast<PitchBendConverterHelperStruct*>(module->state);
    PitchBendConverterHelperStruct fresh;
    // Configuration a controller sent, not processing state: keep it (some hosts reset on stop).
    std::copy_n(&st->rangeOverride[0][0], kGroups * kChannels, &fresh.rangeOverride[0][0]);
    std::copy_n(&st->perNoteRange[0][0], kGroups * kChannels, &fresh.perNoteRange[0][0]);
    fresh.lastMpe = st->lastMpe;
    *st = fresh;
}

const KPBuffPair* ProcessPitchBendConverter(uint64_t /*processCallID*/, uint32_t count, Module* module)
{
    auto* st = static_cast<PitchBendConverterHelperStruct*>(module->state);
    float* outL = module->output.first->data;
    float* outR = module->output.second->data;

    syncMpeSwitch(*st, module);
    syncRelease(*st, module);
    // Recomputed every block, so a range change applies (ramped) without waiting for the next bend.
    const float target = targetOctaves(*st, module);
    if (st->snap) {
        st->snap = false;
        st->current = st->rampTarget = target;
        st->rampRemaining = 0;
    } else if (target != st->rampTarget) {
        st->rampTarget = target;
        st->rampRemaining = std::max<uint32_t>(1, uint32_t(std::lround(module->mSampleRate * kRampSeconds)));
        st->rampStep = (target - st->current) / float(st->rampRemaining);
    }

    if (st->rampRemaining == 0) {
        std::fill_n(outL, count, st->current);
    } else {
        for (uint32_t i = 0; i < count; ++i) {
            if (st->rampRemaining > 0) {
                st->current = (--st->rampRemaining == 0) ? st->rampTarget : st->current + st->rampStep;
            }
            outL[i] = st->current;
        }
    }
    std::copy_n(outL, count, outR);

    return &module->output;
}

void MIDIPitchBendConverter(const pm::UmpMessage& message, Module* module)
{
    if (!pm::isChannelVoice2(message)) return;
    auto* st = static_cast<PitchBendConverterHelperStruct*>(module->state);
    const uint8_t group   = message.group & 0x0F;
    const uint8_t channel = message.channel & 0x0F;
    syncMpeSwitch(*st, module);   // before any RPN below lands under the old mode
    syncRelease(*st, module);     // before a bend below can move a note that was just released

    switch (message.status) {
    case pm::kStatusNoteOn:
        st->group = group;
        st->channel = channel;
        st->perNoteBend = 0.0f;
        st->released = false;
        st->notePitch = message.attributeType == pm::kAttrPitch7_9 ? float(message.attribute) / 512.0f
                                                                   : float(message.note);
        st->perNoteTuning = message.hasKeyPitch ? float(double(message.keyPitch) / 33554432.0 - st->notePitch)
                                                : 0.0f;
        st->snap = true;
        break;
    case pm::kStatusPitchBend:
        st->channelBend[group][channel] = float(pm::pitchBendToUnit(message.data32));
        break;
    case pm::kStatusControlChange:
        handleRpnCC(*st, module, group, channel, message.ccIndex, uint8_t(message.ccData >> 25));
        break;
    case pm::kStatusRegController:
        if (message.note != kRpnBankPitch) break;
        if (message.ccIndex == kRpnPitchBendSensitivity)
            setBendRange(*st, module, group, channel, semitonesFromRegisteredValue(message.ccData));
        else if (message.ccIndex == kRpnPerNotePitchBendSensitivity)
            st->perNoteRange[group][channel] = semitonesFromRegisteredValue(message.ccData);
        else if (message.ccIndex == kRpnMpeConfiguration && isMpeConfigChannel(channel))
            clearBendRanges(*st, group);   // MPE Configuration Message
        break;
    case pm::kStatusRegPerNoteController:
        if (message.ccIndex == pm::kRegPerNotePitch7_25)
            st->perNoteTuning = float(double(message.ccData) / 33554432.0 - st->notePitch);
        break;
    case pm::kStatusPerNotePitchBend:
        st->perNoteBend = float(pm::pitchBendToUnit(message.data32));
        break;
    case pm::kStatusPerNoteManagement:
        if (message.optionFlags & pm::kPerNoteMgmtReset) st->perNoteBend = st->perNoteTuning = 0.0f;
        break;
    default:
        break;
    }
}
