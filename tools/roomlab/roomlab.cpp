//
//  roomlab.cpp
//
//  Offline renderer for the StruckRoom experiments: one struck note, played
//  through a 2-D rectangular "room" whose every length scales with the note.
//
//    strike -> object (StruckSynth's ModalBank, unchanged)
//           -> direct path                    (delay, 1/sqrt(r))
//           -> 4 first-order wall reflections (delay, wall absorption filter)
//           -> 4 wall resonators              (ModalBank per wall, rung by the
//                                              sound reaching the wall)
//           -> room modes                     (standing waves (nx, ny), gains
//                                              from source and mic positions,
//                                              T60s from wall absorption)
//
//  The listener is a coincident Mid/Side pair: an omni (pressure) and a
//  figure-8 facing +x (particle velocity, scaled by rho*c). Coincident, so it
//  has no length in it and stays the same at every pitch; a spaced pair would
//  sit a different number of wavelengths apart on every note.
//  L = M + width*S, R = M - width*S.
//
//  Units: lengths are in WAVELENGTHS OF THE NOTE (lambda0 = c / f0), so c = f0
//  lambda/s and a path of r wavelengths takes r / f0 seconds. That is what
//  makes the room follow the pitch: the same numbers at any note describe the
//  same scene, transposed.
//
//  Usage: roomlab out.wav [key=value ...]   (roomlab --help lists the keys)
//

#include "ModalBank.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <thread>
#include <vector>

namespace {

constexpr double kLn1e6 = 13.815510557964274;   // 60 dB in energy
constexpr double kRefHz = 261.6255653005986;    // note 60: where *.ref values are given

// Level calibration, by measurement (tools/stats.py), so that at the default
// scene each path alone comes out near the direct sound's RMS. Physical
// constants (c^2, the source strength) would only move these around.
constexpr double kLateGain = 0.0021;
constexpr double kRingGain = 0.0079;

// ---------------------------------------------------------------------------
// Parameters. Everything is key=value on the command line; the defaults are a
// medium room with mildly absorbent, non-ringing walls.
// ---------------------------------------------------------------------------
struct Wall {
    double absorb     = 0.15;   // absorption coefficient at f0, 0..1
    double absorbTilt = 0.5;    // alpha(f) = absorb * (f/f0)^tilt: >0 soaks up highs
    double ring       = 0.0;    // wall resonator level; 0 = the wall doesn't ring
    std::string ringMat = "membrane";
    double ringRatio  = 0.5;    // wall fundamental / note fundamental
    double ringT60    = 1.5;    // seconds at note 60 (see decay.track)
    double ringTilt   = 1.0;    // the wall bank's DampTilt
    bool   link       = false;  // ring with the ABSORBED energy, sqrt(alpha(f))
};

struct Params {
    double note    = 60.0;
    double fs      = 48000.0;
    double seconds = 4.0;

    // object
    std::string mat = "bar";
    double objT60   = 2.0;      // seconds at note 60
    double objTilt  = 0.6;
    double objPos   = 0.23;     // strike position on the object
    double objBright = 0.0;
    double hardness = 0.15;     // contact length, 0..1 of 1.6 periods

    // room, in wavelengths of the note at note 60 (see room.track)
    double roomW = 3.0, roomD = 2.2;
    double roomTrack  = 1.0;    // 1: room scales with the note; 0: fixed physical room
    double decayTrack = 0.0;    // 1: object/wall/air T60s scale as 1/f0 too; 0: seconds, as in StruckSynth
    double airT60     = 12.0;   // seconds at note 60: keeps axial modes finite
    double roomMaxHarm = 32.0;  // room modes up to this multiple of f0 (pitch-relative cap)
    int    roomMaxModes = 250000;

    // placement, 0..1 of the room's width / depth
    double srcX = 0.31, srcY = 0.37;
    double micX = 0.62, micY = 0.71;
    double micWidth = 1.0;      // side (figure-8) level in the M/S pair; 0 = mono

