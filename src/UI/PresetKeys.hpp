// PresetKeys.hpp — the plugin-param ↔ preset-JSON-field mapping, shared by the dev-only JSON
// dumper (PresetDump.hpp) and the always-on preset loader (GeneratedPresets.h resolution in
// StruckRoomUI.cpp). Kept in its own always-compiled header (not gated by STRUCKROOM_PRESET_DUMP)
// since preset loading must work in every build, not just dev builds.

#ifndef STRUCKROOM_PRESETKEYS_HPP
#define STRUCKROOM_PRESETKEYS_HPP

#include "StruckRoomParams.h"

namespace struckroom {

struct PresetKey { const char* module; const char* item; };

// One entry per param, index-aligned to ParamIndex. Three logical modules: Global voice controls,
// the Strike exciter, and the ModalBank resonator (object / decay / timbre). Items are stable
// preset-file keys — they only need to agree between the dumper and the loader, not with any DSP
// serialization.
static constexpr PresetKey kPresetKeys[kParamCount] = {
   /*VoiceMode*/       {"Global",    "voiceMode"},
   /*Glide*/           {"Global",    "glide"},
   /*Level*/           {"Global",    "level"},
   /*Spread*/          {"Global",    "spread"},
   /*StrikeType*/      {"Strike",    "type"},
   /*Hardness*/        {"Strike",    "hardness"},
   /*VelToLevel*/      {"Strike",    "velToLevel"},
   /*VelToHardness*/   {"Strike",    "velToHardness"},
   /*ContactMode*/     {"Strike",    "contactMode"},
   /*Pitch*/           {"ModalBank", "pitch"},
   /*Material*/        {"ModalBank", "material"},
   /*ModeCount*/       {"ModalBank", "modeCount"},
   /*Stretch*/         {"ModalBank", "stretch"},
   /*DecayT60*/        {"ModalBank", "decayT60"},
   /*DampTilt*/        {"ModalBank", "dampTilt"},
   /*DampBellAmount*/  {"ModalBank", "dampBellAmount"},
   /*DampBellCentre*/  {"ModalBank", "dampBellCentre"},
   /*DampBellWidth*/   {"ModalBank", "dampBellWidth"},
   /*StrikePosition*/  {"ModalBank", "strikePosition"},
   /*Brightness*/      {"ModalBank", "brightness"},
   // MPE describes the controller, not the sound: factory presets leave it out, and the loader keeps
   // the current value for any key a preset does not mention.
   /*Mpe*/             {"Global",    "mpe"},
   /*PressToDecay*/    {"Expression", "pressToDecay"},
   /*TimbreToTilt*/    {"Expression", "timbreToTilt"},
   /*TimbreToPos*/     {"Expression", "timbreToPos"},
   // Room, placement, mix and the two wall pairs
   /*RoomWidth*/        {"Room",           "width"},
   /*RoomDepth*/        {"Room",           "depth"},
   /*RoomTrack*/        {"Room",           "roomTrack"},
   /*DecayTrack*/       {"Room",           "decayTrack"},
   /*RoomAir*/          {"Room",           "air"},
   /*RoomDetail*/       {"Room",           "detail"},
   /*SrcX*/             {"Placement",      "srcX"},
   /*SrcY*/             {"Placement",      "srcY"},
   /*MicX*/             {"Placement",      "micX"},
   /*MicY*/             {"Placement",      "micY"},
   /*Direct*/           {"Mix",            "direct"},
   /*Early*/            {"Mix",            "early"},
   /*Late*/             {"Mix",            "late"},
   /*SideAbsorb*/       {"SideWalls",      "absorb"},
   /*SideBright*/       {"SideWalls",      "bright"},
   /*SideRing*/         {"SideWalls",      "ring"},
   /*SideRingMat*/      {"SideWalls",      "ringMaterial"},
   /*SideRingRatio*/    {"SideWalls",      "ringRatio"},
   /*SideRingDecay*/    {"SideWalls",      "ringDecay"},
   /*SideRingDamp*/     {"SideWalls",      "ringDamp"},
   /*SideLink*/         {"SideWalls",      "link"},
   /*FBAbsorb*/         {"FrontBackWalls", "absorb"},
   /*FBBright*/         {"FrontBackWalls", "bright"},
   /*FBRing*/           {"FrontBackWalls", "ring"},
   /*FBRingMat*/        {"FrontBackWalls", "ringMaterial"},
   /*FBRingRatio*/      {"FrontBackWalls", "ringRatio"},
   /*FBRingDecay*/      {"FrontBackWalls", "ringDecay"},
   /*FBRingDamp*/       {"FrontBackWalls", "ringDamp"},
   /*FBLink*/           {"FrontBackWalls", "link"},
};

} // namespace struckroom

#endif // STRUCKROOM_PRESETKEYS_HPP
