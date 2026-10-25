#include "pm_param_data.h"

// SinOsc — 11 params. All defaults 0 (→ produces A440 with no pitch input connected).
// name / item / unit / flags / valueStrings / min / max / def / info
static const ModuleParamConstants sinOscConstants[] = {
   {"pitch",         SinOscParamItemPitch,         0, 0, nullptr, -48.0f, 48.0f, 0.0f, 0},
   {"cents",         SinOscParamItemPitchFine,     0, 0, nullptr, -100.0f, 100.0f, 0.0f, 0},
   {"pitchInput",    SinOscParamItemPitchInput,    0, 0, nullptr, 0.0f, 0.0f, 0.0f, 0},
   {"modPitch",      SinOscParamItemModPitch,      0, 0, nullptr, 0.0f, 0.0f, 0.0f, 0},
   {"modPitch2",     SinOscParamItemModPitch2,     0, 0, nullptr, 0.0f, 0.0f, 0.0f, 0},
   {"modPhase",      SinOscParamItemModPhase,      0, 0, nullptr, 0.0f, 0.0f, 0.0f, 0},
   {"freqModLevel",  SinOscParamItemFreqModLevel,  0, 0, nullptr, -1.0f, 1.0f, 0.0f, 0},
   {"phaseModLevel", SinOscParamItemPhaseModLevel, 0, 0, nullptr, 0.0f, 1.0f, 0.0f, 0},
   {"freqModLevel2", SinOscParamItemFreqModLevel2, 0, 0, nullptr, 0.0f, 1.0f, 0.0f, 0},
   {"modSync",       SinOscParamItemModSync,       0, 0, nullptr, 0.0f, 0.0f, 0.0f, 0},
   {"modSyncType",   SinOscParamItemModSyncType,   0, 0, nullptr, 0.0f, 2.0f, 0.0f, 0},
   {"detune",        SinOscParamItemDetune,        0, 0, nullptr, 0.0f, 25.0f, 0.0f, 0},
   {"waveform",      SinOscParamItemWaveform,      0, 0, nullptr, 0.0f,  2.0f, 0.0f, 0},
};

// Level — gain stage (output = input × level). Default unity; the plugin sets the sine mix level.
static const ModuleParamConstants levelConstants[] = {
   {"input", LevelParamItemInput, 0, 0, nullptr, 0.0f, 0.0f, 0.0f, 0}, // connection
   {"level", LevelParamItemLevel, 0, 0, nullptr, 0.0f, 1.0f, 1.0f, 0},
};
const ModuleParamConstantsGroup levelConstantsGroup = {
   const_cast<char*>("level"), sizeof(levelConstants) / sizeof(levelConstants[0]), levelConstants};

const ModuleParamConstantsGroup sinOscConstantsGroup = {
   const_cast<char*>("sinOsc"),
   sizeof(sinOscConstants) / sizeof(sinOscConstants[0]),
   sinOscConstants,
};

