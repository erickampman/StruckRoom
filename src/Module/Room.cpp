//
//  Room.cpp
//
//  See Room.hpp for the model, and tools/roomlab for the offline renderer this
//  was ported from (its README records what the first experiments showed).
//

#include "Room.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>

// The two hot loops (the strike retune and the mode loop) are built twice on x86-64 Linux, for
// AVX2 + FMA and for the baseline, and the loader picks one per CPU. The baseline has only 16
// 4-wide registers, so the mode loop's 8-wide vectors spill on every sample: measured on an
// i3-9100 at Detail 21, 205% of a core on the baseline against 41% with AVX2. Building the whole
// plugin for AVX2 would make it crash on older CPUs instead. Elsewhere this is empty: ARM has
// NEON throughout, and macOS has no ifunc for the loader to choose with.
#if defined(__x86_64__) && defined(__linux__)
#define ROOM_HOT __attribute__((target_clones("arch=x86-64-v3", "default")))
#else
#define ROOM_HOT
#endif

namespace {

constexpr double kLn1e6  = 13.815510557964274;   // 60 dB, in energy
constexpr double kRefHz  = 261.6255653005986;    // note 60: where "at note 60" values apply
constexpr float  kDenormalFloor = 1e-20f;

// Level calibration, measured in roomlab (tools/roomlab/stats.py): at the default
// scene each path alone comes out near the direct sound's RMS.
constexpr double kLateGain = 0.0021;
constexpr double kRingGain = 0.0079;

constexpr double kDefaultArea = 3.0 * 2.2;        // where kLateGain was measured

// Overall level after the room, so a struck note leaves headroom for chords: the engine's Final
// stage hard-clips at full scale. Measured: StruckSynth alone peaks near 0.95 on a high Bell at full
// velocity, and the room adds 30-50% at the strike; at 0.35 a single note peaks near a third of full
// scale, leaving room for chords (plugin.cpp rounds off what still goes over, see softKnee).
constexpr double kOutputTrim = 0.35;

// Applied settings are rebuilt at most this often while a knob moves.
constexpr double kRebuildSeconds = 0.03;

// Where along the image-to-mic line the wall is. When the source sits on the wall
// its image does too, and 0/0 would follow: the source itself is then where the
// wall is struck (t = 0).
inline double safeRatio(double num, double den) {
    return (std::fabs(den) > 1e-9) ? std::clamp(num / den, 0.0, 1.0) : 0.0;
}

// Branch-free float sin and exp for the retune loop, so it vectorises: the library calls do not, and
// a chord retunes thousands of modes per voice in one block. Each is only as general as its caller.

// sin(x) for |x| <= pi/2: odd Taylor series through x^11, error < 6e-8.
inline float sinHalfPi(float x) {
    const float x2 = x * x;
    return x * (1.0f + x2 * (-1.0f / 6 + x2 * (1.0f / 120 + x2 * (-1.0f / 5040
             + x2 * (1.0f / 362880 + x2 * (-1.0f / 39916800))))));
}

// exp(x) for x <= 0. Near zero (the slow decays, where 1 - r is tiny and its precision IS the
// decay time) a Taylor series keeps full relative accuracy in x; further out, 2^(x log2 e) by
// splitting off the exponent bits, where a part in 10^7 no longer matters.
inline float expNonPositive(float x) {
    const float near = 1.0f + x * (1.0f + x * (0.5f + x * (1.0f / 6 + x * (1.0f / 24 + x * (1.0f / 120)))));
    const float t  = std::max(x * 1.44269504f, -126.0f);
    const float fl = std::floor(t);
    const float f  = t - fl;                                   // 0..1
    const float p  = 1.0f + f * (0.693147f + f * (0.240227f + f * (0.0555041f + f * (0.00961813f + f * 0.00133336f))));
    const int32_t bits = (int32_t)(fl + 127.0f) << 23;
    float scale; std::memcpy(&scale, &bits, sizeof scale);
    return (x > -0.1f) ? near : p * scale;
}

inline double spreadGain(double r) { return 1.0 / std::sqrt(std::max(r, 0.1)); }   // 2-D: 1/sqrt(r)

bool sameWall(const Room::Wall& a, const Room::Wall& b) {
    return a.absorb == b.absorb && a.bright == b.bright && a.ring == b.ring && a.ringMat == b.ringMat
        && a.ringRatio == b.ringRatio && a.ringT60 == b.ringT60 && a.ringTilt == b.ringTilt
        && a.link == b.link;
}

bool sameSettings(const Room::Settings& a, const Room::Settings& b) {
    return a.width == b.width && a.depth == b.depth && a.roomTrack == b.roomTrack
        && a.decayTrack == b.decayTrack && a.airT60 == b.airT60 && a.detail == b.detail
        && a.srcX == b.srcX && a.srcY == b.srcY && a.micX == b.micX && a.micY == b.micY
        && a.micWidth == b.micWidth && a.direct == b.direct && a.early == b.early
        && a.late == b.late && sameWall(a.wall[0], b.wall[0]) && sameWall(a.wall[1], b.wall[1]);
}

// Energy lost per wall hit at f = q * f0: the broadband absorption plus the
// one-pole's high-frequency roll-off, |R|^2 = (1 - absorb) / (1 + (f/fc)^2).
// The reflections use the same one-pole, so the standing waves and the early
// sound agree on how live the room is.
double wallLoss(const Room::Wall& w, double q) {
    const double a  = std::clamp((double)w.absorb, 0.0, 0.999);
    const double fc = std::exp2((double)w.bright);   // corner, in multiples of f0
    return -std::log(1.0 - a) + std::log1p((q / fc) * (q / fc));
}

} // namespace

