// donut.cpp - The famous spinning ASCII donut (after Andy Sloane's donut.c, 2006).
//
// The idea in one sentence: sample thousands of points on the surface of a torus, rotate
// them in 3D, project them onto the 2D terminal, keep only the point nearest the viewer in
// each cell (the z-buffer), and pick a character whose "amount of ink" matches how brightly
// that point is lit.
#include "sim.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace {

constexpr float kTwoPi = 6.2831853f;

// Characters ordered from darkest (little ink) to brightest (lots of ink).
const char kShades[] = ".,-~:;=!*#$@";
constexpr int kNumShades = sizeof(kShades) - 1;   // 12 (the -1 skips the hidden '\0' at the end)

// ---- a tiny bit of 3D maths ------------------------------------------------

struct Vec3 {
    float x, y, z;
};

// The cosine and sine of an angle, computed once and reused for many points.
struct Rotation {
    float c, s;
    explicit Rotation(float angle) : c(std::cos(angle)), s(std::sin(angle)) {}
};

// Rotate a point around the X, Y or Z axis. Each is just a 2D rotation of the other two coordinates:
//   x' = x*cos - y*sin
//   y' = x*sin + y*cos
Vec3 rotateX(Vec3 v, Rotation r) { return {v.x, v.y * r.c - v.z * r.s, v.y * r.s + v.z * r.c}; }
Vec3 rotateY(Vec3 v, Rotation r) { return {v.x * r.c + v.z * r.s, v.y, -v.x * r.s + v.z * r.c}; }
Vec3 rotateZ(Vec3 v, Rotation r) { return {v.x * r.c - v.y * r.s, v.x * r.s + v.y * r.c, v.z}; }

// ---- the simulation --------------------------------------------------------

class Donut : public Simulation {
public:
    Donut() {
        // One colour per shade character: dark chocolate in shadow -> golden glaze in the light.
        for (int i = 0; i < kNumShades; ++i) {
            const float t = float(i) / (kNumShades - 1);
            warm_[i] = gradient({{70, 30, 15}, {190, 95, 35}, {250, 170, 70}, {255, 240, 200}}, t);
            plain_[i] = lerp({90, 90, 90}, {255, 255, 255}, t);
        }
    }

    const char* name() const override { return "Spinning Donut"; }
    const char* help() const override { return "+/- speed  C colour"; }

    void resize(int w, int h) override {
        w_ = w;
        h_ = h;
        zbuf_.assign(size_t(w) * h, 0.0f);
    }

    void onKey(int key) override {
        if (key == '+' || key == '=') speed_ = std::min(speed_ * 1.25, 8.0);
        if (key == '-') speed_ = std::max(speed_ / 1.25, 0.1);
        if (key == 'c') colour_ = !colour_;
    }

    void update(double dt) override {
        // The donut tumbles around two axes at different speeds, so it never repeats exactly.
        angleA_ += 1.2 * speed_ * dt;   // radians per second around the X axis
        angleB_ += 0.6 * speed_ * dt;   // radians per second around the Z axis
    }

    void draw(Canvas& cv) override {
        const float R1 = 1.0f;   // radius of the tube (the "dough" thickness)
        const float R2 = 2.0f;   // distance from the donut's centre to the middle of the tube
        const float K2 = 5.0f;   // distance from the viewer's eye to the donut

        // A terminal cell is about twice as tall as it is wide, so the window is "2*h_" columns tall
        // if we measured it in square units. Use whichever direction is smaller.
        const float size = std::min(float(w_), 2.0f * h_);
        // K1 converts "x / z" into screen cells. This choice makes the donut fill ~3/4 of the window.
        const float K1 = size * K2 * 3.0f / (8.0f * (R1 + R2));

        // How far apart to sample points. Neighbouring points must land less than one cell apart on
        // screen, even on the part of the donut nearest the viewer (z = K2 - R1 - R2, where it looks
        // biggest). Otherwise some cells get skipped and the back of the donut shows through the gaps.
        const float zNearest = K2 - R1 - R2;
        const float thetaStep = std::min(0.07f, 0.9f * zNearest / (K1 * R1));          // around the tube
        const float phiStep = std::min(0.02f, 0.9f * zNearest / (K1 * (R1 + R2)));     // around the donut

        // phi takes the same values for every theta, so compute their cos/sin once per frame, not
        // once per point. (Trig functions are slow compared to + and *.)
        phiTable_.clear();
        for (float phi = 0; phi < kTwoPi; phi += phiStep) phiTable_.emplace_back(phi);

        std::fill(zbuf_.begin(), zbuf_.end(), 0.0f);
        const Rotation rotA{float(angleA_)}, rotB{float(angleB_)};
        const Color* palette = colour_ ? warm_ : plain_;

        // theta goes around the tube's cross-section (a small circle)...
        for (float theta = 0; theta < kTwoPi; theta += thetaStep) {
            const Rotation t(theta);
            // A point on that small circle, sitting R2 to the right of the centre...
            const Vec3 circlePoint{R2 + R1 * t.c, R1 * t.s, 0};
            // ...and the surface normal there (an arrow pointing straight out of the surface).
            const Vec3 circleNormal{t.c, t.s, 0};

            // ...and phi spins that small circle around the Y axis, sweeping out the whole donut.
            for (const Rotation& p : phiTable_) {
                // Build the donut (rotate by phi), then tumble it (rotate by A, then by B).
                const Vec3 point = rotateZ(rotateX(rotateY(circlePoint, p), rotA), rotB);
                const Vec3 normal = rotateZ(rotateX(rotateY(circleNormal, p), rotA), rotB);

                // Perspective projection: things further away (bigger z) look smaller.
                const float z = point.z + K2;
                const float ooz = 1.0f / z;   // "one over z": bigger means closer to the viewer
                const int sx = int(w_ / 2.0f + K1 * ooz * point.x);
                const int sy = int(h_ / 2.0f - K1 * ooz * point.y * 0.5f);   // minus: screen rows go DOWN
                if (sx < 0 || sx >= w_ || sy < 0 || sy >= h_) continue;

                // Z-buffer: only draw this point if it's closer than whatever is already in this cell.
                float& nearest = zbuf_[sy * w_ + sx];
                if (ooz <= nearest) continue;
                nearest = ooz;

                // Lighting: how much does the surface face the light? That's the dot product of the
                // normal with the light direction (0, 1, -1)/sqrt(2): up and towards the viewer.
                // Result is -1 (facing away) .. +1 (facing the light directly).
                const float luminance = (normal.y - normal.z) * 0.70710678f;
                const int shade = std::clamp(int(luminance * kNumShades), 0, kNumShades - 1);
                cv.set(sx, sy, kShades[shade], palette[shade]);
            }
        }
    }

private:
    int w_ = 0, h_ = 0;
    std::vector<float> zbuf_;   // 1/z of the nearest point drawn in each cell (0 = nothing yet)
    std::vector<Rotation> phiTable_;
    double angleA_ = 0, angleB_ = 0;
    double speed_ = 1.0;
    bool colour_ = true;
    Color warm_[kNumShades], plain_[kNumShades];
};

}  // namespace

std::unique_ptr<Simulation> makeDonut() { return std::make_unique<Donut>(); }