    Wall wall[2];               // 0 = side walls (x = 0, W), 1 = front/back (y = 0, D)

    // mix
    double direct = 1.0, early = 1.0, late = 1.0, ringMix = 1.0;
};

const ModalMaterial* findMaterial(const std::string& name) {
    auto squash = [](std::string s) {
        std::string o;
        for (char c : s) if (c != ' ' && c != '_' && c != '-') o += (char)std::tolower((unsigned char)c);
        return o;
    };
    const std::string want = squash(name);
    for (int i = 0; i < ModalMaterialCount; ++i)
        if (squash(kModalMaterials[i].name) == want) return &kModalMaterials[i];
    return nullptr;
}

bool setParam(Params& p, const std::string& k, const std::string& v) {
    const double d = std::atof(v.c_str());
    std::map<std::string, double*> num = {
        {"note", &p.note}, {"fs", &p.fs}, {"seconds", &p.seconds},
        {"obj.t60", &p.objT60}, {"obj.tilt", &p.objTilt}, {"obj.pos", &p.objPos},
        {"obj.bright", &p.objBright}, {"obj.hardness", &p.hardness},
        {"room.w", &p.roomW}, {"room.d", &p.roomD}, {"room.track", &p.roomTrack},
        {"decay.track", &p.decayTrack}, {"room.air", &p.airT60},
        {"room.maxharm", &p.roomMaxHarm},
        {"src.x", &p.srcX}, {"src.y", &p.srcY}, {"mic.x", &p.micX}, {"mic.y", &p.micY},
        {"mic.width", &p.micWidth},
        {"mix.direct", &p.direct}, {"mix.early", &p.early}, {"mix.late", &p.late},
        {"mix.ring", &p.ringMix},
    };
    if (auto it = num.find(k); it != num.end()) { *it->second = d; return true; }
    if (k == "obj.mat") { p.mat = v; return true; }
    if (k == "room.maxmodes") { p.roomMaxModes = std::atoi(v.c_str()); return true; }

    // wall keys: side.* , fb.* , or walls.* for both pairs
    const auto dot = k.find('.');
    if (dot == std::string::npos) return false;
    const std::string pre = k.substr(0, dot), key = k.substr(dot + 1);
    std::vector<Wall*> ws;
    if (pre == "side" || pre == "walls") ws.push_back(&p.wall[0]);
    if (pre == "fb"   || pre == "walls") ws.push_back(&p.wall[1]);
    if (ws.empty()) return false;
    for (Wall* w : ws) {
        if      (key == "absorb")    w->absorb = d;
        else if (key == "tilt")      w->absorbTilt = d;
        else if (key == "ring")      w->ring = d;
        else if (key == "ring.mat")  w->ringMat = v;
        else if (key == "ring.ratio") w->ringRatio = d;
        else if (key == "ring.t60")  w->ringT60 = d;
        else if (key == "ring.tilt") w->ringTilt = d;
        else if (key == "link")      w->link = d != 0.0;
        else return false;
    }
    return true;
}

void usage() {
    std::puts(
        "roomlab out.wav [key=value ...]\n"
        "  note seconds fs\n"
        "  obj.mat obj.t60 obj.tilt obj.pos obj.bright obj.hardness\n"
        "  room.w room.d (wavelengths at note 60) room.track decay.track room.air\n"
        "  room.maxharm room.maxmodes\n"
        "  src.x src.y mic.x mic.y (0..1 of the room) mic.width (M/S side level)\n"
        "  side.* fb.* walls.*: absorb tilt ring ring.mat ring.ratio ring.t60 ring.tilt link\n"
        "  mix.direct mix.early mix.late mix.ring\n"
        "Materials: string bar tunedbar membrane opentube closedtube tine bell\n"
        "           guitarbody violinbody soundboard genericbody");
}

// ---------------------------------------------------------------------------
// Signal helpers. Offline, so "delay" is just adding a shifted copy.
// ---------------------------------------------------------------------------

// out[n] += g * x[n - delay], linear interpolation for the fractional part.
void addDelayed(std::vector<float>& out, const std::vector<float>& x, double delay, double g) {
    if (delay < 0.0) delay = 0.0;
    const long   di = (long)std::floor(delay);
    const double fr = delay - (double)di;
    const long   n  = (long)out.size();
    for (long i = di; i < n; ++i) {
        const long j = i - di;
        const double a = (j < (long)x.size()) ? x[j] : 0.0;
        const double b = (j >= 1 && j - 1 < (long)x.size()) ? x[j - 1] : 0.0;
        out[i] += (float)(g * ((1.0 - fr) * a + fr * b));
    }
}

// Linear-phase FIR with magnitude |H(f)| = mag(f), by frequency sampling plus a
// Hann window. Its (taps-1)/2 latency is reported so callers can subtract it.
std::vector<float> designFir(int taps, double fs, auto mag) {
    const int N = taps;   // odd
    const int M = (N - 1) / 2;
    std::vector<float> h(N);
    for (int n = 0; n < N; ++n) {
        double acc = 0.0;
        // Type-I linear phase: h[n] = (1/N) * sum_k A(k) cos(2 pi k (n - M) / N)
        for (int k = 0; k < N; ++k) {
            const int kk = (k <= N / 2) ? k : N - k;
            const double f = fs * (double)kk / (double)N;
            acc += mag(f) * std::cos(2.0 * M_PI * (double)k * (double)(n - M) / (double)N);
        }
        const double w = 0.5 - 0.5 * std::cos(2.0 * M_PI * (double)n / (double)(N - 1));
        h[n] = (float)(acc / (double)N * w);
    }
    return h;
}

std::vector<float> convolve(const std::vector<float>& x, const std::vector<float>& h) {
    std::vector<float> y(x.size(), 0.0f);
    const size_t H = h.size();
    for (size_t i = 0; i < x.size(); ++i) {
        double acc = 0.0;
        const size_t kmax = std::min(H, i + 1);
        for (size_t k = 0; k < kmax; ++k) acc += (double)h[k] * (double)x[i - k];
        y[i] = (float)acc;
    }
    return y;
}

std::vector<float> runBank(ModalBank& bank, const std::vector<float>& x) {
    std::vector<float> L(x.size()), R(x.size());
    const int B = 512;
    for (size_t i = 0; i < x.size(); i += B) {
        const int n = (int)std::min<size_t>(B, x.size() - i);
        bank.processBlock(x.data() + i, L.data() + i, R.data() + i, n);
    }
    // Spread 0: L = R = y * cos(pi/4). Undo the pan law to get y.
    for (size_t i = 0; i < x.size(); ++i) L[i] *= (float)M_SQRT2;
    return L;
}

// ---------------------------------------------------------------------------
// Room modes. Not a ModalBank: that class energy-normalises its 64 modes and
// applies its own strike-position law, while a room needs signed gains from
// the mode shapes and a T60 per mode from the walls. Same two-pole recursion.
// ---------------------------------------------------------------------------
struct RoomModes {
    std::vector<float> a1, a2, b, gM, gS;
    // A mode's velocity is in quadrature with its pressure. For the resonator's
    // y[n] = A r^n sin(theta), A r^n cos(theta) = qa*y[n] + qb*y[n-1].
    std::vector<float> qa, qb;
    std::vector<double> freq, t60;
};

void renderRoomModes(const RoomModes& m, const std::vector<float>& x,
                     std::vector<float>& outL, std::vector<float>& outR) {
    const size_t modes = m.a1.size(), n = x.size();
    const unsigned threads = std::max(1u, std::min(16u, std::thread::hardware_concurrency()));
    std::vector<std::vector<float>> accL(threads, std::vector<float>(n, 0.0f));
    std::vector<std::vector<float>> accR(threads, std::vector<float>(n, 0.0f));
    std::vector<std::thread> pool;

    for (unsigned t = 0; t < threads; ++t) {
        pool.emplace_back([&, t] {
            const size_t lo = modes * t / threads, hi = modes * (t + 1) / threads;
            const size_t cnt = hi - lo;
            if (cnt == 0) return;
            std::vector<float> y1(cnt, 0.0f), y2(cnt, 0.0f);
            const float* __restrict a1 = m.a1.data() + lo;
            const float* __restrict a2 = m.a2.data() + lo;
            const float* __restrict b  = m.b.data()  + lo;
            const float* __restrict gm = m.gM.data() + lo;
            const float* __restrict gs = m.gS.data() + lo;
            const float* __restrict qa = m.qa.data() + lo;
            const float* __restrict qb = m.qb.data() + lo;
            float* __restrict Y1 = y1.data();
            float* __restrict Y2 = y2.data();
            float* oL = accL[t].data();
            float* oR = accR[t].data();
            for (size_t s = 0; s < n; ++s) {
                const float xs = x[s];
                float sL = 0.0f, sR = 0.0f;   // M and S
                for (size_t i = 0; i < cnt; ++i) {
                    const float y = b[i] * xs + a1[i] * Y1[i] + a2[i] * Y2[i];
                    sL += gm[i] * y;
                    sR += gs[i] * (qa[i] * y + qb[i] * Y1[i]);
                    Y2[i] = Y1[i];
                    Y1[i] = y;
                }
                oL[s] = sL;
                oR[s] = sR;
                // Denormal guard, every 4096 samples.
                if ((s & 4095) == 4095)
                    for (size_t i = 0; i < cnt; ++i) {
                        if (std::fabs(Y1[i]) < 1e-20f) Y1[i] = 0.0f;
                        if (std::fabs(Y2[i]) < 1e-20f) Y2[i] = 0.0f;
                    }
            }
        });
    }
    for (auto& th : pool) th.join();
    for (unsigned t = 0; t < threads; ++t)
        for (size_t s = 0; s < n; ++s) { outL[s] += accL[t][s]; outR[s] += accR[t][s]; }
}

bool writeWavFloat(const char* path, const std::vector<float>& L, const std::vector<float>& R, int fs) {
    FILE* f = std::fopen(path, "wb");
    if (!f) return false;
    const uint32_t frames = (uint32_t)L.size(), ch = 2, bytes = frames * ch * 4;
    auto u32 = [&](uint32_t v) { std::fwrite(&v, 4, 1, f); };
    auto u16 = [&](uint16_t v) { std::fwrite(&v, 2, 1, f); };
    std::fwrite("RIFF", 1, 4, f); u32(36 + bytes); std::fwrite("WAVE", 1, 4, f);
    std::fwrite("fmt ", 1, 4, f); u32(16); u16(3); u16((uint16_t)ch); u32((uint32_t)fs);
    u32((uint32_t)fs * ch * 4); u16((uint16_t)(ch * 4)); u16(32);
    std::fwrite("data", 1, 4, f); u32(bytes);
    for (uint32_t i = 0; i < frames; ++i) { std::fwrite(&L[i], 4, 1, f); std::fwrite(&R[i], 4, 1, f); }
    std::fclose(f);
    return true;
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2 || !std::strcmp(argv[1], "--help")) { usage(); return argc < 2 ? 1 : 0; }
    Params p;
    for (int i = 2; i < argc; ++i) {
        const std::string a = argv[i];
        const auto eq = a.find('=');
        if (eq == std::string::npos || !setParam(p, a.substr(0, eq), a.substr(eq + 1))) {
            std::fprintf(stderr, "unknown argument: %s\n", a.c_str());
            usage();
            return 1;
        }
    }