Room::Room() {
    std::fill_n(mDelay, kDelaySize, 0.0f);
    std::fill_n(mY1, kMaxModes, 0.0f);
    std::fill_n(mY2, kMaxModes, 0.0f);
    for (ModalBank& b : mWallBank) b.setSpread(0.0f);
}

void Room::setSampleRate(double sr) {
    if (sr <= 0.0 || sr == mSampleRate) return;
    mSampleRate = sr;
    for (ModalBank& b : mWallBank) b.setSampleRate(sr);
    reset();
    mPitchDirty = true;
}

void Room::reset() {
    std::fill_n(mDelay, kDelaySize, 0.0f);
    std::fill_n(mY1, kMaxModes, 0.0f);
    std::fill_n(mY2, kMaxModes, 0.0f);
    std::fill_n(mLP, 4, 0.0f);
    for (ModalBank& b : mWallBank) b.reset();
}

void Room::setSettings(const Settings& s) {
    if (sameSettings(s, mPending)) return;
    mPending = s;
    mSettingsDirty = true;
}

void Room::strike(float f0) {
    if (!(f0 > 0.0f)) return;
    // A strike takes pending settings at once: the throttle is for knob moves.
    if (mSettingsDirty) {
        mSettings = mPending;
        mSettingsDirty = false;
        mGeomDirty = true;
    }
    // Room Track below 1 sizes the room in wavelengths by the pitch, so the
    // mode list itself changes with the note.
    if (f0 != mFundamental && mSettings.roomTrack != 1.0f) mGeomDirty = true;
    mFundamental = f0;
    mPitchDirty = true;
}

