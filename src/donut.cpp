// donut.cpp - The famous spinning ASCII donut (after Andy Sloane's donut.c, 2006), which can
//             also FLY: it swoops around a figure-8 through 3D space, towards you and away,
//             and PULSE: it pops like a drop hitting water and sends out ripple rings.
//
// The idea in one sentence: sample thousands of points on the surface of a torus, rotate
// them in 3D, move them to wherever the donut is, project them onto the 2D terminal, keep only
// the point nearest the viewer in each cell (the z-buffer), and pick a character whose "amount
// of ink" matches how brightly that point is lit.
#include "sim.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <deque>
#include <vector>

namespace {

constexpr float kTwoPi = 6.2831853f;

// Characters ordered from darkest (little ink) to brightest (lots of ink).
const char kShades[] = ".,-~:;=!*#$@";
constexpr int kNumShades = sizeof(kShades) - 1;   // 12 (the -1 skips the hidden '\0' at the end)

// The donut and the camera.
constexpr float R1 = 1.0f;          // radius of the tube (the "dough" thickness)
constexpr float R2 = 2.0f;          // distance from the donut's centre to the middle of the tube
constexpr float kOuter = R1 + R2;   // the whole donut fits inside a ball of this radius
constexpr float K2 = 5.0f;          // distance from the viewer's eye to the donut when it spins in place

// The flight (see flightCentre() below).
constexpr double kLoopSeconds = 12.0;           // one trip round the figure-8, at speed x1
constexpr float kNearZ = 6.0f, kFarZ = 14.0f;   // the closest and furthest the centre gets
constexpr float kFogDensity = 0.06f;            // how fast far-away things fade (per unit of distance)
constexpr float kFlyZoom = 2.5f;                // flying uses a longer camera lens (see zoom())
constexpr double kGlideSeconds = 1.5;           // time to glide between spinning in place and flying
constexpr int kTrailLen = 75;                   // remembered centre positions, one per frame (2.5 s)

// The pulse (see pulse() below): a springy pop plus ripple rings.
constexpr double kSpringW0 = 17.0;     // the spring's natural frequency, radians per second
constexpr double kSpringZeta = 0.38;   // damping ratio: 0 wobbles for ever, 1 creeps back, between them it bounces
constexpr double kKick = 6.9;          // how hard a pulse kicks the swell (per second): it peaks at 1.25x
constexpr double kSpringStep = 0.001;  // the spring moves in fixed steps of 1 ms
constexpr float kMaxSwell = 1.4f;      // never draw the donut bigger than this, however often you press Space
constexpr double kRingSeconds = 1.2;   // how long a ripple ring lasts
constexpr float kRingReach = 1.6f;     // how far a ring spreads, in donut radii
constexpr double kEchoDelay = 0.15;    // a second, fainter ring follows the first this much later
constexpr int kMaxRings = 8;

// ---- a tiny bit of 3D maths ------------------------------------------------

struct Vec3 {
    float x, y, z;
};

Vec3 operator+(Vec3 a, Vec3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
Vec3 operator*(float k, Vec3 v) { return {k * v.x, k * v.y, k * v.z}; }
// lerp() for points. (Another overload called lerp would hide the Color one from terminal.h.)
Vec3 mix(Vec3 a, Vec3 b, float t) { return {a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t}; }

// Goes from 0 to 1 like t does, but starts and stops gently (its slope is 0 at both ends).
float smoothstep(float t) { return t * t * (3 - 2 * t); }

// Fog: the further away, the more colours fade into the black background. Light crossing haze
// loses the same FRACTION of itself for every unit of distance, so what's left falls off
// exponentially (the Beer-Lambert law). At z = K2, where the donut spins in place, nothing fades.
float fog(float z) { return std::min(1.0f, std::exp(-kFogDensity * (z - K2))); }

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

    const char* name() const override { return flying_ ? "Flying Donut" : "Spinning Donut"; }
    const char* help() const override { return "M fly/spin  T trail  Space pulse  +/- speed  C colour"; }

    void resize(int w, int h) override {
        w_ = w;
        h_ = h;
        zbuf_.assign(size_t(w) * h, 0.0f);
        trail_.clear();   // the remembered positions were worked out for the old window size
    }

    void onKey(int key) override {
        if (key == '+' || key == '=') speed_ = std::min(speed_ * 1.25, 8.0);
        if (key == '-') speed_ = std::max(speed_ / 1.25, 0.1);
        if (key == 'c') colour_ = !colour_;
        if (key == 'm') flying_ = !flying_;
        if (key == 't') showTrail_ = !showTrail_;
        if (key == ' ') pulse();
    }

    void update(double dt) override {
        // The donut tumbles around two axes at different speeds. (1.2 is exactly twice 0.6, so the
        // tumble repeats every 2*pi / 0.6 = 10.5 seconds, when B has turned once and A twice.)
        angleA_ += 1.2 * speed_ * dt;   // radians per second around the X axis
        angleB_ += 0.6 * speed_ * dt;   // radians per second around the Z axis

        // Move along the figure-8: t goes once round from 0 to 2*pi every kLoopSeconds.
        const double before = flightT_;
        flightT_ = std::fmod(flightT_ + kTwoPi / kLoopSeconds * speed_ * dt, double(kTwoPi));

        // Pulse by itself each time the flying donut swoops through the middle of the 8, closest to
        // you: that's where t passes pi, or wraps round from 2*pi back to 0.
        const bool crossedMiddle = flightT_ < before || (before < kTwoPi / 2 && flightT_ >= kTwoPi / 2);
        if (crossedMiddle && fly_ >= 1) pulse();

        // The pulse's spring moves in real time (not sped up by +/-), in fixed 1 ms steps.
        springLeft_ += dt;
        while (springLeft_ >= kSpringStep) {
            springLeft_ -= kSpringStep;
            stepSpring(kSpringStep);
        }
        for (Ring& r : rings_) r.age += dt;
        rings_.erase(std::remove_if(rings_.begin(), rings_.end(),
                                    [](const Ring& r) { return r.age >= kRingSeconds + kEchoDelay; }),
                     rings_.end());

        // Glide between spinning in place (fly_ = 0) and flying (fly_ = 1) instead of jumping.
        const double glide = dt / kGlideSeconds;
        fly_ = std::clamp(fly_ + (flying_ ? glide : -glide), 0.0, 1.0);

        // The trail remembers where the centre has been. Once the donut stops flying it shrinks away.
        if (fly_ > 0) trail_.push_back(centre());
        if (int(trail_.size()) > kTrailLen || (fly_ == 0 && !trail_.empty())) trail_.pop_front();
    }

    void draw(Canvas& cv) override {
        // The camera (see zoom()). The lens and the donut's distance grow together, so the donut
        // still looks the same size, and its centre still lands on the same cell.
        const float zoom = this->zoom();
        const float K1 = focal() * zoom;
        const Vec3 seen = centre();                     // where the donut appears, in "zoom 1" units
        const Vec3 c{seen.x, seen.y, seen.z * zoom};    // where it really is

        // A pulse makes the donut swell: every point moves away from the centre by the factor s.
        const float s = std::clamp(1 + float(swell_), 0.7f, kMaxSwell);

        // How far apart to sample points. Neighbouring points must land less than one cell apart on
        // screen, even on the part of the donut nearest the viewer (z = c.z - R1 - R2, where it looks
        // biggest). Otherwise some cells get skipped and the back of the donut shows through the gaps.
        const float zNearest = c.z - kOuter * s;
        const float thetaStep = std::min(0.07f, 0.9f * zNearest / (K1 * R1 * s));      // around the tube
        const float phiStep = std::min(0.02f, 0.9f * zNearest / (K1 * kOuter * s));    // around the donut

        // phi takes the same values for every theta, so compute their cos/sin once per frame, not
        // once per point. (Trig functions are slow compared to + and *.)
        phiTable_.clear();
        for (float phi = 0; phi < kTwoPi; phi += phiStep) phiTable_.emplace_back(phi);

        std::fill(zbuf_.begin(), zbuf_.end(), 0.0f);
        const Rotation rotA{float(angleA_)}, rotB{float(angleB_)};

        // Fade the colours with distance (fog), once per frame for the whole donut.
        const Color* base = colour_ ? warm_ : plain_;
        const float fade = fog(seen.z);
        Color palette[kNumShades];
        for (int i = 0; i < kNumShades; ++i) palette[i] = lerp(Color{0, 0, 0}, base[i], fade);

        // theta goes around the tube's cross-section (a small circle)...
        for (float theta = 0; theta < kTwoPi; theta += thetaStep) {
            const Rotation t(theta);
            // A point on that small circle, sitting R2 to the right of the centre...
            const Vec3 circlePoint{R2 + R1 * t.c, R1 * t.s, 0};
            // ...and the surface normal there (an arrow pointing straight out of the surface).
            const Vec3 circleNormal{t.c, t.s, 0};

            // ...and phi spins that small circle around the Y axis, sweeping out the whole donut.
            for (const Rotation& p : phiTable_) {
                // Build the donut (rotate by phi), tumble it (rotate by A, then by B), swell it by s, then
                // move it to where it is. The normal is a direction, not a place: it only turns.
                const Vec3 point = s * rotateZ(rotateX(rotateY(circlePoint, p), rotA), rotB) + c;
                const Vec3 normal = rotateZ(rotateX(rotateY(circleNormal, p), rotA), rotB);

                // Perspective projection: things further away (bigger z) look smaller.
                const float ooz = 1.0f / point.z;   // "one over z": bigger means closer to the viewer
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

        if (showTrail_) drawTrail(cv, focal(), zoom);
        for (const Ring& r : rings_) {
            drawRing(cv, focal(), zoom, r.at, r.age, 1.0f);
            drawRing(cv, focal(), zoom, r.at, r.age - kEchoDelay, 0.55f);   // the fainter echo
        }
        if (flying_) {
            char info[96];
            std::snprintf(info, sizeof info, " flying   depth z = %4.1f   looks %3d%% as big as at z = %g", seen.z,
                          int(std::lround(100 * K2 / seen.z)), K2);
            cv.text(0, 0, info, Color{160, 160, 160});
        }
    }

private:
    // K1 converts "x / z" into screen columns. This choice makes the donut fill ~3/4 of the window
    // when it is K2 away. A terminal cell is about twice as tall as it is wide, so the window is
    // "2*h_" columns tall if we measured it in square units. Use whichever direction is smaller.
    float focal() const { return std::min(float(w_), 2.0f * h_) * K2 * 3.0f / (8.0f * kOuter); }

    // The camera's lens. Spinning in place uses the original donut.c camera: close up, with a very
    // wide view (like a phone's 0.5x lens) and strong perspective. But a wide-angle view stretches
    // things near the edges of the picture, and the flying donut goes right up to the edges. So
    // when flying, the camera moves kFlyZoom times further back AND zooms in kFlyZoom times: the
    // donut looks just as big, but the view is narrower and the stretching mostly goes away.
    // Doing both at once while the donut glides out is the film-makers' "dolly zoom".
    float zoom() const { return 1 + (kFlyZoom - 1) * smoothstep(float(fly_)); }

    // Where the donut's centre is now, in "zoom 1" units (only its z is multiplied by zoom()):
    // straight ahead when spinning in place, on the figure-8 when flying, and part-way between
    // while it glides from one to the other.
    Vec3 centre() const {
        const Vec3 still{0, 0, K2};
        return mix(still, flightCentre(flightT_), smoothstep(float(fly_)));
    }

    // The flight path: a figure-8 through space, designed in two parts.
    //   depth   z(t) = middle - swing * cos(2t)   near (big) each time it crosses the middle of the
    //                                             8, far (small) out at the ends of the two loops
    //   screen  u(t) = roomX * sin(t)             columns right of the window's centre
    //           v(t) = roomY * sin(2t)            rows below it. v goes round twice as fast as u,
    //                                             and that is what draws an 8 (a Lissajous curve).
    // "room" is how far the centre can go before the donut touches an edge (see room()). A far-away
    // donut looks smaller, so it can get closer to the edge: the room depends on z.
    // Finally, undo the projection (see draw()) to find the 3D point that appears at (u, v):
    //   sx = w/2 + K1 * x / z     =>     x = u * z / K1
    Vec3 flightCentre(double t) const {
        const float K1 = focal();
        const float z = 0.5f * (kNearZ + kFarZ) - 0.5f * (kFarZ - kNearZ) * float(std::cos(2 * t));
        if (K1 <= 0) return {0, 0, z};
        const float u = room(w_ / 2.0f - 1, K1, z) * float(std::sin(t));
        const float v = room(h_ / 2.0f - 1, K1 * 0.5f, z) * float(std::sin(2 * t));   // rows: y squashed by 0.5
        return {u * z / K1, -2 * v * z / K1, z};   // -2: rows go DOWN, and y was squashed by 0.5
    }

    // How far from the middle (in cells) the donut's centre can be, with the whole donut still on
    // screen, when it is z away and the screen edge is 'edge' cells out. It is easiest in ANGLES, as
    // seen from the camera (which flies with lens kFlyZoom, so the donut is really z*kFlyZoom away):
    //   the edge is at angle E, where tan E = edge / K1           (K1: cells per unit of x/z)
    //   the donut is a ball of radius kOuter, d away; it covers angle B either side of its centre,
    //   where sin B = kOuter / d  (the lines of sight that just touch the ball)
    // so the centre can be at most E - B off to the side, and tan(E - B) = (tanE - tanB) / (1 + tanE tanB).
    static float room(float edge, float K1, float z) {
        const float lens = K1 * kFlyZoom, d = z * kFlyZoom;
        const float tanE = edge / lens;
        const float tanB = kOuter / std::sqrt(d * d - kOuter * kOuter);
        return std::max(0.0f, lens * (tanE - tanB) / (1 + tanE * tanB));
    }

    // The trail: a dot wherever the centre has been, older dots dimmer. The dots go through the same
    // z-buffer test as the donut, so the donut hides the ones behind it, and you can see the newest
    // ones through its hole (the centre of a donut is in the middle of the hole).
    void drawTrail(Canvas& cv, float K1, float zoom) {
        const Color bright = colour_ ? Color{215, 120, 55} : Color{190, 190, 190};
        const int n = int(trail_.size());
        for (int i = 0; i < n; ++i) {
            const Vec3& p = trail_[i];   // in "zoom 1" units, so K1 is the zoom-1 one
            const int sx = int(w_ / 2.0f + K1 * p.x / p.z);
            const int sy = int(h_ / 2.0f - K1 * p.y / p.z * 0.5f);
            if (sx < 0 || sx >= w_ || sy < 0 || sy >= h_) continue;
            if (1 / (p.z * zoom) <= zbuf_[sy * w_ + sx]) continue;   // part of the donut is in front of this dot
            const float newness = float(i + 1) / n;     // ~0 = oldest .. 1 = newest
            const float level = std::round(newness * fog(p.z) * 10) / 10;   // few levels, few colour codes
            cv.set(sx, sy, '.', lerp(Color{0, 0, 0}, bright, level));
        }
    }

    // A pulse, like a drop hitting water: the donut pops (a kick to its spring) and two ripple rings
    // start spreading from where it is. The rings stay there while the donut flies on.
    void pulse() {
        swellV_ = std::max(swellV_, 0.0) + kKick;   // a new kick always pops OUT, even mid-wobble
        rings_.push_back({centre(), 0});
        if (int(rings_.size()) > kMaxRings) rings_.erase(rings_.begin());
    }

    // The swell s = 1 + x behaves like a weight on a spring with damping:
    //     x'' = -w0^2 x - 2 zeta w0 x'
    // The spring pulls it back towards normal size (x = 0) and the damping takes energy out. A pulse
    // flicks the weight (a sudden kick to x'). With zeta = 0.38 it bounces: 1.25x, then 0.93x, then
    // it settles. Each overshoot is exp(-pi zeta / sqrt(1 - zeta^2)) = 0.28 times the one before.
    // This is semi-implicit Euler: change the speed first, then move with the NEW speed. Plain Euler
    // (see pendulum.cpp) would slowly pump energy into the spring; this order doesn't.
    void stepSpring(double h) {
        swellV_ += (-kSpringW0 * kSpringW0 * swell_ - 2 * kSpringZeta * kSpringW0 * swellV_) * h;
        swell_ += swellV_ * h;
    }

    // A ripple ring: a circle around the spot where a pulse happened, facing the camera. It grows like
    // a blast wave, radius ~ t^(2/5): very fast at first, slower and slower as it spreads, and fades.
    // Its dots go through the z-buffer test, so the donut hides any part of a ring behind it.
    void drawRing(Canvas& cv, float K1, float zoom, Vec3 at, double age, float strength) {
        if (age < 0 || age >= kRingSeconds) return;
        const float u = float(age / kRingSeconds);   // 0 = just born .. 1 = gone
        const float radius = kOuter * (1.1f + kRingReach * std::pow(u, 0.4f));   // in 3D units
        const float fade = (1 - u) * (1 - u) * strength * fog(at.z);
        const char ch = fade > 0.45f ? '~' : fade > 0.2f ? '-' : '.';   // the ring thins as it fades
        const Color bright = colour_ ? Color{255, 215, 150} : Color{235, 235, 235};
        const Color col = lerp(Color{0, 0, 0}, bright, std::round(fade * 10) / 10);
        // On screen it is an ellipse, rx columns wide and rx/2 rows tall (cells are twice as tall).
        const float cx = w_ / 2.0f + K1 * at.x / at.z, cy = h_ / 2.0f - K1 * at.y / at.z * 0.5f;
        const float rx = K1 * radius / at.z;
        const float ooz = 1 / (at.z * zoom);
        const int steps = int(kTwoPi * rx / 0.7f) + 8;   // dots about 0.7 of a column apart: no gaps
        for (int i = 0; i < steps; ++i) {
            const float a = kTwoPi * i / steps;
            const int sx = int(cx + rx * std::cos(a)), sy = int(cy - rx * 0.5f * std::sin(a));
            if (sx < 0 || sx >= w_ || sy < 0 || sy >= h_) continue;
            if (ooz <= zbuf_[sy * w_ + sx]) continue;   // part of the donut is in front
            cv.set(sx, sy, ch, col);
        }
    }

    int w_ = 0, h_ = 0;
    std::vector<float> zbuf_;   // 1/z of the nearest point drawn in each cell (0 = nothing yet)
    std::vector<Rotation> phiTable_;
    double angleA_ = 0, angleB_ = 0;
    double speed_ = 1.0;
    bool colour_ = true;
    Color warm_[kNumShades], plain_[kNumShades];

    bool flying_ = true;       // what M switches. fly_ follows it smoothly:
    double fly_ = 0;           // 0 = spinning in place .. 1 = flying (so it glides out at the start)
    double flightT_ = 0;       // where along the figure-8 we are (0..2*pi)
    bool showTrail_ = true;
    std::deque<Vec3> trail_;   // recent centre positions, oldest first

    struct Ring {
        Vec3 at;      // where the pulse happened, in "zoom 1" units like the trail
        double age;   // seconds since then
    };
    std::vector<Ring> rings_;
    double swell_ = 0, swellV_ = 0;   // the pulse's spring: x (0 = normal size) and its speed x'
    double springLeft_ = 0;           // time not yet used by a whole 1 ms spring step
};

}  // namespace

std::unique_ptr<Simulation> makeDonut() { return std::make_unique<Donut>(); }