    const double fs = p.fs;
    const double f0 = 440.0 * std::exp2((p.note - 69.0) / 12.0);
    const size_t N  = (size_t)(p.seconds * fs);
    const double decayScale = std::pow(kRefHz / f0, p.decayTrack);   // T60 multiplier
    // Room size in wavelengths. track 1: constant (room scales with the note);
    // track 0: fixed physical size, so more wavelengths fit as the pitch rises.
    const double roomScale = std::pow(f0 / kRefHz, 1.0 - p.roomTrack);
    const double W = p.roomW * roomScale, D = p.roomD * roomScale;
    const double samplesPerLambda = fs / f0;   // c = f0 lambda/s

    const ModalMaterial* objMat = findMaterial(p.mat);
    if (!objMat) { std::fprintf(stderr, "unknown material %s\n", p.mat.c_str()); return 1; }

    // --- strike: raised-cosine contact, as StrikeModule's note-relative mode ---
    std::vector<float> strike(N, 0.0f);
    {
        const int len = std::max(1, (int)std::lround(p.hardness * 1.6 * fs / f0));
        double sum = 0.0;
        for (int i = 0; i < len && i < (int)N; ++i) {
            const double w = (len == 1) ? 1.0 : 0.5 - 0.5 * std::cos(2.0 * M_PI * (i + 0.5) / len);
            strike[i] = (float)w;
            sum += w;
        }
        for (int i = 0; i < len && i < (int)N; ++i) strike[i] = (float)(strike[i] / sum);
    }