// ---------------------------------------------------------------------------
// Stage 1: everything that doesn't depend on the pitch (when Room Track is 1).
// ---------------------------------------------------------------------------
void Room::rebuildGeometry() {
    const Settings& s = mSettings;
    const double roomScale = std::pow((double)mFundamental / kRefHz, 1.0 - (double)s.roomTrack);
    const double W = std::max(0.05, (double)s.width * roomScale);
    const double D = std::max(0.05, (double)s.depth * roomScale);

    // Mode count is about pi * W * D * H^2 (a quarter ellipse of (nx, ny) under
    // the harmonic limit H). Lower H rather than truncate an uneven list.
    double H = std::max(0.5, (double)s.detail);
    const double budget = 0.95 * kMaxModes;
    if (M_PI * W * D * H * H > budget) H = std::sqrt(budget / (M_PI * W * D));

    const double areaNorm = 1.0 / std::sqrt(W * D * kDefaultArea);
    const int nxMax = (int)std::floor(2.0 * W * H);
    const int nyMax = (int)std::floor(2.0 * D * H);
    auto psi = [](int n, double frac) { return std::cos(M_PI * n * frac); };

    // Unsorted into scratch (the coefficient arrays, which stage 2 overwrites).
    float* ratio = mA1; float* loss = mA2; float* gm = mB; float* gs = mQA;
    int n = 0;
    for (int nx = 0; nx <= nxMax && n < kMaxModes; ++nx) {
        for (int ny = 0; ny <= nyMax && n < kMaxModes; ++ny) {
            if (nx == 0 && ny == 0) continue;
            const double kx = nx / W, ky = ny / D;
            const double kk = std::hypot(kx, ky);
            const double q  = 0.5 * kk;                 // f / f0
            if (q > H) continue;

            // The standing wave's pressure at the source and the mic: a node
            // (cos = 0) can be neither driven nor heard.
            const double ps = psi(nx, s.srcX) * psi(ny, s.srcY);
            if (std::fabs(ps) < 1e-4) continue;
            const double pm = psi(nx, s.micX) * psi(ny, s.micY);
            const double cx = kx / kk, cy = ky / kk;    // direction cosines of its plane waves
            // Particle velocity along x, times rho*c: -(c/omega) d(psi)/dx.
            const double vx = -cx * std::sin(M_PI * nx * s.micX) * psi(ny, s.micY);

            // Green's-function weight: Lambda * psi_s / area, Lambda normalising
            // the cosines. sqrt(f0/f) rather than the 2-D Green's 1/f keeps the
            // modal field's tilt consistent with the flat direct path. The mode
            // count grows with the area, so 1/area makes the field's ENERGY fall
            // as 1/area: physical, but a tiny room would be deafening. 1/sqrt(area)
            // keeps it constant, so Late means the same at every size (and equals
            // roomlab's 1/area at its default 3 x 2.2 room).
            const double lam = (nx ? 2.0 : 1.0) * (ny ? 2.0 : 1.0);
            const double g   = kLateGain * lam * ps * areaNorm / std::sqrt(q);

            ratio[n] = (float)q;
            // The plane-wave pair making this mode hits the side walls
            // c*cos(theta_x)/W times a second; c = f0 in these units.
            loss[n]  = (float)((cx / W) * wallLoss(s.wall[0], q) + (cy / D) * wallLoss(s.wall[1], q));
            gm[n]    = (float)(g * pm);
            gs[n]    = (float)(g * vx);
            mIdx[n]  = n;
            ++n;
        }
    }

    // Sorted by frequency, so stage 2 can stop at Nyquist.
    std::sort(mIdx, mIdx + n, [&](int a, int b) { return ratio[a] < ratio[b]; });
    for (int i = 0; i < n; ++i) {
        const int j = mIdx[i];
        mRatio[i] = ratio[j];
        mLoss[i]  = loss[j];
        mGM0[i]   = gm[j];
        mGS0[i]   = gs[j];
    }
    mGeomCount = n;
    // A rebuilt list is a different set of modes: the old states would ring at
    // the wrong frequencies with the wrong gains. The object, still sounding,
    // re-excites the room at once.
    std::fill_n(mY1, kMaxModes, 0.0f);
    std::fill_n(mY2, kMaxModes, 0.0f);
    mCount = 0;
}