// IFS oscillator — trimmed v1. Defaults match IFSShapeParams{} in IFSCore.hpp so the initial
// wavetable and the parameter tree agree.
static const ModuleParamConstants ifsOscConstants[] = {
   {"pitchInput", IFSOscParamItemPitchInput, 0, 0, nullptr,   0.0f,   0.0f,  0.0f, 0},
   {"pitch",      IFSOscParamItemPitch,      0, 0, nullptr, -24.0f,  24.0f,  0.0f, 0},
   {"cents",      IFSOscParamItemPitchFine,  0, 0, nullptr, -100.0f, 100.0f, 0.0f, 0},
   {"ifs1Squash", IFSOscParamItemIFS1Squash, 0, 0, nullptr, -0.99f,  0.99f,  0.5f, 0},
   {"ifs2Squash", IFSOscParamItemIFS2Squash, 0, 0, nullptr, -0.99f,  0.99f, -0.5f, 0},
   {"bx",         IFSOscParamItemBx,         0, 0, nullptr,  -1.0f,   1.0f,  0.0f, 0},
   {"by",         IFSOscParamItemBy,         0, 0, nullptr,  -1.0f,   1.0f,  0.5f, 0},
   {"startEndY",  IFSOscParamItemStartEndY,  0, 0, nullptr,  -1.0f,   1.0f, -1.0f, 0},
   {"smoothing",  IFSOscParamItemSmoothing,  0, 0, nullptr,   0.0f,   1.0f,  0.5f, 0},
   {"iterations", IFSOscParamItemIterations, 0, 0, nullptr,   0.0f,   1.0f,  1.0f, 0},
   {"pitchModIn",  IFSOscParamItemPitchModInput,   0, 0, nullptr, 0.0f, 0.0f, 0.0f, 0},
   {"pitchModLvl", IFSOscParamItemPitchModLevel,   0, 0, nullptr, -1.0f, 1.0f, 0.0f, 0},
   {"bxModIn",     IFSOscParamItemBxModInput,      0, 0, nullptr, 0.0f, 0.0f, 0.0f, 0},
   {"bxModLvl",    IFSOscParamItemBxModLevel,      0, 0, nullptr, -1.0f, 1.0f, 0.0f, 0},
   {"byModIn",     IFSOscParamItemByModInput,      0, 0, nullptr, 0.0f, 0.0f, 0.0f, 0},
   {"byModLvl",    IFSOscParamItemByModLevel,      0, 0, nullptr, -1.0f, 1.0f, 0.0f, 0},
   {"sq1ModIn",    IFSOscParamItemSquash1ModInput, 0, 0, nullptr, 0.0f, 0.0f, 0.0f, 0},
   {"sq1ModLvl",   IFSOscParamItemSquash1ModLevel, 0, 0, nullptr, -1.0f, 1.0f, 0.0f, 0},
   {"sq2ModIn",    IFSOscParamItemSquash2ModInput, 0, 0, nullptr, 0.0f, 0.0f, 0.0f, 0},
   {"sq2ModLvl",   IFSOscParamItemSquash2ModLevel, 0, 0, nullptr, -1.0f, 1.0f, 0.0f, 0},
   {"detune",      IFSOscParamItemDetune,          0, 0, nullptr,  0.0f, 25.0f, 0.0f, 0},
   {"syncIn",      IFSOscParamItemSyncInput,       0, 0, nullptr,  0.0f,  0.0f, 0.0f, 0}, // connection
   {"syncMode",    IFSOscParamItemSyncMode,        0, 0, nullptr,  0.0f,  3.0f, 0.0f, 0}, // 0=Off
   {"level",       IFSOscParamItemLevel,           0, 0, nullptr,  0.0f,  1.0f, 1.0f, 0}, // unity; plugin sets mix
};
const ModuleParamConstantsGroup ifsOscConstantsGroup = {
   const_cast<char*>("ifsOsc"),
   sizeof(ifsOscConstants) / sizeof(ifsOscConstants[0]),
   ifsOscConstants,
};

// IFS envelope. Defaults picked for a musical amplitude envelope: startEndY=0 (silence at the
// ends), blue peak → green sustain, holdMode=Green so it holds at green while the note is held
// and releases to 0 on note-off (ADSR-like). NOTE these differ from IFSEnvShapeParams{}; the
// module renders struct defaults at init then re-renders from these on the first process.
static const ModuleParamConstants ifsEnvConstants[] = {
   {"gateInput",  IFSEnvParamItemGateInput,  0, 0, nullptr,  0.0f,   0.0f,  0.0f,  0},
   {"duration",   IFSEnvParamItemDuration,   0, 0, nullptr,  0.01f,  10.0f, 1.0f,  0},
   {"startEndY",  IFSEnvParamItemStartEndY,  0, 0, nullptr, -1.0f,   1.0f,  0.0f,  0},
   {"blueX",      IFSEnvParamItemBlueX,      0, 0, nullptr,  0.0f,   1.0f,  0.15f, 0},
   {"blueY",      IFSEnvParamItemBlueY,      0, 0, nullptr, -1.0f,   1.0f,  1.0f,  0},
   {"greenX",     IFSEnvParamItemGreenX,     0, 0, nullptr,  0.0f,   1.0f,  0.45f, 0},
   {"greenY",     IFSEnvParamItemGreenY,     0, 0, nullptr, -1.0f,   1.0f,  0.5f,  0},
   {"ifs1Squash", IFSEnvParamItemIFS1Squash, 0, 0, nullptr, -0.99f,  0.99f, 0.2f,  0},
   {"ifs2Squash", IFSEnvParamItemIFS2Squash, 0, 0, nullptr, -0.99f,  0.99f, 0.2f,  0},
   {"ifs3Squash", IFSEnvParamItemIFS3Squash, 0, 0, nullptr, -0.99f,  0.99f, 0.2f,  0},
   {"smoothing",  IFSEnvParamItemSmoothing,  0, 0, nullptr,  0.0f,   1.0f,  0.5f,  0},
   {"iterations", IFSEnvParamItemIterations, 0, 0, nullptr,  0.0f,   7.0f,  2.0f,  0},
   {"holdMode",   IFSEnvParamItemHoldMode,   0, 0, nullptr,  0.0f,   3.0f,  2.0f,  0},
   {"legatoMode", IFSEnvParamItemLegatoMode, 0, 0, nullptr,  0.0f,   1.0f,  0.0f,  0},
};
const ModuleParamConstantsGroup ifsEnvConstantsGroup = {
   const_cast<char*>("ifsEnv"),
   sizeof(ifsEnvConstants) / sizeof(ifsEnvConstants[0]),
   ifsEnvConstants,
};

