// pendulum.cpp - A double pendulum: two rods, two weights, and a lot of chaos.
//
// The physics recipe:
//   1. Describe the system by its STATE: two angles and two angular velocities.
//   2. Physics (Lagrange's equations) tells us how fast the state is changing: derivative(state).
//   3. A numerical integrator (RK4) uses that to step the state forward by a tiny time h.
//   4. Repeat 500 times per simulated second. Draw a picture whenever a frame is due.
#include "sim.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <deque>
#include <vector>

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double g = 9.81;              // gravity, m/s^2
constexpr double m1 = 1.0, m2 = 1.0;    // masses of the upper and lower bob, kg
constexpr double L1 = 1.0, L2 = 1.0;    // lengths of the upper and lower rod, m
constexpr double kStep = 0.002;         // physics time step h in seconds (500 steps per second)
constexpr int kTrailLen = 400;          // remembered positions of the lower bob (one every 2 steps)

const Color kRod{130, 130, 140};
const Color kMainColour{255, 170, 50};
const Color kTwinColour{80, 200, 255};
const Color kFaded{25, 25, 35};

// ---- physics ---------------------------------------------------------------

// Angles are measured from straight down: 0 = hanging still, pi = balanced straight up.
struct State {
    double th1, th2;   // angles of the upper and lower rod (radians)
    double w1, w2;     // their angular velocities (radians per second)
};

// Let us write maths on whole states, e.g. s + h * k.
State operator+(const State& a, const State& b) { return {a.th1 + b.th1, a.th2 + b.th2, a.w1 + b.w1, a.w2 + b.w2}; }
State operator*(double k, const State& s) { return {k * s.th1, k * s.th2, k * s.w1, k * s.w2}; }

// The equations of motion of a double pendulum. Returns d(state)/dt:
// the angles change at the angular velocities, and the angular velocities change at
// the angular accelerations a1, a2 (derived with Lagrangian mechanics).
State derivative(const State& s) {
    const double d = s.th1 - s.th2;
    const double den = 2 * m1 + m2 - m2 * std::cos(2 * d);
    const double a1 = (-g * (2 * m1 + m2) * std::sin(s.th1) - m2 * g * std::sin(s.th1 - 2 * s.th2) -
                       2 * std::sin(d) * m2 * (s.w2 * s.w2 * L2 + s.w1 * s.w1 * L1 * std::cos(d))) /
                      (L1 * den);
    const double a2 = (2 * std::sin(d) *
                       (s.w1 * s.w1 * L1 * (m1 + m2) + g * (m1 + m2) * std::cos(s.th1) +
                        s.w2 * s.w2 * L2 * m2 * std::cos(d))) /
                      (L2 * den);
    return {s.w1, s.w2, a1, a2};
}

// Explicit Euler: follow the slope at the START of the step. Simple, but the error always
// pushes the same way, so the energy slowly creeps up and the pendulum goes wild.
State stepEuler(const State& s, double h) { return s + h * derivative(s); }

// Classic 4th-order Runge-Kutta: measure the slope 4 times across the step (start, two guesses
// at the middle, end) and use a weighted average. Hugely more accurate for the same h.
State stepRK4(const State& s, double h) {
    const State k1 = derivative(s);
    const State k2 = derivative(s + (h / 2) * k1);
    const State k3 = derivative(s + (h / 2) * k2);
    const State k4 = derivative(s + h * k3);
    return s + (h / 6) * (k1 + 2 * k2 + 2 * k3 + k4);
}

// Kinetic + potential energy. With no friction this should never change; how much it DOES
// change tells you how accurate the integrator is.
double energy(const State& s) {
    const double v1sq = L1 * L1 * s.w1 * s.w1;
    const double v2sq = v1sq + L2 * L2 * s.w2 * s.w2 + 2 * L1 * L2 * s.w1 * s.w2 * std::cos(s.th1 - s.th2);
    const double kinetic = 0.5 * m1 * v1sq + 0.5 * m2 * v2sq;
    const double potential = -(m1 + m2) * g * L1 * std::cos(s.th1) - m2 * g * L2 * std::cos(s.th2);
    return kinetic + potential;
}