// ---------------------------------------------------------------------------
// Stage 2: the pitch. Cheap, so it runs on every strike.
// ---------------------------------------------------------------------------
ROOM_HOT void Room::retunePitch() {
    const Settings& s = mSettings;
    const double fs = mSampleRate;
    const double f0 = mFundamental;
    const double decayScale = std::pow(kRefHz / f0, (double)s.decayTrack);
    const double airRate = kLn1e6 / (std::max(0.1, (double)s.airT60) * decayScale);
    const double nyquist = 0.45 * fs;
    // Below this a mode is inaudible rumble, and its quadrature term (divided by
    // sin w) amplifies float rounding: kept in the list, but silent.
    const double subsonic = 20.0;

    // --- modes ---
    // Runs on every strike, for every voice struck, so it is kept lean: float throughout (the
    // coefficients are stored as float, so double bought nothing), and the list is sorted by
    // frequency, so the Nyquist and subsonic limits are index ranges found by binary search.
    const int oldCount = mCount;
    const int n   = (int)(std::upper_bound(mRatio, mRatio + mGeomCount, (float)(nyquist / f0)) - mRatio);
    const int sub = (int)(std::lower_bound(mRatio, mRatio + n, (float)(subsonic / f0)) - mRatio);
    const float w0    = (float)(2.0 * M_PI * f0 / fs);
    const float lossK = (float)(-0.5 * f0 / fs);        // energy rate -> log of the pole radius
    const float airK  = (float)(-0.5 * airRate / fs);
    const float late  = s.late;
    for (int i = 0; i < n; ++i) {
        const float w  = mRatio[i] * w0;                     // 0 .. 0.9 pi (Nyquist cut above)
        const float r  = std::min(expNonPositive(lossK * mLoss[i] + airK), 0.9999995f);
        const float sw = sinHalfPi(std::min(w, (float)M_PI - w));
        const float cw = sinHalfPi((float)M_PI_2 - w);

        // A mode still ringing from the last strike is carried over at the same
        // amplitude and phase. Its two state samples alone would read as a far
        // bigger amplitude at a lower frequency (near DC, a growing ramp). With
        // y1 = A sin(theta) and c = A cos(theta) from the OLD coefficients, the
        // new y2 is A/r' * sin(theta - w').
        if (i < oldCount) {
            const float c = mQA[i] * mY1[i] + mQB[i] * mY2[i];
            mY2[i] = (mY1[i] * cw - c * sw) / r;
        }

        mA1[i] = 2.0f * r * cw;
        mA2[i] = -r * r;
        mB[i]  = sw;
        // For y[n] = A r^n sin(theta): A r^n cos(theta) = (cos w * y[n] - r * y[n-1]) / sin w.
        mQA[i] = cw / sw;
        mQB[i] = -r / sw;
        mGM[i] = late * mGM0[i];
        mGS[i] = late * mGS0[i];
    }
    for (int i = 0; i < sub; ++i) {
        mB[i] = mGM[i] = mGS[i] = 0.0f;
        mY1[i] = mY2[i] = 0.0f;
    }
    const int padded = (n + 15) & ~15;   // processChunk takes modes sixteen at a time
    for (int i = n; i < padded; ++i) {
        mA1[i] = mA2[i] = mB[i] = mQA[i] = mQB[i] = mGM[i] = mGS[i] = 0.0f;
    }
    // Modes entering or leaving the list carry no meaningful state.
    for (int i = std::min(oldCount, n); i < std::max(oldCount, padded); ++i) mY1[i] = mY2[i] = 0.0f;
    mCount = padded;

    // --- early paths ---
    const double roomScale = std::pow(f0 / kRefHz, 1.0 - (double)s.roomTrack);
    const double W = std::max(0.05, (double)s.width * roomScale);
    const double D = std::max(0.05, (double)s.depth * roomScale);
    const double spl = fs / f0;                          // samples per wavelength
    const double maxDelay = kDelaySize - 4;
    struct Pt { double x, y; };
    const Pt S   { s.srcX * W, s.srcY * D };
    const Pt Mic { s.micX * W, s.micY * D };
    auto dist = [](Pt a, Pt b) { return std::hypot(a.x - b.x, a.y - b.y); };

    // A plane wave from `from` reaches the omni as is and the figure-8 times the
    // x component of its direction of travel: sound from the left lands in L.
    auto setPath = [&](Path& p, Pt from, double g) {
        const double r  = dist(from, Mic);
        const double ux = (r > 1e-9) ? (Mic.x - from.x) / r : 0.0;
        p.delay = (float)std::min(r * spl, maxDelay);
        p.gM = (float)(g * spreadGain(r));
        p.gS = (float)(g * spreadGain(r) * ux);
    };
    setPath(mDirect, S, s.direct);
    // Every path's level is relative to the direct sound's spreading: dividing the whole output by
    // it keeps the loudness put as the object and listener move, while the balance of direct, early
    // and late still changes with distance the way it would in a real room.
    mOutGain = (float)(std::sqrt(std::max(dist(S, Mic), 0.1)) * kOutputTrim);
    mLateTap.delay = mDirect.delay;    // the modes start when the sound arrives

    for (int pair = 0; pair < 2; ++pair) {
        const Wall& w = s.wall[pair];
        mReflG[pair] = (float)std::sqrt(1.0 - std::clamp((double)w.absorb, 0.0, 0.999));
        const double fc = f0 * std::exp2((double)w.bright);
        mLPa[pair] = (fc >= nyquist) ? 0.0f : (float)std::exp(-2.0 * M_PI * fc / fs);
        mRingOn[pair] = w.ring > 0.0f;
    }

    const Pt images[4] = { { -S.x, S.y }, { 2.0 * W - S.x, S.y }, { S.x, -S.y }, { S.x, 2.0 * D - S.y } };
    for (int k = 0; k < 4; ++k) {
        const int pair = k / 2;
        const Wall& w = s.wall[pair];
        setPath(mImage[k], images[k], s.early * mReflG[pair]);

        if (!mRingOn[pair]) continue;
        // Where the path to the mic meets the wall: where this wall is struck.
        // That point lies on the image path, so the ring's total delay (source
        // to wall to mic) is the image path's, and the tap is shared.
        const Pt I = images[k];
        Pt R;
        double along;
        if (pair == 0) {
            const double wx = (I.x < 0.0) ? 0.0 : W;
            const double t  = safeRatio(wx - I.x, Mic.x - I.x);
            R = { wx, I.y + t * (Mic.y - I.y) };
            along = R.y / D;
        } else {
            const double wy = (I.y < 0.0) ? 0.0 : D;
            const double t  = safeRatio(wy - I.y, Mic.y - I.y);
            R = { I.x + t * (Mic.x - I.x), wy };
            along = R.x / W;
        }
        ModalBank& bank = mWallBank[k];
        // A ringing bank retuned far down would read its state as a huge
        // amplitude (see the modes above); ModalBank has no carry-over, so a
        // wall whose pitch changes starts clean.
        const float wallHz = (float)(f0 * w.ringRatio);
        if (std::fabs(bank.fundamental() - wallHz) > 1e-3f * wallHz) bank.reset();
        bank.setMaterial(kModalMaterials[std::clamp(w.ringMat, 0, (int)ModalMaterialCount - 1)]);
        bank.setFundamental(wallHz);
        bank.setDecayT60((float)(w.ringT60 * decayScale));
        bank.setDampTilt(w.ringTilt);
        bank.setStrikePosition((float)std::clamp(along, 0.0, 1.0));

        mRingInG[k] = (float)spreadGain(dist(S, R));
        const double rOut = dist(R, Mic);
        const double ux   = (rOut > 1e-9) ? (Mic.x - R.x) / rOut : 0.0;
        mRingOutM[k] = (float)(kRingGain * w.ring * spreadGain(rOut));
        mRingOutS[k] = (float)(kRingGain * w.ring * spreadGain(rOut) * ux);
    }
}