// IFS LFO — 2-transform shape, defaults matching IFSShapeParams{} (iterations param 3 → 4 iters).
static const ModuleParamConstants ifsLFOConstants[] = {
   {"pitch",      IFSLFOParamItemPitch,      0, 0, nullptr, -7.0f,  7.0f,  0.0f, 0},
   {"bx",         IFSLFOParamItemBx,         0, 0, nullptr, -1.0f,  1.0f,  0.0f, 0},
   {"by",         IFSLFOParamItemBy,         0, 0, nullptr, -1.0f,  1.0f,  0.5f, 0},
   {"ifs1Squash", IFSLFOParamItemIFS1Squash, 0, 0, nullptr, -0.99f, 0.99f, 0.5f, 0},
   {"ifs2Squash", IFSLFOParamItemIFS2Squash, 0, 0, nullptr, -0.99f, 0.99f,-0.5f, 0},
   {"startEndY",  IFSLFOParamItemStartEndY,  0, 0, nullptr, -1.0f,  1.0f, -1.0f, 0},
   {"smoothing",  IFSLFOParamItemSmoothing,  0, 0, nullptr,  0.0f,  1.0f,  0.5f, 0},
   {"iterations", IFSLFOParamItemIterations, 0, 0, nullptr,  0.0f,  7.0f,  3.0f, 0},
};
const ModuleParamConstantsGroup ifsLFOConstantsGroup = {
   const_cast<char*>("ifsLFO"),
   sizeof(ifsLFOConstants) / sizeof(ifsLFOConstants[0]),
   ifsLFOConstants,
};

// CV mixer. LFO/Env enabled by default (summed); CC off, CC#1, mode = add.
static const ModuleParamConstants cvMixerConstants[] = {
   {"lfoInput",  CVMixerParamItemLFOInput,  0, 0, nullptr, 0.0f,   0.0f, 0.0f, 0}, // connection
   {"envInput",  CVMixerParamItemEnvInput,  0, 0, nullptr, 0.0f,   0.0f, 0.0f, 0}, // connection
   {"enableLFO", CVMixerParamItemEnableLFO, 0, 0, nullptr, 0.0f,   1.0f, 1.0f, 0},
   {"enableEnv", CVMixerParamItemEnableEnv, 0, 0, nullptr, 0.0f,   1.0f, 1.0f, 0},
   {"enableCC",  CVMixerParamItemEnableCC,  0, 0, nullptr, 0.0f,   1.0f, 0.0f, 0},
   {"ccNumber",  CVMixerParamItemCCNumber,  0, 0, nullptr, 1.0f, 127.0f, 1.0f, 0},
   {"mode",      CVMixerParamItemMode,      0, 0, nullptr, 0.0f,   1.0f, 0.0f, 0},
};
const ModuleParamConstantsGroup cvMixerConstantsGroup = {
   const_cast<char*>("cvMixer"),
   sizeof(cvMixerConstants) / sizeof(cvMixerConstants[0]),
   cvMixerConstants,
};

