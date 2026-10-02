// fire.cpp - The "Doom fire" effect (from the PlayStation port of Doom, 1995; made famous
//            by Fabien Sanglard's write-up "How DOOM fire was done").
//
// The screen is a grid of HEAT values: 0 = cold, 1 = white-hot.
//   * The bottom row is the fuel: it is kept at full heat.
//   * Every step, each cell passes its heat to the cell ABOVE it, minus a random amount of
//     cooling, and nudged randomly one column left or right.
// That's the whole algorithm. Heat that rises, cools at random and wobbles sideways looks like flames.
// A rule like this ("each cell's next value depends only on its neighbours") is a cellular automaton.
#include "sim.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

namespace {

// Characters from "no fire" to "white-hot".
const char kRamp[] = " .:-=+*#%@";
constexpr int kRampLen = sizeof(kRamp) - 1;
constexpr int kPaletteSize = 32;
constexpr double kStep = 1.0 / 40;   // the fire updates 40 times per second, whatever the frame rate

class Fire : public Simulation {
public:
    Fire() {
        // Precompute one colour per heat level: black -> red -> orange -> yellow -> white.
        for (int i = 0; i < kPaletteSize; ++i) {
            const float t = float(i) / (kPaletteSize - 1);
            palette_[i] = gradient({{0, 0, 0}, {120, 10, 0}, {230, 60, 0}, {255, 150, 20}, {255, 230, 120}, {255, 255, 240}}, t);
        }
    }

    const char* name() const override { return "Doom Fire"; }
    const char* help() const override { return "Space fuel  Up/Down height  Left/Right wind"; }

    void resize(int w, int h) override {
        w_ = w;
        h_ = h;
        heat_.assign(size_t(w) * h, 0.0f);
    }

    void onKey(int key) override {
        if (key == ' ') burning_ = !burning_;
        if (key == KEY_UP) heightFrac_ = std::min(heightFrac_ + 0.1f, 1.2f);
        if (key == KEY_DOWN) heightFrac_ = std::max(heightFrac_ - 0.1f, 0.2f);
        if (key == KEY_RIGHT) wind_ = std::min(wind_ + 0.25f, 1.0f);
        if (key == KEY_LEFT) wind_ = std::max(wind_ - 0.25f, -1.0f);
    }

    // Fixed time step: frames may arrive at uneven intervals, but the fire always advances in equal
    // steps of kStep. 'leftover_' carries the bit of time that wasn't enough for a full step.
    void update(double dt) override {
        leftover_ += dt;
        while (leftover_ >= kStep) {
            leftover_ -= kStep;
            step();
        }
    }

    void draw(Canvas& cv) override {
        for (int y = 0; y < h_; ++y) {
            for (int x = 0; x < w_; ++x) {
                const float t = heat_[y * w_ + x];
                const int ch = std::min(int(t * kRampLen), kRampLen - 1);
                if (ch == 0) continue;   // kRamp[0] is a space
                cv.set(x, y, kRamp[ch], palette_[int(t * (kPaletteSize - 1))]);
            }
        }
        char info[96];
        std::snprintf(info, sizeof info, " fuel %s   flame height %d%%   wind %+.2f",
                      burning_ ? "ON " : "OFF", int(std::lround(heightFrac_ * 100)), wind_);
        cv.text(0, 0, info, Color{160, 160, 160});
    }

private:
    void step() {
        // 1. Fuel row. When the fuel is off, let it die down gradually instead of all at once.
        float* fuel = &heat_[size_t(h_ - 1) * w_];
        for (int x = 0; x < w_; ++x) fuel[x] = burning_ ? 1.0f : std::max(0.0f, fuel[x] - 0.05f);

        // 2. Spread heat upwards. On average a cell loses 'cooling' per row, so the flames die out
        //    after about 1/cooling rows. Pick cooling so that's heightFrac_ of the window.
        const float cooling = 1.0f / (heightFrac_ * h_);
        // Go from the top row down. Row y writes into row y-1, and row y itself is only written
        // later (by row y+1), so we always read last step's value. That's why one array is enough.
        for (int y = 1; y < h_; ++y) {
            for (int x = 0; x < w_; ++x) {
                int dx = rng_.integer(-1, 1);                                   // random flicker
                if (rng_.uniform(0, 1) < std::abs(wind_)) dx += wind_ > 0 ? 1 : -1;   // steady wind
                const int dst = ((x + dx) % w_ + w_) % w_;                      // wrap around the edges
                const float h = heat_[y * w_ + x] - rng_.uniform(0, 2 * cooling);
                heat_[(y - 1) * w_ + dst] = std::max(0.0f, h);
            }
        }
    }

    int w_ = 0, h_ = 0;
    std::vector<float> heat_;   // w_ * h_ values, 0..1, row 0 is the top of the screen
    Color palette_[kPaletteSize];
    bool burning_ = true;
    float heightFrac_ = 0.6f;
    float wind_ = 0.0f;
    double leftover_ = 0;
    Random rng_;
};

}  // namespace

std::unique_ptr<Simulation> makeFire() { return std::make_unique<Fire>(); }