    // --- object ---
    ModalBank obj;
    obj.setSampleRate(fs);
    obj.setMaterial(*objMat);
    obj.setFundamental((float)f0);
    obj.setDecayT60((float)(p.objT60 * decayScale));
    obj.setDampTilt((float)p.objTilt);
    obj.setStrikePosition((float)p.objPos);
    obj.setBrightness((float)p.objBright);
    obj.setSpread(0.0f);
    const std::vector<float> y = runBank(obj, strike);

    // --- geometry ---
    struct Pt { double x, y; };
    const Pt S { p.srcX * W, p.srcY * D };
    const Pt Mic { p.micX * W, p.micY * D };
    auto dist = [](Pt a, Pt b) { return std::hypot(a.x - b.x, a.y - b.y); };
    auto spread = [](double r) { return 1.0 / std::sqrt(std::max(r, 0.1)); };   // 2-D: 1/sqrt(r)

    std::vector<float> mid(N, 0.0f), side(N, 0.0f);

    // A plane wave arriving from `from` reaches the omni as is and the
    // figure-8 times the x component of its direction of travel: sound from
    // the left (travelling +x) lands in L.
    auto arrive = [&](const std::vector<float>& sig, Pt from, double extraDelay, double g) {
        const double r  = dist(from, Mic);
        const double ux = (r > 1e-9) ? (Mic.x - from.x) / r : 0.0;
        addDelayed(mid,  sig, r * samplesPerLambda + extraDelay, g * spread(r));
        addDelayed(side, sig, r * samplesPerLambda + extraDelay, g * spread(r) * ux);
    };

