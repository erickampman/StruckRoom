//
//  CCConverterModule.cpp
//
//  A chosen MIDI CC number → a held value (Boolean gate or continuous), optionally scaling a
//  patched input as a VCA (out = in × (offset + amount·cc)). Ported from PolygraphModular's
//  CCConverterModule (AUv3); the CC index/value arrive in pm::UmpMessage::ccIndex / ccData.
//
//  PlainOldSynth adds a Per Note mode (CCParamItemPerNote), which is how CC74 becomes the per-voice
//  "Timbre" source. StruckRoom takes PlainOldSynth's version and feeds CC74 to ModalBank's damp tilt
//  and strike position (the Tmb>Tilt / Tmb>Pos knobs; see VoiceManager::buildDefaultPatch):
//
//  - Off (the default, and what CC #1 uses): the last value of that CC on ANY channel, as before.
//  - On: the CC is tracked per channel (× group) and each voice follows the channel its note was
//    played on, so a note-per-channel (MPE) controller gives each note its own value. The MIDI 2.0
//    registered per-note controller with the SAME index (#74 for CC74 — MIDI 2.0 numbers them to
//    match) overrides it for the note it names, as does CLAP's Brightness note expression, which
//    arrives as that controller. The per-note value resets on note-on and on per-note management
//    Reset.
//
//  With Per Note AND the MPE param (lower zone), a note on a member channel takes the HIGHER of its
//  own value and the master channel's — the same rule as the Pressure source.
//
//  Continuous values are de-zippered (ControlRamp); Boolean (gate) values switch instantly.
//

#include "CCConverterModule.hpp"
#include "ControlRamp.hpp"
#include "pm_midi.h"
#include <algorithm>

namespace {

constexpr int kGroups   = 16;
constexpr int kChannels = 16;

struct CCConverterHelperStruct {
    double curValue = 0.0;                          // Per Note off: last value on any channel

    float   channelValue[kGroups][kChannels] = {};  // Per Note on: last value per group/channel
    float   noteValue = 0.0f;                       // Per Note on: per-note controller, this note
    bool    hasNoteValue = false;
    uint8_t group = 0, channel = 0;                 // where this voice's note was played
    bool    released = false;                       // seen Module::noteReleased (syncRelease)
    float   releasedValue = 0.0f;                   // Per Note on: the note's own channel value at release

    ControlRamp ramp;
};

// MPE §3.3.3: once the note is released its member channel no longer controls it. Remember the
// channel value it had, before a later message (this runs ahead of each one) can change it.
void syncRelease(CCConverterHelperStruct& st, Module* module) {
    if (module->noteReleased == st.released) return;
    st.released = module->noteReleased;
    if (st.released) st.releasedValue = st.channelValue[st.group][st.channel];
}

} // namespace

const void InitCCConverter(uint32_t /*count*/, Module* module)
{
    module->state = new CCConverterHelperStruct();
}

const void DeinitCCConverter(Module* module)
{
    delete static_cast<CCConverterHelperStruct*>(module->state);
    module->state = nullptr;
}

const void ResetCCConverter(Module* module)
{
    *static_cast<CCConverterHelperStruct*>(module->state) = CCConverterHelperStruct();
}

const KPBuffPair* ProcessCCConverter(uint64_t processCallID, uint32_t count, Module* module)
{
    auto* state = static_cast<CCConverterHelperStruct*>(module->state);
    float* outL = module->output.first->data;
    float* outR = module->output.second->data;

    const float amount = module->paramByEnum(CCParamItemAmount);
    const float offset = module->paramByEnum(CCParamItemOffset);
    const bool  perNote = module->paramByEnum(CCParamItemPerNote) >= 0.5f;
    const bool  isBool  = module->paramByEnum(CCParamItemIsBoolean) >= 0.5f;

    syncRelease(*state, module);
    const bool member = perNote && pm::isMpeMemberChannel(module->paramByEnum(CCParamItemMPE) >= 0.5f, state->channel);
    // Under MPE a released note keeps the member-channel value it had (MPE §3.3.3); the master's still
    // applies. With MPE off, or Per Note off, the controller keeps reaching releasing notes.
    const float channelValue = (member && state->released) ? state->releasedValue
                                                           : state->channelValue[state->group][state->channel];
    float value = !perNote            ? float(state->curValue)
                : state->hasNoteValue ? state->noteValue
                                      : channelValue;
    if (member)
        value = std::max(value, state->channelValue[state->group][pm::kMpeMasterChannel]);
    if (isBool) state->ramp.snapNext();   // a gate switches, it does not slide
    state->ramp.fill(value, module->mSampleRate, outL, count);

    // out = offset + amount·cc (amount=1, offset=0 -> just the CC). With a signal patched, the CC
    // scales it (VCA): out = in × that. Stereo: each channel scaled independently.
    if (module->isInputConnected(CCParamItemInput)) {
        const KPBuffPair* in = module->retrieveInput(CCParamItemInput, processCallID, count);
        const float* inL = in->first->data;
        const float* inR = in->second->data;
        for (uint32_t i = 0; i < count; ++i) {
            const float shaped = offset + amount * outL[i];
            outL[i] = inL[i] * shaped;
            outR[i] = inR[i] * shaped;
        }
    } else {
        for (uint32_t i = 0; i < count; ++i) {
            outL[i] = offset + amount * outL[i];
            outR[i] = outL[i];
        }
    }

    return &module->output;
}

void MIDICCConverter(const pm::UmpMessage& message, Module* module)
{
    if (!pm::isChannelVoice2(message)) return;
    auto* state = static_cast<CCConverterHelperStruct*>(module->state);
    syncRelease(*state, module);

    const uint8_t index   = static_cast<uint8_t>(module->paramByEnum(CCParamItemNumber));
    const bool    isBool  = module->paramByEnum(CCParamItemIsBoolean) >= 0.5f;
    const bool    perNote = module->paramByEnum(CCParamItemPerNote) >= 0.5f;

    auto valueOf = [isBool](uint32_t data) {
        constexpr double kMidi2CCNormalization = 1.0 / 4294967295.0;
        return isBool ? (data != 0 ? 1.0 : 0.0) : data * kMidi2CCNormalization;
    };

    switch (message.status) {
    case pm::kStatusControlChange:
        if (message.ccIndex != index) break;
        if (perNote) state->channelValue[message.group & 0x0F][message.channel & 0x0F] = float(valueOf(message.ccData));
        else         state->curValue = valueOf(message.ccData);
        break;
    case pm::kStatusRegPerNoteController:
        if (!perNote || message.ccIndex != index) break;
        state->noteValue = float(valueOf(message.ccData));
        state->hasNoteValue = true;
        break;
    case pm::kStatusNoteOn:
        if (!perNote || !pm::isNoteOn(message)) break;
        state->group = message.group & 0x0F;
        state->channel = message.channel & 0x0F;
        state->hasNoteValue = false;
        state->released = false;
        state->ramp.snapNext();   // every note-on re-strikes in StruckRoom, so start at the new value
        break;
    case pm::kStatusPerNoteManagement:
        if (perNote && (message.optionFlags & pm::kPerNoteMgmtReset)) state->hasNoteValue = false;
        break;
    default:
        break;
    }
}
