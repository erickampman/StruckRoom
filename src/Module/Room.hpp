//
//  Room.hpp
//
//  The space a struck object sits in: a 2-D rectangular room whose every length
//  scales with the note, driven by the object's sound (a ModalBank's output).
//
//    object -> direct path                     delay, 1/sqrt(r)
//           -> 4 first-order wall reflections  delay, wall absorption filter, and
//                                              the wall effects: flex, shift
//           -> 4 wall resonators               a ModalBank per wall, rung by the
//                                              sound reaching it, struck where
//                                              that sound meets the wall; rattle
//           -> room modes (nx, ny)             the standing waves: gains from the
//                                              source and listener positions,
//                                              T60s from the walls' absorption
//
//  Lengths are in WAVELENGTHS OF THE NOTE (lambda0 = c / f0): a path r long
//  takes r / f0 seconds, so the same settings describe the same scene at every
//  pitch. Room Track and Decay Track bend that rule (see Settings).
//
//  The listener is a coincident Mid/Side pair, an omni plus a figure-8 facing
//  +x, so it has no length in it either. L = M + width*S, R = M - width*S.
//
//  Framework-free, like ModalBank: the PolygraphModular wrapper (RoomModule) only
//  marshals parameters and pitch. Real-time safe: all storage is fixed-size and
//  nothing allocates after construction. tools/roomlab is the offline original.
//

#ifndef Room_hpp
#define Room_hpp

#include "ModalBank.hpp"

#include <cstdint>

// The two hot loops (retunePitch and processChunk) are built twice on x86-64 Linux, for
// AVX2 + FMA and for the baseline, and the loader picks one per CPU. The baseline has only 16
// 4-wide registers, so the mode loop's 8-wide vectors spill on every sample: measured on an
// i3-9100 at Detail 21, 205% of a core on the baseline against 41% with AVX2. Building the whole
// plugin for AVX2 would make it crash on older CPUs instead. Elsewhere this is empty: ARM has
// NEON throughout, and macOS has no ifunc for the loader to choose with. It goes on the
// declarations below and on the definitions in Room.cpp: clang wants it on every declaration.
#if defined(__x86_64__) && defined(__linux__)
#define ROOM_HOT __attribute__((target_clones("arch=x86-64-v3", "default")))
#else
#define ROOM_HOT
#endif

class Room {
public:
    static constexpr int kMaxModes = 16384;
    static constexpr int kMaxBlock = 256;     // processBlock chunks internally to this

    struct Wall {                    // one pair: side walls (x) or front/back (y)
        float absorb    = 0.15f;     // broadband absorption, 0..1
        float bright    = 5.0f;      // HF damping corner, octaves above f0 (8 ~ flat)
        float ring      = 0.0f;      // wall resonator level, 0 = the wall doesn't ring
        int   ringMat   = 3;         // the object's material list: the static tables, then the generators
        float ringRatio = 0.5f;      // wall fundamental / note fundamental
        float ringT60   = 1.5f;      // seconds at note 60
        float ringTilt  = 1.0f;      // the wall bank's DampTilt
        bool  link      = false;     // ring with the ABSORBED energy, not the incident sound

        // Wall effects (tools/roomlab exp5). Flex and Rattle depend on level, against the
        // object's level at full velocity (see kObjectRef in Room.cpp): softer notes, gentler walls.
        float shift  = 0.0f;         // frequency shift, fraction of f0: the near wall up, the far down;
                                     // the wall resonators hear it too
        float flex   = 0.0f;         // the wall moves with the pressure on it: delay swing, periods of f0
        float rattle = 0.0f;         // the wall resonator buzzes past a gap
        float gap    = 0.15f;        // that gap, a share of a full-velocity ring (kRingRef in Room.cpp)
    };

    struct Settings {
        float width = 3.0f, depth = 2.2f;   // wavelengths at note 60
        float roomTrack  = 1.0f;     // 1: room scales with the note; 0: fixed physical size
        float decayTrack = 0.0f;     // 1: wall/air T60s scale as 1/f0; 0: seconds
        float airT60     = 12.0f;    // seconds at note 60; keeps axial modes finite
        float detail     = 12.0f;    // room modes up to this multiple of f0 (the CPU control)
        float srcX = 0.31f, srcY = 0.37f;   // 0..1 of the room
        float micX = 0.62f, micY = 0.71f;
        float micWidth = 1.0f;       // side level in the M/S pair
        float direct = 1.0f, early = 1.0f, late = 1.0f;
        Wall  wall[2];
    };

    Room();