// LPF — 24 dB/oct Butterworth. Cutoff default 5 octaves above A440 (~14 kHz, near-open); Q = 1
// (flat Butterworth). Input + mod inputs are connection ports (value 0); mod levels default 0.
static const ModuleParamConstants lpfConstants[] = {
   {"input",         LPFParamItemInput,          0, 0, nullptr,  0.0f,  0.0f,  0.0f, 0}, // connection
   {"frequency",     LPFParamItemPitch,          0, 0, nullptr, -6.0f,  5.4f,  5.0f, 0},
   {"freqModLevel",  LPFParamItemFreqModLevel,   0, 0, nullptr, -8.0f,  8.0f,  0.0f, 0},
   {"q",             LPFParamItemQ,              0, 0, nullptr,  1.0f, 20.0f,  1.0f, 0},
   {"freqModInput",  LPFParamItemModFreqInput,   0, 0, nullptr,  0.0f,  0.0f,  0.0f, 0}, // connection
   {"freqModInput2", LPFParamItemModFreqInput2,  0, 0, nullptr,  0.0f,  0.0f,  0.0f, 0}, // connection
   {"freqModLevel2", LPFParamItemFreqModLevel2,  0, 0, nullptr, -8.0f,  8.0f,  0.0f, 0},
   {"qModInput",     LPFParamItemModQInput,      0, 0, nullptr,  0.0f,  0.0f,  0.0f, 0}, // connection
   {"qModLevel",     LPFParamItemModQInputLevel, 0, 0, nullptr,-19.0f, 19.0f,  0.0f, 0},
};
const ModuleParamConstantsGroup lpfConstantsGroup = {
   const_cast<char*>("lpf"),
   sizeof(lpfConstants) / sizeof(lpfConstants[0]),
   lpfConstants,
};

// ADSR — 7 params. Times in seconds; sustain is a level 0..1.
static const ModuleParamConstants adsrConstants[] = {
   {"attack",    ADSRParamItemAttack,    0, 0, nullptr, 0.001f, 10.0f, 0.01f, 0},
   {"decay",     ADSRParamItemDecay,     0, 0, nullptr, 0.001f, 10.0f, 1.00f, 0},
   {"sustain",   ADSRParamItemSustain,   0, 0, nullptr, 0.0f, 1.0f, 0.5f, 0},
   {"release",   ADSRParamItemRelease,   0, 0, nullptr, 0.001f, 10.0f, 1.00f, 0},
   {"gateInput", ADSRParamItemGateInput, 0, 0, nullptr, 0.0f, 3.4e38f, 0.0f, 0},
   {"legato",    ADSRParamItemLegato,    0, 0, nullptr, 0.0f, 2.0f, 0.0f, 0},
   {"linear",    ADSRParamItemLinear,    0, 0, nullptr, 0.0f, 1.0f, 0.0f, 0},
};

const ModuleParamConstantsGroup adsrConstantsGroup = {
   const_cast<char*>("adsr"),
   sizeof(adsrConstants) / sizeof(adsrConstants[0]),
   adsrConstants,
};

// Combine — 4-quadrant multiplier (the VCA). Type defaults to Multiply, Level 1.0.
static const ModuleParamConstants combineConstants[] = {
   {"input1", CombineParamItemInput1, 0, 0, nullptr, 0.0f, 0.0f, 0.0f, 0},
   {"input2", CombineParamItemInput2, 0, 0, nullptr, 0.0f, 0.0f, 0.0f, 0},
   {"input3", CombineParamItemInput3, 0, 0, nullptr, 0.0f, 0.0f, 0.0f, 0},
   {"type", CombineParamItemType, 0, 0, nullptr, (float)ParamItemCombineType_Add,
    (float)ParamItemCombineType_Multiply, (float)ParamItemCombineType_Multiply, 0},
   {"level", CombineParamItemLevel, 0, 0, nullptr, 0.0f, 1.0f, 1.0f, 0},
};
const ModuleParamConstantsGroup combineConstantsGroup = {
   const_cast<char*>("combine"), sizeof(combineConstants) / sizeof(combineConstants[0]),
   combineConstants};

// Final — output stage (DC block / level / M-S spread / clip).
static const ModuleParamConstants finalConstants[] = {
   {"input", FinalOutputParamItemInput, 0, 0, nullptr, 0.0f, 0.0f, 0.0f, 0},
   {"spread", FinalOutputParamItemSpread, 0, 0, nullptr, 0.0f, 1.0f, 0.7f, 0},
   {"level", FinalOutputParamItemLevel, 0, 0, nullptr, 0.0f, 1.0f, 0.7f, 0},
   {"clipLevel", FinalOutputParamItemClipLevel, 0, 0, nullptr, 0.0f, 1.0f, 1.0f, 0},
   {"filterDC", FinalOutputParamItemFilterDC, 0, 0, nullptr, 0.0f, 1.0f, 1.0f, 0},
};
const ModuleParamConstantsGroup finalConstantsGroup = {
   const_cast<char*>("final"), sizeof(finalConstants) / sizeof(finalConstants[0]), finalConstants};