struct Vec2 {
    double x, y;
};

// Positions of the two bobs in metres. Pivot at (0, 0), y points DOWN (like screen rows).
void bobPositions(const State& s, Vec2& b1, Vec2& b2) {
    b1 = {L1 * std::sin(s.th1), L1 * std::cos(s.th1)};
    b2 = {b1.x + L2 * std::sin(s.th2), b1.y + L2 * std::cos(s.th2)};
}

// ---- drawing helpers -------------------------------------------------------

// Pick the character that best matches the direction of a line.
char lineChar(double dx, double dy) {
    // dy is in rows (down is positive). A row is about 2 columns tall, so scale it to look right.
    double deg = std::atan2(-dy * 2.0, dx) * 180.0 / kPi;   // -180..180
    if (deg < 0) deg += 180;                                // a line looks the same both ways round
    if (deg < 22.5 || deg >= 157.5) return '-';
    if (deg < 67.5) return '/';
    if (deg < 112.5) return '|';
    return '\\';
}

// Draw a straight line by stepping along it one cell at a time (a simple "DDA" line algorithm).
void drawLine(Canvas& cv, Vec2 a, Vec2 b, Color c) {
    const char ch = lineChar(b.x - a.x, b.y - a.y);
    const int steps = int(std::max(std::abs(b.x - a.x), std::abs(b.y - a.y))) + 1;
    for (int i = 0; i <= steps; ++i) {
        const double t = double(i) / steps;
        cv.set(int(std::lround(a.x + (b.x - a.x) * t)), int(std::lround(a.y + (b.y - a.y) * t)), ch, c);
    }
}

void plot(Canvas& cv, Vec2 p, char ch, Color c) { cv.set(int(std::lround(p.x)), int(std::lround(p.y)), ch, c); }

// ---- the simulation --------------------------------------------------------

struct Pendulum {
    State state;
    std::deque<Vec2> trail;   // recent positions of the lower bob, oldest first
    Color colour;
};

class DoublePendulum : public Simulation {
public:
    DoublePendulum() { reset(); }

    const char* name() const override { return "Double Pendulum"; }
    const char* help() const override { return "R new swing  T twin  I integrator  +/- speed"; }

    void resize(int w, int h) override {
        w_ = w;
        h_ = h;
    }

    void onKey(int key) override {
        if (key == 'r') reset();
        if (key == 't') toggleTwin();
        if (key == 'i') {
            useRK4_ = !useRK4_;
            e0_ = energy(pends_[0].state);   // measure drift from the moment we switched
        }
        if (key == '+' || key == '=') timeScale_ = std::min(timeScale_ * 2, 4.0);
        if (key == '-') timeScale_ = std::max(timeScale_ / 2, 0.125);
    }

    void update(double dt) override {
        // Fixed time step (same idea as the fire): physics always moves in equal steps of kStep,
        // however long the frame took. 'leftover_' keeps the remainder for next frame.
        leftover_ += dt * timeScale_;
        while (leftover_ >= kStep) {
            leftover_ -= kStep;
            simTime_ += kStep;
            for (Pendulum& p : pends_) {
                p.state = useRK4_ ? stepRK4(p.state, kStep) : stepEuler(p.state, kStep);
                // Euler can eventually blow up to infinity. If that happens, just start over.
                if (!std::isfinite(p.state.w1) || !std::isfinite(p.state.w2)) {
                    reset();
                    return;
                }
            }
            if (++stepCount_ % 2 == 0) recordTrail();
        }
    }