    void setSampleRate(double sr);
    void reset();                          // flush all state (voice allocation)

    // Block-rate. Changed settings are applied at most every ~30 ms, so turning a
    // knob doesn't rebuild the room on every block.
    void setSettings(const Settings& s);

    // Note-on: retune the room to this fundamental in the next processBlock. The
    // room keeps that tuning until the next strike; bends and glides move only
    // the object, as they would in a real room.
    void strike(float f0);

    // in: the object's (mono) sound. May be null (nothing this block).
    void processBlock(const float* in, float* outL, float* outR, int n);

    int activeModes() const { return mCount; }

private:
    void rebuildGeometry();      // mode list, gains, losses: on settings change
    ROOM_HOT void retunePitch();          // coefficients and paths for mFundamental: on strike
    ROOM_HOT void processChunk(const float* in, float* outL, float* outR, int n);

    // --- room modes, structure of arrays, padded to a multiple of 16 --------------
    alignas(32) float mA1[kMaxModes], mA2[kMaxModes], mB[kMaxModes];
    alignas(32) float mGM[kMaxModes], mGS[kMaxModes];      // omni and figure-8 gains
    alignas(32) float mQA[kMaxModes], mQB[kMaxModes];      // quadrature from y[n], y[n-1]
    alignas(32) float mY1[kMaxModes], mY2[kMaxModes];
    // geometry cache: f0-independent per mode
    float mRatio[kMaxModes];     // f / f0
    float mLoss [kMaxModes];     // energy loss rate per unit f0, from the walls
    float mGM0  [kMaxModes], mGS0[kMaxModes];
    int   mIdx  [kMaxModes];     // sort scratch
    int   mCount = 0;           // modes in use (after Nyquist cut), padded
    int   mGeomCount = 0;        // modes in the geometry cache

    // --- early paths: one delay line of the object's sound ------------------------
    static constexpr int kDelaySize = 1 << 17;
    float    mDelay[kDelaySize];
    uint32_t mWrite = 0;

    struct Path {
        float delay = 0;         // samples
        float gM = 0, gS = 0;    // omni / figure-8 gains, spreading included
    };
    Path  mDirect, mLateTap;
    Path  mImage[4];             // 0,1 side walls; 2,3 front/back
    float mLP[4]  = {0, 0, 0, 0};   // each reflection's one-pole state
    float mLPa[2] = {0, 0};         // per pair: one-pole coefficient
    float mReflG[2] = {1, 1};       // per pair: sqrt(1 - absorb)

    ModalBank mWallBank[4];
    float     mRingInG[4]  = {0, 0, 0, 0};
    float     mRingOutM[4] = {0, 0, 0, 0}, mRingOutS[4] = {0, 0, 0, 0};
    bool      mRingOn[2]   = {false, false};
    // A generated wall material, per pair: filled when the pair's material changes.
    ModalMode     mGenModes[2][ModalBank::kMaxModes];
    ModalMaterial mGenMat[2] = { { nullptr, 0, "Gen" }, { nullptr, 0, "Gen" } };
    int           mGenIdx[2] = { -1, -1 };

    // --- wall effects, per path (0,1 side walls; 2,3 front/back) -------------------
    struct Hilbert {                 // two chains of four allpasses, outputs 90 degrees apart
        float x1[2][4], x2[2][4], y1[2][4], y2[2][4];
        float d;                     // the first chain's extra sample of delay
    };
    Hilbert mHil[4];
    float mOscC[4] = {1, 1, 1, 1}, mOscS[4] = {0, 0, 0, 0};   // each path's quadrature oscillator
    float mOscCw[4] = {1, 1, 1, 1}, mOscSw[4] = {0, 0, 0, 0}; // its rotation per sample
    bool  mShiftOn[2] = {false, false};
    float mDisp[4] = {0, 0, 0, 0};   // flex: the wall's displacement
    float mFlexK[2] = {0, 0};        // samples of delay per unit of displacement
    float mFlexA = 0.0f;             // the displacement's one-pole, at f0
    float mRattlePrev[4] = {0, 0, 0, 0};
    float mRattleGap[2] = {0, 0}, mRattleK[2] = {0, 0}, mRattleMax[2] = {0, 0};

    Settings mSettings, mPending;
    bool     mGeomDirty = true, mPitchDirty = true, mSettingsDirty = false;
    uint32_t mSinceRebuild = 0;
    float    mOutGain = 1.0f;          // see retunePitch: referenced to the direct path
    double   mSampleRate = 48000.0;
    float    mFundamental = 261.6256f;
};

#endif /* Room_hpp */