// Slew — slew-rate limiter (smooths the pitch signal). Rise/Fall in seconds per unit.
static const ModuleParamConstants slewConstants[] = {
   {"input", SlewParamItemInput, 0, 0, nullptr, 0.0f, 0.0f, 0.0f, 0},
   {"rise", SlewParamItemRise, 0, 0, nullptr, 1e-8f, 10.0f, 0.01f, 0},
   {"fall", SlewParamItemFall, 0, 0, nullptr, 1e-8f, 10.0f, 0.01f, 0},
   {"curve", SlewParamItemCurve, 0, 0, nullptr, 0.0f, 0.0f, 0.0f, 0},
};
const ModuleParamConstantsGroup slewModuleConstantsGroup = {
   const_cast<char*>("slew"), sizeof(slewConstants) / sizeof(slewConstants[0]), slewConstants};

// PitchConverter — MIDI note → pitch (1/octave). One placeholder param.
static const ModuleParamConstants pitchConverterConstants[] = {
   {"dummy", PitchConverterParamItemDummy, 0, 0, nullptr, 0.0f, 0.0f, 0.0f, 0},
};
const ModuleParamConstantsGroup pitchConverterModuleConstantsGroup = {
   const_cast<char*>("pitchConverter"),
   sizeof(pitchConverterConstants) / sizeof(pitchConverterConstants[0]), pitchConverterConstants};

// Channel pressure — pressure (channel, poly, CLAP Pressure) → a held 0..1 value per voice.
static const ModuleParamConstants channelPressureConstants[] = {
   {"dummy", ChannelPressureParamItemDummy, 0, 0, nullptr, 0.0f, 0.0f, 0.0f, 0},
   {"mpe",   ChannelPressureParamItemMPE,   0, 0, nullptr, 0.0f, 1.0f, 0.0f, 0},
};
const ModuleParamConstantsGroup channelPressureConstantsGroup = {
   const_cast<char*>("Channel Pressure"),
   sizeof(channelPressureConstants) / sizeof(channelPressureConstants[0]), channelPressureConstants};

// CC converter — a chosen controller number → a held value; Per Note mode makes CC74 the Timbre source.
static const ModuleParamConstants ccConverterConstants[] = {
   {"ccNumber",    CCParamItemNumber,    0, 0, nullptr, 0.0f, 127.0f, 1.0f, 0},
   {"ccIsBoolean", CCParamItemIsBoolean, 0, 0, nullptr, 0.0f, 1.0f, 0.0f, 0},
   {"input",       CCParamItemInput,     0, 0, nullptr, 0.0f, 0.0f, 0.0f, 0}, // connection
   {"ccAmount",    CCParamItemAmount,    0, 0, nullptr, 0.0f, 1.0f, 1.0f, 0},
   {"ccOffset",    CCParamItemOffset,    0, 0, nullptr, 0.0f, 1.0f, 0.0f, 0},
   {"ccPerNote",   CCParamItemPerNote,   0, 0, nullptr, 0.0f, 1.0f, 0.0f, 0},
   {"ccMpe",       CCParamItemMPE,       0, 0, nullptr, 0.0f, 1.0f, 0.0f, 0},
};
const ModuleParamConstantsGroup ccConverterConstantsGroup = {
   const_cast<char*>("CC Converter"), sizeof(ccConverterConstants) / sizeof(ccConverterConstants[0]), ccConverterConstants};

// Pitch bend converter — MIDI 2.0 pitch bend → a bipolar octaves signal. Bend range in quarter-tones
// (±2 semitones by default), used by per-note bend too until registered controller 7 sets its own.
static const ModuleParamConstants pitchBendConverterConstants[] = {
   {"quarterTones",     PitchBendConverterParamItemQuarterTones,     0, 0, nullptr, 0.0f, 8.0f,  4.0f,  0},
   {"mpe",              PitchBendConverterParamItemMPE,              0, 0, nullptr, 0.0f, 1.0f,  0.0f,  0},
};
const ModuleParamConstantsGroup pitchBendConverterConstantsGroup = {
   const_cast<char*>("Pitch Bend"),
   sizeof(pitchBendConverterConstants) / sizeof(pitchBendConverterConstants[0]), pitchBendConverterConstants};