// ---------------------------------------------------------------------------
// Audio
// ---------------------------------------------------------------------------
void Room::processBlock(const float* in, float* outL, float* outR, int n) {
    if (mSettingsDirty && mSinceRebuild >= (uint32_t)(kRebuildSeconds * mSampleRate)) {
        mSettings = mPending;
        mSettingsDirty = false;
        mGeomDirty = true;
    }
    if (mGeomDirty) {
        rebuildGeometry();
        mGeomDirty = false;
        mPitchDirty = true;
        mSinceRebuild = 0;
    }
    if (mPitchDirty) {
        retunePitch();
        mPitchDirty = false;
    }
    mSinceRebuild += (uint32_t)n;

    for (int done = 0; done < n; done += kMaxBlock) {
        const int len = std::min(kMaxBlock, n - done);
        processChunk(in ? in + done : nullptr, outL + done, outR + done, len);
    }
}

ROOM_HOT void Room::processChunk(const float* in, float* outL, float* outR, int n) {
    constexpr uint32_t mask = kDelaySize - 1;
    float mid[kMaxBlock], side[kMaxBlock], late[kMaxBlock];
    float ringIn[4][kMaxBlock];

    auto readAt = [&](float delay) {
        const double pos = (double)mWrite - (double)delay;
        const double fl  = std::floor(pos);
        const float  fr  = (float)(pos - fl);
        const uint32_t i = (uint32_t)(int64_t)fl;
        return mDelay[i & mask] * (1.0f - fr) + mDelay[(i + 1) & mask] * fr;
    };

    const bool anyRing = mRingOn[0] || mRingOn[1];
    for (int s = 0; s < n; ++s) {
        mDelay[mWrite & mask] = in ? in[s] : 0.0f;

        const float d = readAt(mDirect.delay);
        float m = mDirect.gM * d;
        float sd = mDirect.gS * d;
        late[s] = readAt(mLateTap.delay);

        for (int k = 0; k < 4; ++k) {
            const int pair = k >> 1;
            const float v = readAt(mImage[k].delay);
            const float a = mLPa[pair];
            mLP[k] = a * mLP[k] + (1.0f - a) * v;
            const float lp = mLP[k];
            m  += mImage[k].gM * lp;
            sd += mImage[k].gS * lp;
            if (mRingOn[pair]) {
                // Linked: the wall rings with what it absorbed, the broadband part
                // sqrt(absorb) of the low band plus everything the one-pole took.
                const float drive = mSettings.wall[pair].link
                    ? std::sqrt(std::clamp(mSettings.wall[pair].absorb, 0.0f, 1.0f)) * lp + (v - lp)
                    : v;
                ringIn[k][s] = mRingInG[k] * drive;
            }
        }
        mid[s] = m;
        side[s] = sd;
        ++mWrite;
    }

    // --- wall resonators ---
    if (anyRing) {
        float bl[kMaxBlock], br[kMaxBlock];
        for (int k = 0; k < 4; ++k) {
            if (!mRingOn[k >> 1]) continue;
            mWallBank[k].processBlock(ringIn[k], bl, br, n);
            for (int s = 0; s < n; ++s) {
                const float y = bl[s] * (float)M_SQRT2;   // spread 0: undo the pan law
                mid[s]  += mRingOutM[k] * y;
                side[s] += mRingOutS[k] * y;
            }
        }
    }

    // --- room modes ---
    // Register-blocked: eight modes at a time stay in vector registers for the whole chunk, and
    // their outputs accumulate into per-sample vectors that live in L1. Walking every mode's nine
    // arrays once per SAMPLE (the obvious loop) streamed ~36 bytes per mode per sample from cache
    // and memory; this touches them once per chunk. The 8 lanes are summed once per sample at the
    // end, not once per mode. Same arithmetic, same results; several times faster at high Detail.
    const int modes = mCount;   // a multiple of 16 (retunePitch pads it)
    if (modes > 0) {
        typedef float v8 __attribute__((vector_size(32)));
        alignas(32) v8 accM[kMaxBlock], accS[kMaxBlock];
        const v8 zero = {};
        for (int s = 0; s < n; ++s) { accM[s] = zero; accS[s] = zero; }

        auto load = [](const float* p) { v8 v; std::memcpy(&v, p, sizeof v); return v; };
        auto flush = [](v8& v) {   // denormals: a long tail spends most of its life near zero
            for (int k = 0; k < 8; ++k) if (std::fabs(v[k]) < kDenormalFloor) v[k] = 0.0f;
        };
        // Two groups of eight in flight: each group's recursion waits on its previous sample, so
        // a second, independent group fills the pipeline. Written (b*x + a2*y2) + a1*y1 so only the
        // last multiply-add depends on the previous output. mCount is padded to 16 for this.
        for (int i = 0; i < modes; i += 16) {
            const v8 a1 = load(mA1 + i), a2 = load(mA2 + i), b = load(mB + i);
            const v8 c1 = load(mA1 + i + 8), c2 = load(mA2 + i + 8), d = load(mB + i + 8);
            v8 y1 = load(mY1 + i), y2 = load(mY2 + i);
            v8 z1 = load(mY1 + i + 8), z2 = load(mY2 + i + 8);
            const v8 gm = load(mGM + i), gs = load(mGS + i), qa = load(mQA + i), qb = load(mQB + i);
            const v8 hm = load(mGM + i + 8), hs = load(mGS + i + 8), ra = load(mQA + i + 8), rb = load(mQB + i + 8);
            for (int s = 0; s < n; ++s) {
                const float x = late[s];
                const v8 y = (b * x + a2 * y2) + a1 * y1;
                const v8 z = (d * x + c2 * z2) + c1 * z1;
                accM[s] += gm * y + hm * z;
                accS[s] += gs * (qa * y + qb * y1) + hs * (ra * z + rb * z1);
                y2 = y1; y1 = y;
                z2 = z1; z1 = z;
            }
            flush(y1); flush(y2); flush(z1); flush(z2);
            std::memcpy(mY1 + i, &y1, sizeof y1);
            std::memcpy(mY2 + i, &y2, sizeof y2);
            std::memcpy(mY1 + i + 8, &z1, sizeof z1);
            std::memcpy(mY2 + i + 8, &z2, sizeof z2);
        }
        for (int s = 0; s < n; ++s) {
            const v8 m = accM[s], sd = accS[s];
            mid[s]  += ((m[0] + m[1]) + (m[2] + m[3])) + ((m[4] + m[5]) + (m[6] + m[7]));
            side[s] += ((sd[0] + sd[1]) + (sd[2] + sd[3])) + ((sd[4] + sd[5]) + (sd[6] + sd[7]));
        }
    }

    // Equal-power M/S decode: a centred source keeps its level at any width, and width 0 is mono
    // at unity. (M + S undecoded put a source at one side at twice the level in that channel.)
    const float width = mSettings.micWidth;
    const float g = mOutGain / std::sqrt(1.0f + width * width);
    const float gs = g * width;
    for (int s = 0; s < n; ++s) {
        outL[s] = g * mid[s] + gs * side[s];
        outR[s] = g * mid[s] - gs * side[s];
    }
}
