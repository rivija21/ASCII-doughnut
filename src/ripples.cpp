// ripples.cpp - A ripple tank: click anywhere and a pebble drops in. Real waves spread from it,
//               bounce off the edges and pass through each other.
//
// The physics is the 2D wave equation, which describes small waves on water:
//
//     d2u/dt2 = c^2 * (d2u/dx2 + d2u/dy2)
//
// u(x, y) is how far the water is above (or below) its calm level. The right-hand side is the
// curvature of the surface: it is big where a point sits lower than the average of its neighbours.
// So every bit of water accelerates towards the average of its neighbours. A dip pulls the water
// around it down, which pulls the water around THAT down, and the disturbance spreads out as a ring
// at speed c.
//
// On a grid, the derivatives become differences between neighbouring cells (finite differences):
//
//     next = u + (u - prev) + C^2 * (left + right + up + down - 4u)
//              ^ keep moving   ^ accelerate towards the neighbours' average
//
// where C = c * dt / dx is the "Courant number". It must stay below 1/sqrt(2) in 2D (the CFL
// condition): a wave must not cross more than one cell per step, or the numbers blow up.
#include "sim.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

namespace {

constexpr double kStep = 1.0 / 90;    // physics steps: 90 per second, whatever the frame rate
constexpr float kCourant2 = 0.25f;    // C^2. C = 0.5, so waves travel 0.5 cells per step = 45 cells/s
constexpr float kDamping = 0.007f;    // fraction of the motion lost each step: ripples fade in a few seconds
constexpr float kSpongeDamping = 0.12f;   // extra damping at the edges in "open water" mode
constexpr int kSponge = 10;           // ...over a border this many cells wide
constexpr float kDropDepth = 3.0f;    // how far a pebble pushes the water down
constexpr float kDragDepth = 1.0f;    // a smaller push for each step of a drag
constexpr float kDropWidth = 2.0f;    // the dip is a smooth bump this wide (a single cell would ring)
constexpr float kMakerFreq = 5.5f;    // the wave maker's wiggles per second: wavelength = 45 / 5.5 = 8 cells
constexpr float kMakerSize = 0.55f;   // how far the wave maker bobs up and down
constexpr float kCalm = 0.1f;         // below this the water counts as calm and is left blank

// Characters from calm to steep, and the water's colours from deep trough to bright crest.
const char kRamp[] = " .:-=+*#%@";
constexpr int kRampLen = sizeof(kRamp) - 1;
constexpr int kLevels = 16;

class Ripples : public Simulation {
public:
    Ripples() {
        for (int i = 0; i < kLevels; ++i) {
            const float t = float(i) / (kLevels - 1);
            crest_[i] = gradient({{20, 70, 130}, {60, 160, 230}, {170, 230, 255}, {245, 252, 255}}, t);
            trough_[i] = gradient({{10, 30, 70}, {25, 70, 150}, {40, 110, 200}}, t);
        }
    }

    const char* name() const override { return "Ripple Tank"; }
    const char* help() const override { return "click drop  Space random  W wave maker  B slits  E edges  C calm"; }

    void resize(int w, int h) override {
        w_ = w;
        h_ = h;
        // The grid has square cells, one column wide. A terminal row is twice as tall as a column
        // is wide, so every row of the terminal shows two rows of the grid. That keeps rings round.
        gw_ = w;
        gh_ = 2 * h;
        u_.assign(size_t(gw_) * gh_, 0.0f);
        prev_.assign(u_.size(), 0.0f);
        buildWalls();
        drop(gw_ / 2.0f, gh_ / 2.0f, kDropDepth);   // something to look at before the first click
    }

    void onKey(int key) override {
        if (key == ' ') drop(rng_.uniform(0, float(gw_)), rng_.uniform(0, float(gh_)), kDropDepth);
        if (key == 'w') maker_ = !maker_;
        if (key == 'b') {
            slits_ = !slits_;
            buildWalls();
        }
        if (key == 'e') open_ = !open_;
        if (key == 'c') {
            std::fill(u_.begin(), u_.end(), 0.0f);
            std::fill(prev_.begin(), prev_.end(), 0.0f);
        }
    }

    // A click drops a pebble on that cell (the middle of its two grid rows). Dragging with the button
    // held makes a trail of smaller pushes, like a finger pulled through the water.
    void onClick(int x, int y, bool drag) override { drop(x + 0.5f, 2 * y + 1.0f, drag ? kDragDepth : kDropDepth); }

    void update(double dt) override {
        // Fixed time step (see fire.cpp): the wave equation needs equal steps, and the CFL limit
        // above is only true for THIS step size.
        leftover_ += dt;
        while (leftover_ >= kStep) {
            leftover_ -= kStep;
            time_ += kStep;
            step();
        }
    }

