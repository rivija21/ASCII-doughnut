// terminal.cpp - The only Windows-specific file. See terminal.h for the overview.
#include "terminal.h"

#ifndef NOMINMAX
#define NOMINMAX        // stop <windows.h> defining min/max macros that break std::min/std::max
#endif
#include <windows.h>
#include <mmsystem.h>   // timeBeginPeriod (needs -lwinmm when linking)
#include <conio.h>      // _kbhit, _getch

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>

#ifndef ENABLE_VIRTUAL_TERMINAL_PROCESSING
#define ENABLE_VIRTUAL_TERMINAL_PROCESSING 0x0004
#endif

namespace {

HANDLE hOut = INVALID_HANDLE_VALUE;
DWORD originalMode = 0;
std::atomic<bool> active{false};

void writeRaw(const std::string& s) {
    DWORD written = 0;
    WriteFile(hOut, s.data(), DWORD(s.size()), &written, nullptr);
}

// Called by Windows (on another thread) when you press Ctrl+C.
BOOL WINAPI onCtrlC(DWORD) {
    term::restore();
    return FALSE;   // "not handled": Windows then runs its default handler, which ends the program
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
void Canvas::present() {
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
                n = std::snprintf(buf, sizeof buf, "\x1b[38;2;%d;%d;%dm", c.fg.r, c.fg.g, c.fg.b);
                out_.append(buf, n);
                current = c.fg;
                haveColour = true;
            }
            out_ += c.ch;
        }
    }
    out_ += "\x1b[?2026l";
    writeRaw(out_);   // one big write per frame = no flicker
}

// ------------------------------------------------------------------ console

void term::init() {
    hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    if (!GetConsoleMode(hOut, &originalMode)) {
        std::fprintf(stderr, "Not running in a Windows console. Use Windows Terminal, PowerShell or cmd.\n");
        std::exit(1);
    }
    // Ask the console to understand ANSI escape codes (colours, cursor movement...).
    if (!SetConsoleMode(hOut, originalMode | ENABLE_PROCESSED_OUTPUT | ENABLE_VIRTUAL_TERMINAL_PROCESSING)) {
        std::fprintf(stderr, "This console does not support ANSI escape codes. Try Windows Terminal.\n");
        std::exit(1);
    }
    SetConsoleTitleA("ASCII Simulations");
    SetConsoleCtrlHandler(onCtrlC, TRUE);
    timeBeginPeriod(1);   // let Sleep() wake within ~1 ms instead of ~16 ms -> smoother frames
    active = true;
    //   ESC[?1049h  switch to the "alternate screen" (your normal scrollback is left untouched)
    //   ESC[?25l    hide the blinking cursor
    //   ESC[?7l     don't auto-wrap at the right edge
    //   ESC[2J      clear the screen
    writeRaw("\x1b[?1049h\x1b[?25l\x1b[?7l\x1b[2J");
}

void term::restore() {
    if (!active.exchange(false)) return;   // already restored (e.g. Ctrl+C, then normal exit)
    // reset colour, auto-wrap back on, show cursor, return to the normal screen
    writeRaw("\x1b[0m\x1b[?7h\x1b[?25h\x1b[?1049l");
    SetConsoleMode(hOut, originalMode);
    timeEndPeriod(1);
}

void term::size(int& cols, int& rows) {
    CONSOLE_SCREEN_BUFFER_INFO info;
    if (GetConsoleScreenBufferInfo(hOut, &info)) {
        cols = info.srWindow.Right - info.srWindow.Left + 1;
        rows = info.srWindow.Bottom - info.srWindow.Top + 1;
    } else {
        cols = 80;
        rows = 25;
    }
}

int term::readKey() {
    if (!_kbhit()) return KEY_NONE;
    const int k = _getch();
    if (k == 0 || k == 224) {   // arrow keys arrive as two codes: a 0 or 224 prefix, then the key
        switch (_getch()) {
            case 72: return KEY_UP;
            case 80: return KEY_DOWN;
            case 75: return KEY_LEFT;
            case 77: return KEY_RIGHT;
            default: return KEY_NONE;
        }
    }
    return std::tolower(k);
}

void term::clearScreen() { writeRaw("\x1b[2J"); }

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
        Sleep(DWORD(std::chrono::duration_cast<std::chrono::milliseconds>(next_ - now).count()));
    else
        next_ = now;   // running late: don't try to "catch up" with a burst of frames
    const auto t = Clock::now();
    const double dt = std::chrono::duration<double>(t - last_).count();
    last_ = t;
    if (dt > 0) fps_ += (1.0 / dt - fps_) * 0.05;   // smoothed so the number doesn't jitter
    return dt;
}