    void draw(Canvas& cv) override {
        // Scale so the full reach (L1 + L2 in every direction) fits on screen. Horizontally one metre
        // is twice as many columns as it is rows vertically, because cells are twice as tall as wide.
        const double reach = L1 + L2;
        const double rowsPerMetre = std::min((h_ - 2) / (2 * reach), (w_ - 2) / (4 * reach));
        const Vec2 centre{w_ / 2.0, h_ / 2.0};
        auto toScreen = [&](Vec2 p) { return Vec2{centre.x + p.x * rowsPerMetre * 2, centre.y + p.y * rowsPerMetre}; };

        // 1. Trails first, so the pendulums are drawn on top. Older points are dimmer.
        for (const Pendulum& p : pends_) {
            const int n = int(p.trail.size());
            for (int i = 0; i < n; ++i) {
                const float age = 1.0f - float(i + 1) / n;   // 0 = newest .. ~1 = oldest
                const char ch = age < 0.15f ? '*' : age < 0.5f ? ':' : '.';
                plot(cv, toScreen(p.trail[i]), ch, lerp(p.colour, kFaded, age));
            }
        }

        // 2. Rods and bobs.
        const Vec2 pivot = toScreen({0, 0});
        for (const Pendulum& p : pends_) {
            Vec2 b1, b2;
            bobPositions(p.state, b1, b2);
            const Vec2 s1 = toScreen(b1), s2 = toScreen(b2);
            const Color rod = lerp(kRod, p.colour, 0.25f);
            drawLine(cv, pivot, s1, rod);
            drawLine(cv, s1, s2, rod);
            plot(cv, s1, 'O', p.colour);
            plot(cv, s2, '@', p.colour);
        }
        plot(cv, pivot, '+', Color{255, 255, 255});

        // 3. Numbers.
        char line[128];
        const double e = energy(pends_[0].state);
        std::snprintf(line, sizeof line, " %s   t = %.1f s   speed x%g   energy %.4f J   drift %+.1e J",
                      useRK4_ ? "RK4  " : "Euler", simTime_, timeScale_, e, e - e0_);
        cv.text(0, 0, line, Color{170, 170, 170});
        if (pends_.size() > 1) {
            Vec2 a1, a2, b1, b2;
            bobPositions(pends_[0].state, a1, a2);
            bobPositions(pends_[1].state, b1, b2);
            std::snprintf(line, sizeof line, " twin started 0.001 rad apart - lower bobs are now %.3f m apart",
                          std::hypot(a2.x - b2.x, a2.y - b2.y));
            cv.text(0, 1, line, kTwinColour);
        }
    }

private:
    void reset() {
        // Start somewhere between sideways and upside-down, on a random side. That's enough energy for chaos.
        auto randomAngle = [&] {
            const double a = rng_.uniform(0.5f, 1.0f) * kPi;
            return rng_.integer(0, 1) ? a : -a;
        };
        pends_.clear();
        pends_.push_back({State{randomAngle(), randomAngle(), 0, 0}, {}, kMainColour});
        e0_ = energy(pends_[0].state);
        simTime_ = 0;
        leftover_ = 0;
    }

    // Add (or remove) a second pendulum that starts almost exactly like the first one.
    void toggleTwin() {
        if (pends_.size() > 1) {
            pends_.pop_back();
            return;
        }
        Pendulum twin = pends_[0];
        twin.state.th1 += 0.001;   // 0.001 radians = 0.06 degrees. Watch how fast this grows!
        twin.trail.clear();
        twin.colour = kTwinColour;
        pends_.push_back(twin);
    }

    void recordTrail() {
        for (Pendulum& p : pends_) {
            Vec2 b1, b2;
            bobPositions(p.state, b1, b2);
            p.trail.push_back(b2);
            if (int(p.trail.size()) > kTrailLen) p.trail.pop_front();
        }
    }

    int w_ = 0, h_ = 0;
    std::vector<Pendulum> pends_;   // one, or two when the twin is on
    bool useRK4_ = true;
    double timeScale_ = 1.0;
    double leftover_ = 0;
    double simTime_ = 0;
    double e0_ = 0;                 // energy at the start, to measure drift against
    long long stepCount_ = 0;
    Random rng_;
};

}  // namespace

std::unique_ptr<Simulation> makePendulum() { return std::make_unique<DoublePendulum>(); }