// Strike — note/gate → shaped excitation burst. Defaults from PM (ParamGroupData.m): Mallet,
// hardness 0.3, note-relative contact, level 1. Items 1-2 (gate/hardness) are connection ports.
static const ModuleParamConstants strikeConstants[] = {
   {"gateInput",        StrikeParamItemGateInput,        0, 0, nullptr, 0.0f, 0.0f, 0.0f, 0}, // connection
   {"hardnessInput",    StrikeParamItemHardnessInput,    0, 0, nullptr, 0.0f, 0.0f, 0.0f, 0}, // connection
   {"type",             StrikeParamItemType,             0, 0, nullptr, (float)StrikeTypeMallet, (float)StrikeTypeNoise, (float)StrikeTypeMallet, 0},
   {"hardness",         StrikeParamItemHardness,         0, 0, nullptr, 0.0f, 1.0f, 0.3f, 0},
   {"hardnessModLevel", StrikeParamItemHardnessModLevel, 0, 0, nullptr, -1.0f, 1.0f, 1.0f, 0},
   {"velToLevel",       StrikeParamItemVelToLevel,       0, 0, nullptr, 0.0f, 1.0f, 1.0f, 0},
   {"velToHardness",    StrikeParamItemVelToHardness,    0, 0, nullptr, 0.0f, 1.0f, 0.5f, 0},
   {"level",            StrikeParamItemLevel,            0, 0, nullptr, 0.0f, 2.0f, 1.0f, 0},
   {"contactMode",      StrikeParamItemContactMode,      0, 0, nullptr, (float)StrikeContactNoteRelative, (float)StrikeContactWallClock, (float)StrikeContactNoteRelative, 0},
};
const ModuleParamConstantsGroup strikeConstantsGroup = {
   const_cast<char*>("strike"), sizeof(strikeConstants) / sizeof(strikeConstants[0]), strikeConstants};

