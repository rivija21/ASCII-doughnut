// matrix.cpp - The "digital rain" from the film The Matrix.
//
// Two layers of state:
//   drops_            one falling "raindrop" per column: a position and a speed (these are particles).
//   glyph_, bright_   what character each cell shows, and how brightly (0..1).
//
// When a drop's head passes through a cell it gives the cell a fresh random character at full
// brightness. Every frame, every cell fades a little. Nothing else is needed: the fading
// cells left behind by a moving drop ARE the green trail.
#include "sim.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace {

const char kGlyphs[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789@#$%&*+=-<>?!/\\|{}[]:;";
constexpr int kNumGlyphs = sizeof(kGlyphs) - 1;
constexpr float kFadePerSecond = 1.0f;   // brightness lost per second, so a trail lasts ~1 s
constexpr float kShimmerRate = 0.5f;     // fraction of all cells that change character per second

struct Theme {
    Color dark, bright, head;
};

const Theme kThemes[] = {
    {{0, 35, 10}, {40, 255, 90}, {210, 255, 220}},    // classic green
    {{0, 25, 40}, {40, 210, 255}, {220, 250, 255}},   // cyan
    {{40, 18, 0}, {255, 170, 30}, {255, 240, 200}},   // amber
    {{40, 0, 25}, {255, 70, 170}, {255, 220, 240}},   // pink
};
constexpr int kNumThemes = sizeof(kThemes) / sizeof(kThemes[0]);

struct Drop {
    float y;       // row of the head (can be negative: still above the screen, waiting to fall in)
    float speed;   // rows per second
};

class MatrixRain : public Simulation {
public:
    const char* name() const override { return "Matrix Rain"; }
    const char* help() const override { return "+/- speed  C colour"; }

    void resize(int w, int h) override {
        w_ = w;
        h_ = h;
        glyph_.assign(size_t(w) * h, ' ');
        bright_.assign(size_t(w) * h, 0.0f);
        drops_.resize(w);
        // Scatter the first drops all over (some already on screen) so it doesn't start empty.
        for (Drop& d : drops_) {
            respawn(d);
            d.y = rng_.uniform(-float(h), float(h));
        }
    }

    void onKey(int key) override {
        if (key == '+' || key == '=') speed_ = std::min(speed_ * 1.25f, 5.0f);
        if (key == '-') speed_ = std::max(speed_ / 1.25f, 0.2f);
        if (key == 'c') theme_ = (theme_ + 1) % kNumThemes;
    }

    void update(double dt) override {
        const float step = float(dt) * speed_;

        // 1. Every cell fades a little.
        for (float& b : bright_) b = std::max(0.0f, b - kFadePerSecond * step);

        // 2. Move each drop down. Every cell its head moves INTO lights up with a new character.
        for (int x = 0; x < w_; ++x) {
            Drop& d = drops_[x];
            const int before = int(std::floor(d.y));
            d.y += d.speed * step;
            const int after = int(std::floor(d.y));
            for (int y = std::max(before + 1, 0); y <= after && y < h_; ++y) {
                glyph_[y * w_ + x] = randomGlyph();
                bright_[y * w_ + x] = 1.0f;
            }
            if (d.y >= h_) respawn(d);   // fell off the bottom: start again above the top
        }

        // 3. Randomly swap some characters so the trails shimmer.
        const int changes = int(glyph_.size() * kShimmerRate * step) + 1;
        for (int i = 0; i < changes; ++i) glyph_[rng_.integer(0, int(glyph_.size()) - 1)] = randomGlyph();
    }

    void draw(Canvas& cv) override {
        const Theme& theme = kThemes[theme_];
        for (int y = 0; y < h_; ++y) {
            for (int x = 0; x < w_; ++x) {
                const float b = bright_[y * w_ + x];
                if (b < 0.05f) continue;
                // Round brightness to 16 levels. Neighbouring cells then often share exactly the
                // same colour, so present() has fewer colour codes to send.
                const float level = std::round(b * 16) / 16;
                cv.set(x, y, glyph_[y * w_ + x], lerp(theme.dark, theme.bright, level));
            }
        }
        // The head of each drop is drawn almost white, with a new random character every frame.
        for (int x = 0; x < w_; ++x) {
            const int y = int(std::floor(drops_[x].y));
            if (y >= 0 && y < h_) cv.set(x, y, randomGlyph(), theme.head);
        }
    }

private:
    char randomGlyph() { return kGlyphs[rng_.integer(0, kNumGlyphs - 1)]; }

    void respawn(Drop& d) {
        // Start above the screen. How far above decides how long until it shows up.
        d.y = -rng_.uniform(1.0f, 1.5f * h_);
        d.speed = rng_.uniform(6.0f, 24.0f);
    }

    int w_ = 0, h_ = 0;
    std::vector<Drop> drops_;     // one per column
    std::vector<char> glyph_;     // w_ * h_ characters
    std::vector<float> bright_;   // w_ * h_ brightness values, 0..1
    float speed_ = 1.0f;
    int theme_ = 0;
    Random rng_;
};

}  // namespace

std::unique_ptr<Simulation> makeMatrix() { return std::make_unique<MatrixRain>(); }
