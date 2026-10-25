//
//  RoomModule.cpp
//

#include "RoomModule.hpp"

#include "PitchConverter.hpp"
#include "Room.hpp"

#include <cmath>

struct RoomState {
    Room room;
    bool pendingStrike = true;   // the first block tunes the room even without a note-on
    float mono[512];             // the bank's stereo folded back to mono, per chunk
};

const void InitRoomModule(uint32_t /*count*/, Module* module)
{
    auto* st = new RoomState();
    st->room.setSampleRate(module->mSampleRate);
    module->state = st;
}

const void DeinitRoomModule(Module* module)
{
    delete static_cast<RoomState*>(module->state);
    module->state = nullptr;
}

const void ResetRoomModule(Module* module)
{
    // Voice allocation only. A strike on a ringing voice retunes but doesn't
    // flush: the room's tail carries on into the next note, as a real one would.
    if (auto* st = static_cast<RoomState*>(module->state)) st->room.reset();
}

const void SampleRateRoomModule(Module* module, double newSampleRate)
{
    if (auto* st = static_cast<RoomState*>(module->state)) st->room.setSampleRate(newSampleRate);
}

// Note-on retunes the room in the next process block, which reads the pitch then.
void MIDIRoomModule(const pm::UmpMessage& message, Module* module)
{
    auto* st = static_cast<RoomState*>(module->state);
    if (st && pm::isNoteOn(message)) st->pendingStrike = true;
}

static Room::Wall readWall(Module* module, uint8_t first, uint8_t fx)
{
    Room::Wall w;
    w.absorb    = module->getParamValue(first + 0);
    w.bright    = module->getParamValue(first + 1);
    w.ring      = module->getParamValue(first + 2);
    w.ringMat   = (int)std::lround(module->getParamValue(first + 3));
    w.ringRatio = module->getParamValue(first + 4);
    w.ringT60   = module->getParamValue(first + 5);
    w.ringTilt  = module->getParamValue(first + 6);
    w.link      = module->getParamValue(first + 7) >= 0.5f;
    w.shift     = module->getParamValue(fx + 0) * 0.01f;   // the parameter is in % of f0
    w.flex      = module->getParamValue(fx + 1);
    w.rattle    = module->getParamValue(fx + 2);
    w.gap       = module->getParamValue(fx + 3);
    return w;
}

const KPBuffPair* ProcessRoomModule(uint64_t processCallID, uint32_t count, Module* module)
{
    auto* st = static_cast<RoomState*>(module->state);
    if (!st) return KPBuffPairHelper::zeroes();

    Room::Settings s;
    s.width      = module->getParamValue(RoomParamItemWidth);
    s.depth      = module->getParamValue(RoomParamItemDepth);
    s.roomTrack  = module->getParamValue(RoomParamItemRoomTrack);
    s.decayTrack = module->getParamValue(RoomParamItemDecayTrack);
    s.airT60     = module->getParamValue(RoomParamItemAir);
    s.detail     = module->getParamValue(RoomParamItemDetail);
    s.srcX       = module->getParamValue(RoomParamItemSrcX);
    s.srcY       = module->getParamValue(RoomParamItemSrcY);
    s.micX       = module->getParamValue(RoomParamItemMicX);
    s.micY       = module->getParamValue(RoomParamItemMicY);
    s.micWidth   = module->getParamValue(RoomParamItemMicWidth);
    s.direct     = module->getParamValue(RoomParamItemDirect);
    s.early      = module->getParamValue(RoomParamItemEarly);
    s.late       = module->getParamValue(RoomParamItemLate);
    s.wall[0]    = readWall(module, RoomParamItemSideAbsorb, RoomParamItemSideShift);
    s.wall[1]    = readWall(module, RoomParamItemFBAbsorb, RoomParamItemFBShift);
    st->room.setSettings(s);

    if (st->pendingStrike) {
        float octaves = module->getParamValue(RoomParamItemPitch);
        if (module->isInputConnected(RoomParamItemPitchInput))
            octaves += module->retrieveInput(RoomParamItemPitchInput, processCallID, count)->first->data[0];
        st->room.strike((float)module->pitchConverter->pitchToFrequency(octaves));
        st->pendingStrike = false;
    }

    float* outL = module->output.first->data;
    float* outR = module->output.second->data;

    if (!module->isInputConnected(RoomParamItemInput)) {
        st->room.processBlock(nullptr, outL, outR, (int)count);
        return &module->output;
    }

    // The bank runs with Spread 0 (VoiceManager sets it), so L = R = y * cos(pi/4);
    // their sum over sqrt(2) is the object's mono sound.
    const KPBuffPair* in = module->retrieveInput(RoomParamItemInput, processCallID, count);
    for (uint32_t done = 0; done < count; done += 512) {
        const uint32_t n = std::min<uint32_t>(512, count - done);
        for (uint32_t i = 0; i < n; ++i)
            st->mono[i] = (in->first->data[done + i] + in->second->data[done + i]) * (float)M_SQRT1_2;
        st->room.processBlock(st->mono, outL + done, outR + done, (int)n);
    }
    return &module->output;
}