// ModalBank — damped modal resonator bank. Defaults from PM (ParamGroupData.m). Items 1-5 are
// connection ports (0,0,0). Material default = Bar; Stretch is an INDEX into kModalStretchValues.
static const ModuleParamConstants modalBankConstants[] = {
   {"input",          ModalBankParamItemInput,          0, 0, nullptr, 0.0f, 0.0f, 0.0f, 0}, // connection (excitation)
   {"pitchInput",     ModalBankParamItemPitchInput,     0, 0, nullptr, 0.0f, 0.0f, 0.0f, 0}, // connection
   {"strikeInput",    ModalBankParamItemStrikeInput,    0, 0, nullptr, 0.0f, 0.0f, 0.0f, 0}, // connection
   {"decayModInput",  ModalBankParamItemDecayModInput,  0, 0, nullptr, 0.0f, 0.0f, 0.0f, 0}, // connection
   {"brightModInput", ModalBankParamItemBrightModInput, 0, 0, nullptr, 0.0f, 0.0f, 0.0f, 0}, // connection
   {"pitch",          ModalBankParamItemPitch,          0, 0, nullptr, -4.0f, 4.0f, 0.0f, 0},
   {"pitchModLevel",  ModalBankParamItemPitchModLevel,  0, 0, nullptr, -5.0f, 5.0f, 1.0f, 0},
   {"material",       ModalBankParamItemMaterial,       0, 0, nullptr, (float)ModalBankMaterialString, (float)ModalBankMaterialGenHarmonicNumber, (float)ModalBankMaterialBar, 0},
   {"modeCount",      ModalBankParamItemModeCount,      0, 0, nullptr, 1.0f, 64.0f, 64.0f, 0},
   {"stretch",        ModalBankParamItemStretch,        0, 0, nullptr, 0.0f, 19.0f, 9.0f, 0}, // index into kModalStretchValues
   {"decayT60",       ModalBankParamItemDecayT60,       0, 0, nullptr, 0.02f, 60.0f, 2.0f, 0},
   {"decayModLevel",  ModalBankParamItemDecayModLevel,  0, 0, nullptr, -5.0f, 5.0f, 0.0f, 0},
   {"dampTilt",       ModalBankParamItemDampTilt,       0, 0, nullptr, 0.0f, 3.0f, 0.9f, 0},
   {"dampBellAmount", ModalBankParamItemDampBellAmount, 0, 0, nullptr, -4.0f, 4.0f, 0.0f, 0},
   {"dampBellCentre", ModalBankParamItemDampBellCentre, 0, 0, nullptr, 0.0f, 7.0f, 2.0f, 0},
   {"dampBellWidth",  ModalBankParamItemDampBellWidth,  0, 0, nullptr, 0.1f, 4.0f, 0.4f, 0},
   {"strikePosition", ModalBankParamItemStrikePosition, 0, 0, nullptr, 0.0f, 1.0f, 0.25f, 0},
   {"strikeModLevel", ModalBankParamItemStrikeModLevel, 0, 0, nullptr, -5.0f, 5.0f, 0.0f, 0},
   {"brightness",     ModalBankParamItemBrightness,     0, 0, nullptr, -1.0f, 1.0f, 0.0f, 0},
   {"brightModLevel", ModalBankParamItemBrightModLevel, 0, 0, nullptr, -5.0f, 5.0f, 0.0f, 0},
   {"spread",         ModalBankParamItemSpread,         0, 0, nullptr, 0.0f, 1.0f, 0.3f, 0},
   {"mix",            ModalBankParamItemMix,            0, 0, nullptr, 0.0f, 1.0f, 1.0f, 0},
   {"level",          ModalBankParamItemLevel,          0, 0, nullptr, 0.0f, 2.0f, 0.5f, 0},
   {"pitchMod2Input", ModalBankParamItemPitchMod2Input, 0, 0, nullptr, 0.0f, 0.0f, 0.0f, 0}, // connection (pitch bend)
   {"pitchMod2Level", ModalBankParamItemPitchMod2Level, 0, 0, nullptr, -5.0f, 5.0f, 1.0f, 0},
   {"dampTiltModInput", ModalBankParamItemDampTiltModInput, 0, 0, nullptr, 0.0f, 0.0f, 0.0f, 0}, // connection
   {"dampTiltModLevel", ModalBankParamItemDampTiltModLevel, 0, 0, nullptr, -3.0f, 3.0f, 0.0f, 0},
   {"decayTrack",     ModalBankParamItemDecayTrack,     0, 0, nullptr, 0.0f, 1.0f, 0.0f, 0},
};
const ModuleParamConstantsGroup modalBankConstantsGroup = {
   const_cast<char*>("modalBank"), sizeof(modalBankConstants) / sizeof(modalBankConstants[0]), modalBankConstants};

