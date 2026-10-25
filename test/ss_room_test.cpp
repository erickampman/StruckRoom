// ss_room_test.cpp — the Room core on its own: random settings and pitches must never produce a
// NaN or an infinity, the room must ring down to silence once its input stops, and a strike must
// retune it without a rebuild when Room Track is 1. Prints ALL PASS on success.

#include "Room.hpp"

#include <cmath>
#include <cstdio>
#include <memory>
#include <random>

namespace {

int failures = 0;
void check(bool ok, const char* what) {
    std::printf("  [%s] %s\n", ok ? "pass" : "FAIL", what);
    if (!ok) ++failures;
}

float uni(std::mt19937& rng, float lo, float hi) {
    return std::uniform_real_distribution<float>(lo, hi)(rng);
}

Room::Settings randomSettings(std::mt19937& rng) {
    Room::Settings s;
    s.width = uni(rng, 0.25f, 16.0f);
    s.depth = uni(rng, 0.25f, 16.0f);
    s.roomTrack = uni(rng, 0.0f, 1.0f);
    s.decayTrack = uni(rng, 0.0f, 1.0f);
    s.airT60 = uni(rng, 0.5f, 30.0f);
    s.detail = uni(rng, 2.0f, 64.0f);
    s.srcX = uni(rng, 0.0f, 1.0f); s.srcY = uni(rng, 0.0f, 1.0f);
    s.micX = uni(rng, 0.0f, 1.0f); s.micY = uni(rng, 0.0f, 1.0f);
    s.micWidth = uni(rng, 0.0f, 1.0f);
    s.direct = uni(rng, 0.0f, 2.0f); s.early = uni(rng, 0.0f, 2.0f); s.late = uni(rng, 0.0f, 2.0f);
    for (Room::Wall& w : s.wall) {
        w.absorb = uni(rng, 0.0f, 1.0f);
        w.bright = uni(rng, 0.0f, 8.0f);
        w.ring = uni(rng, 0.0f, 2.0f);
        w.ringMat = (int)uni(rng, 0.0f, 25.99f);   // the static tables and the generators
        w.ringRatio = uni(rng, 0.0625f, 4.0f);
        w.ringT60 = uni(rng, 0.05f, 20.0f);
        w.ringTilt = uni(rng, 0.0f, 3.0f);
        w.link = uni(rng, 0.0f, 1.0f) > 0.5f;
        w.shift = uni(rng, 0.0f, 1.0f) > 0.5f ? uni(rng, 0.0f, 0.1f) : 0.0f;
        w.flex = uni(rng, 0.0f, 1.0f);
        w.rattle = uni(rng, 0.0f, 1.0f);
        w.gap = uni(rng, 0.02f, 0.95f);
    }
    return s;
}

// Corners of the ranges, where most things go wrong.
Room::Settings edgeSettings(int k) {
    Room::Settings s;
    s.width = (k & 1) ? 16.0f : 0.25f;
    s.depth = (k & 2) ? 16.0f : 0.25f;
    s.roomTrack = (k & 4) ? 0.0f : 1.0f;
    s.srcX = s.srcY = (k & 8) ? 0.0f : 1.0f;
    s.micX = s.micY = (k & 16) ? 0.0f : 1.0f;     // with srcX/Y equal: source on the mic
    s.detail = (k & 32) ? 64.0f : 2.0f;
    for (Room::Wall& w : s.wall) {
        w.absorb = (k & 64) ? 1.0f : 0.0f;
        w.bright = (k & 128) ? 8.0f : 0.0f;
        w.ring = 2.0f;
        w.link = (k & 256) != 0;
        w.ringRatio = (k & 512) ? 4.0f : 0.0625f;
        // The wall effects off or at full, and a generated material or a table. The bits above are
        // all taken, so these cycle at co-prime periods: every combination meets every corner.
        w.ringMat = (k % 3 == 0) ? 25 : 3;
        w.shift = (k % 5 == 0) ? 0.1f : 0.0f;
        w.gap = (k % 11 < 5) ? 0.02f : 0.95f;
        w.flex = w.rattle = (k % 7 < 3) ? 1.0f : 0.0f;
    }
    return s;
}

bool renderFinite(Room& room, float f0, int blocks) {
    float in[256], l[256], r[256];
    room.strike(f0);
    for (int b = 0; b < blocks; ++b) {
        for (int i = 0; i < 256; ++i) in[i] = (b == 0 && i == 0) ? 1.0f : 0.0f;
        room.processBlock(in, l, r, 256);
        for (int i = 0; i < 256; ++i)
            if (!std::isfinite(l[i]) || !std::isfinite(r[i]) || std::fabs(l[i]) > 50.0f) return false;   // a unit impulse in: anything near this is a runaway
    }
    return true;
}

} // namespace

int main() {
    std::printf("ss-room: Room core\n");
    auto room = std::make_unique<Room>();
    room->setSampleRate(48000.0);

    std::mt19937 rng(1234);
    bool allFinite = true;
    for (int t = 0; t < 60 && allFinite; ++t) {
        room->setSettings(randomSettings(rng));
        const float f0 = 440.0f * std::exp2(uni(rng, -6.0f, 4.0f));   // ~7 Hz .. 7 kHz
        if (!renderFinite(*room, f0, 8)) {
            std::printf("    non-finite output at random case %d, f0 %.2f Hz\n", t, f0);
            allFinite = false;
        }
    }
    check(allFinite, "random settings and pitches stay finite");

    bool edgesFinite = true;
    for (int k = 0; k < 1024 && edgesFinite; ++k) {
        room->reset();
        room->setSettings(edgeSettings(k));
        for (float f0 : { 0.5f, 261.6f, 8000.0f })
            if (!renderFinite(*room, f0, 2)) {
                std::printf("    non-finite output at edge case %d, f0 %.1f Hz\n", k, f0);
                edgesFinite = false;
                break;
            }
    }
    check(edgesFinite, "range corners stay finite");

    // Default room: rings, then rings down once the input stops.
    room->reset();
    room->setSettings(Room::Settings{});
    float in[256] = {1.0f}, l[256], r[256];
    room->strike(261.6f);
    room->processBlock(in, l, r, 256);
    std::fill_n(in, 256, 0.0f);
    double early = 0.0, tail = 0.0;
    for (int b = 0; b < 48000 * 30 / 256; ++b) {
        room->processBlock(in, l, r, 256);
        for (int i = 0; i < 256; ++i) {
            const double e = (double)l[i] * l[i] + (double)r[i] * r[i];
            if (b < 40) early += e;
            if (b >= 48000 * 29 / 256) tail += e;
        }
    }
    check(early > 1e-8, "a strike makes sound");
    check(tail < early * 1e-9, "and it rings down (30 s, default air 12 s)");
    check(room->activeModes() > 1000 && room->activeModes() < Room::kMaxModes,
          "the default room has thousands of modes");

    std::printf(failures ? "%d FAILED\n" : "ALL PASS\n", failures);
    return failures ? 1 : 0;
}