    // --- direct ---
    arrive(y, S, 0.0, p.direct);

    // --- walls: absorption, first-order reflections, resonators ---
    auto alpha = [&](const Wall& w, double f) {
        const double a = w.absorb * std::pow(std::max(f, 1.0) / f0, w.absorbTilt);
        return std::clamp(a, 0.0, 0.999);
    };
    const int taps = 511;
    const double firLatency = (taps - 1) / 2.0;

    struct Image { int pair; Pt img; };
    const Image images[4] = {
        { 0, { -S.x, S.y } }, { 0, { 2.0 * W - S.x, S.y } },
        { 1, { S.x, -S.y } }, { 1, { S.x, 2.0 * D - S.y } },
    };

    for (int pair = 0; pair < 2; ++pair) {
        const Wall& w = p.wall[pair];
        const auto hRefl = designFir(taps, fs, [&](double f) { return std::sqrt(1.0 - alpha(w, f)); });
        const std::vector<float> yRefl = convolve(y, hRefl);
        std::vector<float> yDrive;
        if (w.ring > 0.0) {
            if (w.link) {
                const auto hAbs = designFir(taps, fs, [&](double f) { return std::sqrt(alpha(w, f)); });
                yDrive = convolve(y, hAbs);
            } else {
                yDrive = y;
            }
        }

        for (const Image& im : images) {
            if (im.pair != pair) continue;

            // Early reflection: the image source, seen through the wall's filter.
            arrive(yRefl, im.img, -firLatency, p.early);

            if (w.ring <= 0.0) continue;

            // Where the path to the (middle) mic meets the wall: that is where
            // this wall is "struck", and it sets the wall bank's strike position.
            Pt R;
            double along;
            if (pair == 0) {
                const double wx = (im.img.x < 0.0) ? 0.0 : W;
                const double t  = (wx - im.img.x) / (Mic.x - im.img.x);
                R = { wx, im.img.y + t * (Mic.y - im.img.y) };
                along = R.y / D;
            } else {
                const double wy = (im.img.y < 0.0) ? 0.0 : D;
                const double t  = (wy - im.img.y) / (Mic.y - im.img.y);
                R = { im.img.x + t * (Mic.x - im.img.x), wy };
                along = R.x / W;
            }
            const ModalMaterial* wm = findMaterial(w.ringMat);
            if (!wm) { std::fprintf(stderr, "unknown material %s\n", w.ringMat.c_str()); return 1; }
            ModalBank bank;
            bank.setSampleRate(fs);
            bank.setMaterial(*wm);
            bank.setFundamental((float)(f0 * w.ringRatio));
            bank.setDecayT60((float)(w.ringT60 * decayScale));
            bank.setDampTilt((float)w.ringTilt);
            bank.setStrikePosition((float)std::clamp(along, 0.0, 1.0));
            bank.setSpread(0.0f);

            // The bank is LTI, so the trip to the wall can be applied to its input.
            std::vector<float> in(N, 0.0f);
            const double rIn = dist(S, R);
            addDelayed(in, yDrive, rIn * samplesPerLambda - (w.link ? firLatency : 0.0), spread(rIn));
            const std::vector<float> ring = runBank(bank, in);
            arrive(ring, R, 0.0, kRingGain * p.ringMix * w.ring);
        }
    }

