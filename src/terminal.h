// terminal.h - A tiny "graphics library" for drawing animations in a terminal (Windows or macOS).
//
// The simulations never talk to the operating system directly. They only use what is declared here:
//
//   Color, Canvas  - an off-screen grid of coloured characters (a "frame buffer").
//                    Draw a whole frame into it, then call present() once to show it.
//   term::...      - switch the console into animation mode, read keys, get its size.
//   FrameClock     - keeps a steady frame rate and tells you how much time has passed.
//
// terminal.cpp implements the parts that are the same everywhere. The term:: functions are
// different on each system, so they live in platform_windows.cpp and platform_mac.cpp (see
// platform.h). Porting to another system means writing one more of those files.
#pragma once

#include <chrono>
#include <cstdint>
#include <initializer_list>
#include <string>
#include <vector>

// ------------------------------------------------------------------ colours

struct Color {
    uint8_t r = 255, g = 255, b = 255;
};

inline bool operator==(Color a, Color b) { return a.r == b.r && a.g == b.g && a.b == b.b; }
inline bool operator!=(Color a, Color b) { return !(a == b); }

// Blend from a (t = 0) to b (t = 1).
Color lerp(Color a, Color b, float t);
// Blend through several colours: gradient({black, red, yellow}, 0.75f) is orange.
Color gradient(std::initializer_list<Color> stops, float t);
// Hue / saturation / value -> RGB. All inputs are 0..1 (hue wraps around).
Color hsv(float h, float s, float v);

// ------------------------------------------------------------------ canvas

struct Cell {
    char ch = ' ';
    Color fg;
};

class Canvas {
public:
    int width() const { return w_; }
    int height() const { return h_; }

    void resize(int w, int h);
    void clear();                                              // fill with spaces
    void set(int x, int y, char ch, Color fg);                 // off-screen cells are ignored
    void text(int x, int y, const std::string& s, Color fg);
    const Cell& at(int x, int y) const { return cells_[y * w_ + x]; }

    // Turn the whole grid into ONE string of characters + ANSI colour codes and write it.
    void present();

private:
    int w_ = 0, h_ = 0;
    std::vector<Cell> cells_;   // row by row: cell (x, y) lives at cells_[y * w_ + x]
    std::string out_;           // reused every frame so we don't reallocate memory
};

// ------------------------------------------------------------------ console

enum Key {
    KEY_NONE = -1,
    KEY_ESC = 27,
    KEY_UP = 1000,
    KEY_DOWN,
    KEY_LEFT,
    KEY_RIGHT,
};

namespace term {
void init();                       // enter "animation mode": alternate screen, hidden cursor, ANSI on
void restore();                    // put the console back the way we found it
void size(int& cols, int& rows);   // visible size of the console window, in character cells
int readKey();                     // next key press (lower-cased), or KEY_NONE. Never waits.
void clearScreen();
}  // namespace term

// ------------------------------------------------------------------ timing

class FrameClock {
public:
    explicit FrameClock(int fps);
    // Sleep until the next frame is due. Returns the seconds since the previous tick().
    double tick();
    double fps() const { return fps_; }   // measured, smoothed frames per second

private:
    using Clock = std::chrono::steady_clock;
    Clock::duration period_;
    Clock::time_point next_, last_;
    double fps_;
};
