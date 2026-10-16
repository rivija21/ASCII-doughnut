// terminal.cpp - The parts of terminal.h that are the same on every operating system:
//                colours, the Canvas and the frame clock. See terminal.h for the overview.
//
// They need only three things from the operating system, declared in platform.h: write bytes to
// the terminal, sleep, and "can it show exact RGB colours?". platform_windows.cpp and
// platform_mac.cpp provide those (and the rest of term::), one file per system.
#include "terminal.h"
#include "platform.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace {

// Older terminals (like Apple's Terminal before macOS 26) can't show exact RGB colours, only a
// fixed palette of 256: 16 basic colours, a 6x6x6 colour cube (16..231) and 24 greys (232..255).
// Return the palette entry nearest to c.
int nearest256(Color c) {
    static const int kLevel[6] = {0, 95, 135, 175, 215, 255};   // the 6 values a cube channel can take
    auto cubeStep = [](int v) { return v < 48 ? 0 : v < 115 ? 1 : (v - 35) / 40; };   // nearest level
    const int r = cubeStep(c.r), g = cubeStep(c.g), b = cubeStep(c.b);
    const int grey = std::clamp(((c.r + c.g + c.b) / 3 - 3) / 10, 0, 23);   // greys are 8, 18, .. 238
    auto distance = [c](int R, int G, int B) {
        return (R - c.r) * (R - c.r) + (G - c.g) * (G - c.g) + (B - c.b) * (B - c.b);
    };
    const int greyValue = 8 + 10 * grey;
    return distance(kLevel[r], kLevel[g], kLevel[b]) <= distance(greyValue, greyValue, greyValue)
               ? 16 + 36 * r + 6 * g + b
               : 232 + grey;
}

}  // namespace

// ------------------------------------------------------------------ colours

Color lerp(Color a, Color b, float t) {
    t = std::clamp(t, 0.0f, 1.0f);
    auto mix = [t](uint8_t x, uint8_t y) { return uint8_t(x + (y - x) * t + 0.5f); };
    return {mix(a.r, b.r), mix(a.g, b.g), mix(a.b, b.b)};
}

Color gradient(std::initializer_list<Color> stops, float t) {
    const int n = int(stops.size());
    if (n == 1) return *stops.begin();
    // Find which pair of neighbouring stops t falls between, then blend those two.
    const float pos = std::clamp(t, 0.0f, 1.0f) * (n - 1);
    const int i = std::min(int(pos), n - 2);
    return lerp(stops.begin()[i], stops.begin()[i + 1], pos - i);
}

Color hsv(float h, float s, float v) {
    h = (h - std::floor(h)) * 6;   // 0..6: which sixth of the colour wheel we're in
    const int i = int(h);
    const float f = h - i;
    const float p = v * (1 - s), q = v * (1 - s * f), t = v * (1 - s * (1 - f));
    float r, g, b;
    switch (i) {
        case 0:  r = v; g = t; b = p; break;
        case 1:  r = q; g = v; b = p; break;
        case 2:  r = p; g = v; b = t; break;
        case 3:  r = p; g = q; b = v; break;
        case 4:  r = t; g = p; b = v; break;
        default: r = v; g = p; b = q; break;
    }
    return {uint8_t(r * 255), uint8_t(g * 255), uint8_t(b * 255)};
}

// ------------------------------------------------------------------ canvas

void Canvas::resize(int w, int h) {
    w_ = std::max(w, 0);
    h_ = std::max(h, 0);
    cells_.assign(size_t(w_) * h_, Cell{});
}

void Canvas::clear() { std::fill(cells_.begin(), cells_.end(), Cell{}); }

void Canvas::set(int x, int y, char ch, Color fg) {
    if (x < 0 || y < 0 || x >= w_ || y >= h_) return;
    cells_[y * w_ + x] = {ch, fg};
}

void Canvas::text(int x, int y, const std::string& s, Color fg) {
    for (size_t i = 0; i < s.size(); ++i) set(x + int(i), y, s[i], fg);
}

// ANSI escape codes used below. "\x1b" is the ESC character (ASCII 27):
//   ESC[?2026h ... ESC[?2026l   "synchronised update": the terminal shows the frame all at once
//   ESC[<row>;<col>H            move the cursor (rows and columns start at 1)
//   ESC[38;2;<r>;<g>;<b>m       set the text colour to an exact RGB value ("true colour")
//   ESC[38;5;<n>m               set the text colour to entry n of the 256-colour palette
void Canvas::present() {
    const bool rgb = platform::trueColour();
    out_.clear();
    out_ += "\x1b[?2026h";
    char buf[32];
    bool haveColour = false;
    Color current;
    for (int y = 0; y < h_; ++y) {
        int n = std::snprintf(buf, sizeof buf, "\x1b[%d;1H", y + 1);
        out_.append(buf, n);
        for (int x = 0; x < w_; ++x) {
            // Writing the bottom-right cell makes some consoles scroll everything up a line.
            if (y == h_ - 1 && x == w_ - 1) break;
            const Cell& c = cells_[y * w_ + x];
            // Only send a colour code when it changes. Spaces are invisible, so they never need one.
            if (c.ch != ' ' && (!haveColour || c.fg != current)) {
                n = rgb ? std::snprintf(buf, sizeof buf, "\x1b[38;2;%d;%d;%dm", c.fg.r, c.fg.g, c.fg.b)
                        : std::snprintf(buf, sizeof buf, "\x1b[38;5;%dm", nearest256(c.fg));
                out_.append(buf, n);
                current = c.fg;
                haveColour = true;
            }
            out_ += c.ch;
        }
    }
    out_ += "\x1b[?2026l";
    platform::write(out_);   // one big write per frame = no flicker
}

// ------------------------------------------------------------------ console
// term::init, restore, size and readKey are different on every system: see platform_*.cpp.

void term::clearScreen() { platform::write("\x1b[2J"); }

// ------------------------------------------------------------------ timing

FrameClock::FrameClock(int fps)
    : period_(std::chrono::duration_cast<Clock::duration>(std::chrono::duration<double>(1.0 / fps))),
      next_(Clock::now()),
      last_(next_),
      fps_(fps) {}

double FrameClock::tick() {
    next_ += period_;
    const auto now = Clock::now();
    if (next_ > now)
        platform::sleepFor(next_ - now);
    else
        next_ = now;   // running late: don't try to "catch up" with a burst of frames
    const auto t = Clock::now();
    const double dt = std::chrono::duration<double>(t - last_).count();
    last_ = t;
    if (dt > 0) fps_ += (1.0 / dt - fps_) * 0.05;   // smoothed so the number doesn't jitter
    return dt;
}