    // --- room modes (the standing waves) ---
    size_t modeCount = 0;
    if (p.late > 0.0) {
        RoomModes m;
        const double fmax = std::min(p.roomMaxHarm * f0, 0.45 * fs);
        const int nxMax = (int)std::floor(2.0 * W * fmax / f0);
        const int nyMax = (int)std::floor(2.0 * D * fmax / f0);
        const double airRate = kLn1e6 / (p.airT60 * decayScale);   // energy decay, 1/s
        auto psi = [](int n, double frac) { return std::cos(M_PI * n * frac); };

        for (int nx = 0; nx <= nxMax; ++nx) {
            for (int ny = 0; ny <= nyMax; ++ny) {
                if (nx == 0 && ny == 0) continue;
                const double kx = nx / W, ky = ny / D;              // cycles per wavelength-unit / 2
                const double kk = std::hypot(kx, ky);
                const double f  = 0.5 * f0 * kk;                     // f = c/2 * |k|
                if (f > fmax) continue;

                // Mode shape at the source and at each mic: the standing wave's
                // pressure there. A node (cos = 0) can't be driven or heard.
                const double ps = psi(nx, p.srcX) * psi(ny, p.srcY);
                const double pm = psi(nx, p.micX) * psi(ny, p.micY);
                if (std::fabs(ps) < 1e-4) continue;

                // Wall losses: the plane-wave pair making this mode hits the
                // side walls c*cos(theta_x)/W times a second, losing alpha each time.
                const double cx = kx / kk, cy = ky / kk;
                const double rate = (f0 * cx / W) * -std::log(1.0 - alpha(p.wall[0], f))
                                  + (f0 * cy / D) * -std::log(1.0 - alpha(p.wall[1], f))
                                  + airRate;
                const double t60 = kLn1e6 / rate;
                const double r   = std::min(std::exp(-6.907755278982137 / (t60 * fs)), 0.9999995);
                const double w   = 2.0 * M_PI * f / fs;

                // Green's-function weight: Lambda * psi_s * psi_m / area, with
                // Lambda normalising the cosines (1 for n = 0, 2 otherwise).
                // The 2-D Green's function falls as 1/omega per mode; with mode
                // density rising as f that is 1/sqrt(f) overall, the same tilt
                // a 2-D free field puts on the direct sound. The direct path is
                // left flat (it is the object's own tone), so the modes get
                // sqrt(f0/f) rather than f0/f to stay consistent with it.
                const double lam = (nx ? 2.0 : 1.0) * (ny ? 2.0 : 1.0);
                const double g   = kLateGain * lam * ps / (W * D) * std::sqrt(f0 / f);

                m.a1.push_back((float)(2.0 * r * std::cos(w)));
                m.a2.push_back((float)(-r * r));
                m.b.push_back((float)std::sin(w));
                // Velocity along x, times rho*c: -(c/omega) d(psi)/dx, which in
                // these units is -cos(theta_x) * sin(nx pi X) * cos(ny pi Y),
                // carried by the quadrature of the pressure oscillation.
                const double vx = -cx * std::sin(M_PI * nx * p.micX) * psi(ny, p.micY);
                m.gM.push_back((float)(p.late * g * pm));
                m.gS.push_back((float)(p.late * g * vx));
                m.qa.push_back((float)(std::cos(w) / std::sin(w)));
                m.qb.push_back((float)(-r / std::sin(w)));
                m.freq.push_back(f);
                m.t60.push_back(t60);
            }
        }
        // Over budget: keep the lowest modes.
        if ((int)m.a1.size() > p.roomMaxModes) {
            std::vector<size_t> idx(m.a1.size());
            for (size_t i = 0; i < idx.size(); ++i) idx[i] = i;
            std::nth_element(idx.begin(), idx.begin() + p.roomMaxModes, idx.end(),
                             [&](size_t a, size_t b) { return m.freq[a] < m.freq[b]; });
            idx.resize(p.roomMaxModes);
            RoomModes k;
            for (size_t i : idx) {
                k.a1.push_back(m.a1[i]); k.a2.push_back(m.a2[i]); k.b.push_back(m.b[i]);
                k.gM.push_back(m.gM[i]); k.gS.push_back(m.gS[i]);
                k.qa.push_back(m.qa[i]); k.qb.push_back(m.qb[i]);
                k.freq.push_back(m.freq[i]); k.t60.push_back(m.t60[i]);
            }
            m = std::move(k);
        }
        modeCount = m.a1.size();

        // The modes are excited when the object's sound arrives, not before:
        // drive them with the object delayed by the direct path.
        std::vector<float> drive(N, 0.0f);
        addDelayed(drive, y, dist(S, Mic) * samplesPerLambda, 1.0);
        renderRoomModes(m, drive, mid, side);

        double tMin = 1e9, tMax = 0.0;
        for (double t : m.t60) { tMin = std::min(tMin, t); tMax = std::max(tMax, t); }
        std::fprintf(stderr, "room %.2f x %.2f lambda, %zu modes up to %.0f Hz, mode T60 %.2f..%.2f s\n",
                     W, D, modeCount, fmax, tMin, tMax);
    }

    std::vector<float> outL(N), outR(N);
    for (size_t i = 0; i < N; ++i) {
        outL[i] = (float)(mid[i] + p.micWidth * side[i]);
        outR[i] = (float)(mid[i] - p.micWidth * side[i]);
    }

    if (!writeWavFloat(argv[1], outL, outR, (int)fs)) {
        std::fprintf(stderr, "can't write %s\n", argv[1]);
        return 1;
    }
    float peak = 0.0f;
    for (size_t i = 0; i < N; ++i) peak = std::max({ peak, std::fabs(outL[i]), std::fabs(outR[i]) });
    std::fprintf(stderr, "%s: note %.1f (%.1f Hz), peak %.4f\n", argv[1], p.note, f0, peak);
    return 0;
}
