// StruckRoomParams.h — the plugin's parameter table, shared by the DSP and UI. A focused modal
// set: global voice controls, the Strike exciter, and the ModalBank resonator (object / decay /
// timbre / output). Non-global params route to the matching VoiceManager module item (see the
// DSP's vmAddress). Defaults mirror the module constants in pm_param_data.cpp.

#ifndef STRUCKROOM_PARAMS_H
#define STRUCKROOM_PARAMS_H

#include <cstddef>
#include <cstdint>

namespace struckroom {

enum ParamIndex {
   // --- Global ---
   kParamVoiceMode = 0, // 0 = Mono (1 voice + glide), 1..4 = Poly 4 / 10 / 20 / 32
   kParamGlide,         // pitch glide — drives the Slew module's Rise and Fall together (mono)
   kParamLevel,         // master level → ModalBank Level
   kParamSpread,        // the listener's stereo width → Room Mic Width (the M/S side level)
   // --- Strike (exciter) ---
   kParamStrikeType,        // 0 = Mallet, 1 = Noise burst
   kParamHardness,          // 0 = hard/bright 1-sample impulse .. 1 = soft/dark
   kParamVelToLevel,        // velocity → loudness
   kParamVelToHardness,     // velocity → brighter/shorter contact
   kParamContactMode,       // 0 = Note-Relative, 1 = Wall-Clock
   // --- ModalBank: object ---
   kParamPitch,             // transpose offset, octaves
   kParamMaterial,          // indexed material / generator (see ModalBankMaterial)
   kParamModeCount,         // number of modes (clamps to the material's depth)
   kParamStretch,           // inharmonicity index (into kModalStretchValues)
   // --- ModalBank: decay ---
   kParamDecayT60,          // T60 of mode 0, seconds
   kParamDampTilt,          // HF damping (0 metal .. ~2 wood)
   kParamDampBellAmount,    // non-monotonic damping bump, octaves of T60 (0 = off)
   kParamDampBellCentre,    // bell centre, octaves above the fundamental
   kParamDampBellWidth,     // bell width, octaves
   // --- ModalBank: timbre ---
   kParamStrikePosition,    // 0..1; 0.5 cancels even modes
   kParamBrightness,        // amplitude tilt across modes
   // --- Appended (CLAP ids are indexes, so new params go last) ---
   kParamMpe,               // MPE input, lower zone: member bend ±48 + the master channel's bend
   // --- Expression (appended): what pressure and timbre (CC74) do; all 0 = no effect ---
   kParamPressToDecay,      // pressure → decay time, octaves of T60 at full pressure (+ rings longer)
   kParamTimbreToTilt,      // timbre → damping tilt, added to Damp at full timbre (+ darker tail)
   kParamTimbreToPos,       // timbre → strike position, added at full timbre (read at the strike)
   // --- Room (appended): the pitch-scaled 2-D room the object sits in; lengths in wavelengths ---
   kParamRoomWidth,         // wavelengths of the note (at note 60, see Room Track)
   kParamRoomDepth,
   kParamRoomTrack,         // 1 = the room scales with the note, 0 = a fixed physical room
   kParamDecayTrack,        // 1 = every T60 (object, walls, air) scales as 1/f0: the whole scene transposes
   kParamRoomAir,           // air T60, seconds at note 60
   kParamRoomDetail,        // room modes up to this multiple of f0 (the CPU control)
   kParamSrcX,              // where the object sits, 0..1 of the room
   kParamSrcY,
   kParamMicX,              // where the listener sits
   kParamMicY,
   kParamDirect,            // mix: the object's own sound
   kParamEarly,             //      first reflections
   kParamLate,              //      the standing waves (room modes)
   // --- Walls (appended): side walls (x), then front/back (y), the same eight each ---
   kParamSideAbsorb,        // broadband absorption 0..1
   kParamSideBright,        // HF damping corner, octaves above f0 (8 ~ none)
   kParamSideRing,          // wall resonator level (0 = the wall doesn't ring)
   kParamSideRingMat,       // the wall's material (the 12 static tables)
   kParamSideRingRatio,     // wall fundamental / note
   kParamSideRingDecay,     // wall T60, seconds
   kParamSideRingDamp,      // the wall bank's damping tilt
   kParamSideLink,          // 1 = the wall rings with the energy it absorbed
   kParamFBAbsorb,
   kParamFBBright,
   kParamFBRing,
   kParamFBRingMat,
   kParamFBRingRatio,
   kParamFBRingDecay,
   kParamFBRingDamp,
   kParamFBLink,
   kParamCount
};

struct ParamSpec {
   const char* name;
   const char* symbol;
   const char* unit;
   float       min;
   float       max;
   float       def;
   bool        logarithmic;
   bool        integer;
};

static constexpr ParamSpec kParams[kParamCount] = {
   // Global
   {"Voices",     "voices",   "",    0.0f,   4.0f,   1.0f,  false, true },
   {"Glide",      "glide",    "s",   0.0001f, 2.0f,  0.01f, true,  false},
   {"Level",      "level",    "",    0.0f,   2.0f,   0.5f,  false, false},
   {"Width",      "spread",   "",    0.0f,   1.0f,   1.0f,  false, false},
   // Strike
   {"Strike",     "strkType", "",    0.0f,   1.0f,   0.0f,  false, true },
   {"Hardness",   "hardness", "",    0.0f,   1.0f,   0.3f,  false, false},
   {"Vel>Level",  "velLevel", "",    0.0f,   1.0f,   1.0f,  false, false},
   {"Vel>Hard",   "velHard",  "",    0.0f,   1.0f,   0.5f,  false, false},
   {"Contact",    "contact",  "",    0.0f,   1.0f,   0.0f,  false, true },
   // ModalBank: object
   {"Pitch",      "pitch",    "oct", -4.0f,  4.0f,   0.0f,  false, false},
   {"Material",   "material", "",    0.0f,  25.0f,   1.0f,  false, true },
   {"Modes",      "modes",    "",    1.0f,  64.0f,  64.0f,  false, true },
   {"Stretch",    "stretch",  "",    0.0f,  19.0f,   9.0f,  false, true },
   // ModalBank: decay
   {"Decay",      "decay",    "s",   0.02f, 60.0f,   2.0f,  true,  false},
   {"Damp",       "dampTilt", "",    0.0f,   3.0f,   0.9f,  false, false},
   {"Bell Amt",   "bellAmt",  "oct", -4.0f,  4.0f,   0.0f,  false, false},
   {"Bell Ctr",   "bellCtr",  "oct", 0.0f,   7.0f,   2.0f,  false, false},
   {"Bell Wid",   "bellWid",  "oct", 0.1f,   4.0f,   0.4f,  false, false},
   // ModalBank: timbre
   {"Strike Pos", "strkPos",  "",    0.0f,   1.0f,   0.25f, false, false},
   {"Brightness", "bright",   "",    -1.0f,  1.0f,   0.0f,  false, false},
   // Appended
   {"MPE",        "mpe",      "",    0.0f,   1.0f,   0.0f,  false, true },
   // Expression
   {"Press>Decay", "prsDecay", "oct", -4.0f, 4.0f,   0.0f,  false, false},
   {"Tmb>Tilt",   "tmbTilt",  "",    -3.0f,  3.0f,   0.0f,  false, false},
   {"Tmb>Pos",    "tmbPos",   "",    -1.0f,  1.0f,   0.0f,  false, false},
   // Room
   {"Room W",     "roomW",    "wl",  0.25f, 16.0f,   3.0f,  true,  false},
   {"Room D",     "roomD",    "wl",  0.25f, 16.0f,   2.2f,  true,  false},
   {"Room Track", "roomTrk",  "",    0.0f,   1.0f,   1.0f,  false, false},
   {"Decay Track","decayTrk", "",    0.0f,   1.0f,   0.0f,  false, false},
   {"Air",        "air",      "s",   0.5f,  30.0f,  12.0f,  true,  false},
   {"Detail",     "detail",   "x f0", 2.0f, 64.0f,  12.0f,  true,  false},
   {"Src X",      "srcX",     "",    0.0f,   1.0f,   0.31f, false, false},
   {"Src Y",      "srcY",     "",    0.0f,   1.0f,   0.37f, false, false},
   {"Mic X",      "micX",     "",    0.0f,   1.0f,   0.62f, false, false},
   {"Mic Y",      "micY",     "",    0.0f,   1.0f,   0.71f, false, false},
   {"Direct",     "direct",   "",    0.0f,   2.0f,   1.0f,  false, false},
   {"Early",      "early",    "",    0.0f,   2.0f,   1.0f,  false, false},
   {"Late",       "late",     "",    0.0f,   2.0f,   1.0f,  false, false},
   // Walls: side
   {"Side Absorb", "sdAbs",   "",    0.0f,   1.0f,   0.15f, false, false},
   {"Side Bright", "sdBrt",   "oct", 0.0f,   8.0f,   5.0f,  false, false},
   {"Side Ring",  "sdRing",   "",    0.0f,   2.0f,   0.0f,  false, false},
   {"Side Mat",   "sdMat",    "",    0.0f,  11.0f,   3.0f,  false, true },
   {"Side Ratio", "sdRatio",  "",    0.0625f, 4.0f,  0.5f,  true,  false},
   {"Side Decay", "sdDecay",  "s",   0.05f, 20.0f,   1.5f,  true,  false},
   {"Side Damp",  "sdDamp",   "",    0.0f,   3.0f,   1.0f,  false, false},
   {"Side Link",  "sdLink",   "",    0.0f,   1.0f,   0.0f,  false, true },
   // Walls: front/back
   {"F/B Absorb", "fbAbs",    "",    0.0f,   1.0f,   0.15f, false, false},
   {"F/B Bright", "fbBrt",    "oct", 0.0f,   8.0f,   5.0f,  false, false},
   {"F/B Ring",   "fbRing",   "",    0.0f,   2.0f,   0.0f,  false, false},
   {"F/B Mat",    "fbMat",    "",    0.0f,  11.0f,   3.0f,  false, true },
   {"F/B Ratio",  "fbRatio",  "",    0.0625f, 4.0f,  0.5f,  true,  false},
   {"F/B Decay",  "fbDecay",  "s",   0.05f, 20.0f,   1.5f,  true,  false},
   {"F/B Damp",   "fbDamp",   "",    0.0f,   3.0f,   1.0f,  false, false},
   {"F/B Link",   "fbLink",   "",    0.0f,   1.0f,   0.0f,  false, true },
};

// Material picker labels (order matches ModalBankMaterial in pm_params.h). Null-terminated so the
// CLAP params extension and the UI dropdown can walk it without a separate count.
static constexpr const char* kMaterialNames[] = {
   "String", "Bar", "Tuned Bar", "Membrane", "Open Tube", "Closed Tube", "Tine", "Bell",
   "Guitar Body", "Violin Body", "Soundboard", "Generic Body",
   "Harmonic", "Octave", "Fibonacci", "Prime", "Triangular", "Tetrahedral", "Partition",
   "Golden", "Silver", "Piano", "Power", "Sqrt", "Box 3D", "Harmonic #",
   nullptr,
};
static_assert(sizeof(kMaterialNames) / sizeof(kMaterialNames[0]) - 1
                 == std::size_t(kParams[kParamMaterial].max) + 1,
              "kMaterialNames and the Material parameter's range disagree");

// kParams is indexed by ParamIndex, so a swapped pair of rows compiles fine and silently cross-wires
// two controls (PlainOldSynth's Effect Mix and Spread once did exactly that). The table is small, so
// check every row's symbol against the enum rather than spot-checking.
constexpr bool symbolIs(ParamIndex index, const char* symbol)
{
   const char* a = kParams[index].symbol;
   while (*a != '\0' && *a == *symbol) { ++a; ++symbol; }
   return *a == *symbol;
}
static_assert(symbolIs(kParamVoiceMode, "voices") && symbolIs(kParamGlide, "glide")
              && symbolIs(kParamLevel, "level") && symbolIs(kParamSpread, "spread")
              && symbolIs(kParamStrikeType, "strkType") && symbolIs(kParamHardness, "hardness")
              && symbolIs(kParamVelToLevel, "velLevel") && symbolIs(kParamVelToHardness, "velHard")
              && symbolIs(kParamContactMode, "contact") && symbolIs(kParamPitch, "pitch")
              && symbolIs(kParamMaterial, "material") && symbolIs(kParamModeCount, "modes")
              && symbolIs(kParamStretch, "stretch") && symbolIs(kParamDecayT60, "decay")
              && symbolIs(kParamDampTilt, "dampTilt") && symbolIs(kParamDampBellAmount, "bellAmt")
              && symbolIs(kParamDampBellCentre, "bellCtr") && symbolIs(kParamDampBellWidth, "bellWid")
              && symbolIs(kParamStrikePosition, "strkPos") && symbolIs(kParamBrightness, "bright")
              && symbolIs(kParamMpe, "mpe") && symbolIs(kParamPressToDecay, "prsDecay")
              && symbolIs(kParamTimbreToTilt, "tmbTilt") && symbolIs(kParamTimbreToPos, "tmbPos")
              && symbolIs(kParamRoomWidth, "roomW") && symbolIs(kParamRoomDepth, "roomD")
              && symbolIs(kParamRoomTrack, "roomTrk") && symbolIs(kParamDecayTrack, "decayTrk")
              && symbolIs(kParamRoomAir, "air") && symbolIs(kParamRoomDetail, "detail")
              && symbolIs(kParamSrcX, "srcX") && symbolIs(kParamSrcY, "srcY")
              && symbolIs(kParamMicX, "micX") && symbolIs(kParamMicY, "micY")
              && symbolIs(kParamDirect, "direct") && symbolIs(kParamEarly, "early")
              && symbolIs(kParamLate, "late")
              && symbolIs(kParamSideAbsorb, "sdAbs") && symbolIs(kParamSideBright, "sdBrt")
              && symbolIs(kParamSideRing, "sdRing") && symbolIs(kParamSideRingMat, "sdMat")
              && symbolIs(kParamSideRingRatio, "sdRatio") && symbolIs(kParamSideRingDecay, "sdDecay")
              && symbolIs(kParamSideRingDamp, "sdDamp") && symbolIs(kParamSideLink, "sdLink")
              && symbolIs(kParamFBAbsorb, "fbAbs") && symbolIs(kParamFBBright, "fbBrt")
              && symbolIs(kParamFBRing, "fbRing") && symbolIs(kParamFBRingMat, "fbMat")
              && symbolIs(kParamFBRingRatio, "fbRatio") && symbolIs(kParamFBRingDecay, "fbDecay")
              && symbolIs(kParamFBRingDamp, "fbDamp") && symbolIs(kParamFBLink, "fbLink"),
              "kParams rows are out of ParamIndex order");

// --- dropdowns -------------------------------------------------------------------------------
// One source of truth for "is this parameter stepped-with-labels, and what are the labels": the
// CLAP params extension (CLAP_PARAM_IS_ENUM + value/text conversion) and the UI dropdowns both read
// it from here, so a label can never drift between the host's generic panel and our own.

static constexpr const char* kVoiceModeChoices[]   = { "Mono", "Poly 4", "Poly 10", "Poly 20", "Poly 32", nullptr };
static constexpr const char* kOffOnChoices[]       = { "Off", "On", nullptr };
// A wall rings as one of the static material tables (the generators need a per-instance buffer).
static constexpr const char* kWallMaterialNames[]  = {
   "String", "Bar", "Tuned Bar", "Membrane", "Open Tube", "Closed Tube", "Tine", "Bell",
   "Guitar Body", "Violin Body", "Soundboard", "Generic Body", nullptr };
static constexpr const char* kStrikeTypeChoices[]  = { "Mallet", "Noise",   nullptr };
static constexpr const char* kContactModeChoices[] = { "Note-Rel", "Wall-Clk", nullptr };
static constexpr const char* kMpeChoices[]         = { "Off", "On",         nullptr };

inline const char* const* choicesFor(uint32_t index)
{
   switch (index) {
   case kParamVoiceMode:   return kVoiceModeChoices;
   case kParamStrikeType:  return kStrikeTypeChoices;
   case kParamContactMode: return kContactModeChoices;
   case kParamMpe:         return kMpeChoices;
   case kParamMaterial:    return kMaterialNames;
   case kParamSideRingMat:
   case kParamFBRingMat:   return kWallMaterialNames;
   case kParamSideLink:
   case kParamFBLink:      return kOffOnChoices;
   default:                return nullptr;
   }
}

inline bool isDropdownParam(uint32_t index) { return choicesFor(index) != nullptr; }

} // namespace struckroom

#endif // STRUCKROOM_PARAMS_H