// Room — the pitch-scaled 2-D room. Defaults match Room::Settings (Room.hpp). Items 1-2 are
// connection ports.
static const ModuleParamConstants roomConstants[] = {
   {"input",        RoomParamItemInput,         0, 0, nullptr, 0.0f, 0.0f, 0.0f, 0}, // connection
   {"pitchInput",   RoomParamItemPitchInput,    0, 0, nullptr, 0.0f, 0.0f, 0.0f, 0}, // connection
   {"pitch",        RoomParamItemPitch,         0, 0, nullptr, -4.0f, 4.0f, 0.0f, 0},
   {"width",        RoomParamItemWidth,         0, 0, nullptr, 0.25f, 16.0f, 3.0f, 0},
   {"depth",        RoomParamItemDepth,         0, 0, nullptr, 0.25f, 16.0f, 2.2f, 0},
   {"roomTrack",    RoomParamItemRoomTrack,     0, 0, nullptr, 0.0f, 1.0f, 1.0f, 0},
   {"decayTrack",   RoomParamItemDecayTrack,    0, 0, nullptr, 0.0f, 1.0f, 0.0f, 0},
   {"air",          RoomParamItemAir,           0, 0, nullptr, 0.5f, 30.0f, 12.0f, 0},
   {"detail",       RoomParamItemDetail,        0, 0, nullptr, 2.0f, 64.0f, 12.0f, 0},
   {"srcX",         RoomParamItemSrcX,          0, 0, nullptr, 0.0f, 1.0f, 0.31f, 0},
   {"srcY",         RoomParamItemSrcY,          0, 0, nullptr, 0.0f, 1.0f, 0.37f, 0},
   {"micX",         RoomParamItemMicX,          0, 0, nullptr, 0.0f, 1.0f, 0.62f, 0},
   {"micY",         RoomParamItemMicY,          0, 0, nullptr, 0.0f, 1.0f, 0.71f, 0},
   {"micWidth",     RoomParamItemMicWidth,      0, 0, nullptr, 0.0f, 1.0f, 1.0f, 0},
   {"direct",       RoomParamItemDirect,        0, 0, nullptr, 0.0f, 2.0f, 1.0f, 0},
   {"early",        RoomParamItemEarly,         0, 0, nullptr, 0.0f, 2.0f, 1.0f, 0},
   {"late",         RoomParamItemLate,          0, 0, nullptr, 0.0f, 2.0f, 1.0f, 0},
   {"sideAbsorb",   RoomParamItemSideAbsorb,    0, 0, nullptr, 0.0f, 1.0f, 0.15f, 0},
   {"sideBright",   RoomParamItemSideBright,    0, 0, nullptr, 0.0f, 8.0f, 5.0f, 0},
   {"sideRing",     RoomParamItemSideRing,      0, 0, nullptr, 0.0f, 2.0f, 0.0f, 0},
   {"sideRingMat",  RoomParamItemSideRingMat,   0, 0, nullptr, 0.0f, 25.0f, 3.0f, 0},
   {"sideRingRatio",RoomParamItemSideRingRatio, 0, 0, nullptr, 0.0625f, 4.0f, 0.5f, 0},
   {"sideRingT60",  RoomParamItemSideRingT60,   0, 0, nullptr, 0.05f, 20.0f, 1.5f, 0},
   {"sideRingTilt", RoomParamItemSideRingTilt,  0, 0, nullptr, 0.0f, 3.0f, 1.0f, 0},
   {"sideLink",     RoomParamItemSideLink,      0, 0, nullptr, 0.0f, 1.0f, 0.0f, 0},
   {"fbAbsorb",     RoomParamItemFBAbsorb,      0, 0, nullptr, 0.0f, 1.0f, 0.15f, 0},
   {"fbBright",     RoomParamItemFBBright,      0, 0, nullptr, 0.0f, 8.0f, 5.0f, 0},
   {"fbRing",       RoomParamItemFBRing,        0, 0, nullptr, 0.0f, 2.0f, 0.0f, 0},
   {"fbRingMat",    RoomParamItemFBRingMat,     0, 0, nullptr, 0.0f, 25.0f, 3.0f, 0},
   {"fbRingRatio",  RoomParamItemFBRingRatio,   0, 0, nullptr, 0.0625f, 4.0f, 0.5f, 0},
   {"fbRingT60",    RoomParamItemFBRingT60,     0, 0, nullptr, 0.05f, 20.0f, 1.5f, 0},
   {"fbRingTilt",   RoomParamItemFBRingTilt,    0, 0, nullptr, 0.0f, 3.0f, 1.0f, 0},
   {"fbLink",       RoomParamItemFBLink,        0, 0, nullptr, 0.0f, 1.0f, 0.0f, 0},
   {"sideShift",    RoomParamItemSideShift,     0, 0, nullptr, 0.0f, 10.0f, 0.0f, 0},   // % of f0
   {"sideFlex",     RoomParamItemSideFlex,      0, 0, nullptr, 0.0f, 1.0f, 0.0f, 0},
   {"sideRattle",   RoomParamItemSideRattle,    0, 0, nullptr, 0.0f, 1.0f, 0.0f, 0},
   {"sideGap",      RoomParamItemSideGap,       0, 0, nullptr, 0.02f, 0.95f, 0.15f, 0},
   {"fbShift",      RoomParamItemFBShift,       0, 0, nullptr, 0.0f, 10.0f, 0.0f, 0},
   {"fbFlex",       RoomParamItemFBFlex,        0, 0, nullptr, 0.0f, 1.0f, 0.0f, 0},
   {"fbRattle",     RoomParamItemFBRattle,      0, 0, nullptr, 0.0f, 1.0f, 0.0f, 0},
   {"fbGap",        RoomParamItemFBGap,         0, 0, nullptr, 0.02f, 0.95f, 0.15f, 0},
};
const ModuleParamConstantsGroup roomConstantsGroup = {
   const_cast<char*>("room"), sizeof(roomConstants) / sizeof(roomConstants[0]), roomConstants};
