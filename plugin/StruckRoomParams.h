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
   kParamVoiceMode = 0, // 0 = Mono (1 voice + glide), 1..3 = Poly 10 / 20 / 32
   kParamGlide,         // pitch glide — drives the Slew module's Rise and Fall together (mono)
   kParamLevel,         // master level → ModalBank Level
   kParamSpread,        // stereo distribution of modes → ModalBank Spread
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
   {"Voices",     "voices",   "",    0.0f,   3.0f,   1.0f,  false, true },
   {"Glide",      "glide",    "s",   0.0001f, 2.0f,  0.01f, true,  false},
   {"Level",      "level",    "",    0.0f,   2.0f,   0.5f,  false, false},
   {"Spread",     "spread",   "",    0.0f,   1.0f,   0.3f,  false, false},
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
              && symbolIs(kParamTimbreToTilt, "tmbTilt") && symbolIs(kParamTimbreToPos, "tmbPos"),
              "kParams rows are out of ParamIndex order");

// --- dropdowns -------------------------------------------------------------------------------
// One source of truth for "is this parameter stepped-with-labels, and what are the labels": the
// CLAP params extension (CLAP_PARAM_IS_ENUM + value/text conversion) and the UI dropdowns both read
// it from here, so a label can never drift between the host's generic panel and our own.

static constexpr const char* kVoiceModeChoices[]   = { "Mono", "Poly 10", "Poly 20", "Poly 32", nullptr };
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
   default:                return nullptr;
   }
}

inline bool isDropdownParam(uint32_t index) { return choicesFor(index) != nullptr; }

} // namespace struckroom

#endif // STRUCKROOM_PARAMS_H