    void draw(Canvas& cv) override {
        for (int y = 0; y < h_; ++y) {
            for (int x = 0; x < w_; ++x) {
                const int a = (2 * y) * gw_ + x, b = a + gw_;   // the two grid cells behind this one
                if (wall_[a]) {
                    cv.set(x, y, '#', Color{120, 120, 130});
                    continue;
                }
                // Light the water from the top left: a surface tilted towards the light is bright, one
                // tilted away is dark. The tilt is how much higher the water is on one side than the other.
                const float tiltX = at(x - 1, 2 * y) - at(x + 1, 2 * y);
                const float tiltY = at(x, 2 * y - 1) - at(x, 2 * y + 2);
                const float lit = 0.35f * (u_[a] + u_[b]) + 1.6f * (tiltX + tiltY);
                const float mag = std::fabs(lit);
                if (mag < kCalm) continue;   // calm water stays black
                const int ch = std::min(int((mag - kCalm) * 6) + 1, kRampLen - 1);
                const int level = std::min(int(mag * 8), kLevels - 1);
                cv.set(x, y, kRamp[ch], lit > 0 ? crest_[level] : trough_[level]);
            }
        }
        if (maker_)
            for (int y = 0; y < h_; ++y) cv.set(2, y, '|', Color{255, 200, 60});

        char info[128];
        std::snprintf(info, sizeof info, " ripple tank   click anywhere to drop a pebble   edges: %s   waves: %.0f cells/s",
                      open_ ? "open water (absorb)" : "walls (reflect)", std::sqrt(kCourant2) / kStep);
        cv.text(0, 0, info, Color{160, 160, 160});
    }

private:
    // u at grid cell (x, y). Off the edge of the tank, use the nearest cell inside: the slope at the
    // wall is zero, which is how water meets a wall (and it makes waves bounce back).
    float at(int x, int y) const {
        x = std::clamp(x, 0, gw_ - 1);
        y = std::clamp(y, 0, gh_ - 1);
        return u_[y * gw_ + x];
    }

    // Push the water down in a smooth dip around (cx, cy). u and prev both move, so the water starts
    // at rest (in the wrong place) instead of with a sudden speed.
    void drop(float cx, float cy, float depth) {
        const int r = int(3 * kDropWidth) + 1;
        for (int y = int(cy) - r; y <= int(cy) + r; ++y) {
            for (int x = int(cx) - r; x <= int(cx) + r; ++x) {
                if (x < 0 || y < 0 || x >= gw_ || y >= gh_ || wall_[y * gw_ + x]) continue;
                const float d2 = (x - cx) * (x - cx) + (y - cy) * (y - cy);
                const float push = depth * std::exp(-d2 / (2 * kDropWidth * kDropWidth));
                u_[y * gw_ + x] -= push;
                prev_[y * gw_ + x] -= push;
            }
        }
    }

    // One step of the wave equation for the whole tank. The new value only needs this cell's prev,
    // so it can be written straight into prev_, and then the two arrays swap roles.
    void step() {
        for (int y = 0; y < gh_; ++y) {
            for (int x = 0; x < gw_; ++x) {
                const int i = y * gw_ + x;
                if (wall_[i]) {
                    prev_[i] = 0;   // a barrier: the water there can't move
                    continue;
                }
                const float c = u_[i];
                const float left = x > 0 && !wall_[i - 1] ? u_[i - 1] : c;
                const float right = x < gw_ - 1 && !wall_[i + 1] ? u_[i + 1] : c;
                const float up = y > 0 && !wall_[i - gw_] ? u_[i - gw_] : c;
                const float down = y < gh_ - 1 && !wall_[i + gw_] ? u_[i + gw_] : c;
                const float laplacian = left + right + up + down - 4 * c;
                prev_[i] = c + (c - prev_[i]) * (1 - damping(x, y)) + kCourant2 * laplacian;
            }
        }
        u_.swap(prev_);
        // The wave maker: a straight bar near the left edge, bobbing up and down. It sends out
        // straight (plane) waves, like the vibrating bar in a school ripple tank.
        if (maker_) {
            const float lift = kMakerSize * float(std::sin(2 * 3.14159265 * kMakerFreq * time_));
            for (int y = 0; y < gh_; ++y) u_[y * gw_ + 2] = lift;
        }
    }

    // "Open water" puts a sponge round the edges: extra damping that grows towards the wall, so waves
    // fade away there instead of bouncing back. (Damping that switched on suddenly would itself
    // reflect waves, so it rises gradually.)
    float damping(int x, int y) const {
        if (!open_) return kDamping;
        const int edge = std::min({x, gw_ - 1 - x, y, gh_ - 1 - y});
        if (edge >= kSponge) return kDamping;
        const float t = float(kSponge - edge) / kSponge;
        return kDamping + kSpongeDamping * t * t;
    }

    // B builds a wall down the tank with two narrow gaps: Young's double-slit experiment. Turn the
    // wave maker on (W) and the waves squeezing through the two gaps spread out and overlap; where
    // crest meets crest they add up, where crest meets trough they cancel, in fanned-out stripes.
    void buildWalls() {
        wall_.assign(size_t(gw_) * gh_, 0);
        if (!slits_) return;
        const int wx = gw_ * 2 / 5;
        const int gap = 3;                                    // half the width of each slit, in cells
        const int apart = std::clamp(gh_ / 3, 14, 36);         // distance between the slits' centres
        for (int y = 0; y < gh_; ++y) {
            const int d1 = std::abs(y - (gh_ / 2 - apart / 2)), d2 = std::abs(y - (gh_ / 2 + apart / 2));
            if (d1 <= gap || d2 <= gap) continue;
            for (int x = wx; x < wx + 2; ++x) wall_[y * gw_ + x] = 1;
        }
        for (size_t i = 0; i < wall_.size(); ++i)
            if (wall_[i]) u_[i] = prev_[i] = 0;
    }

    int w_ = 0, h_ = 0;     // terminal cells
    int gw_ = 0, gh_ = 0;   // grid cells (gh_ = 2 * h_)
    std::vector<float> u_, prev_;   // the water's height now, and one step ago
    std::vector<unsigned char> wall_;
    bool maker_ = false, slits_ = false, open_ = true;   // open water by default: rings spread out and leave
    double leftover_ = 0, time_ = 0;
    Color crest_[kLevels], trough_[kLevels];
    Random rng_;
};

}  // namespace

std::unique_ptr<Simulation> makeRipples() { return std::make_unique<Ripples>(); }
