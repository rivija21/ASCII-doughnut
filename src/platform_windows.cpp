// platform_windows.cpp - The Windows side of terminal.h (see platform.h for the overview).
// The only file that includes <windows.h>. The macOS version is platform_mac.cpp.
#include "platform.h"
#include "terminal.h"

#ifndef NOMINMAX
#define NOMINMAX        // stop <windows.h> defining min/max macros that break std::min/std::max
#endif
#include <windows.h>
#include <mmsystem.h>   // timeBeginPeriod (needs -lwinmm when linking)
#include <conio.h>      // _kbhit, _getch

#include <atomic>
#include <cctype>
#include <cstdio>
#include <cstdlib>

#ifndef ENABLE_VIRTUAL_TERMINAL_PROCESSING
#define ENABLE_VIRTUAL_TERMINAL_PROCESSING 0x0004
#endif

namespace {

HANDLE hOut = INVALID_HANDLE_VALUE;
DWORD originalMode = 0;
std::atomic<bool> active{false};

// Called by Windows (on another thread) when you press Ctrl+C.
BOOL WINAPI onCtrlC(DWORD) {
    term::restore();
    return FALSE;   // "not handled": Windows then runs its default handler, which ends the program
}

}  // namespace

// ------------------------------------------------------------------ platform.h

void platform::write(const std::string& s) {
    DWORD written = 0;
    WriteFile(hOut, s.data(), DWORD(s.size()), &written, nullptr);
}

void platform::sleepFor(std::chrono::steady_clock::duration d) {
    Sleep(DWORD(std::chrono::duration_cast<std::chrono::milliseconds>(d).count()));
}

bool platform::trueColour() { return true; }   // every Windows 10+ console with ANSI codes does

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
    platform::write("\x1b[?1049h\x1b[?25l\x1b[?7l\x1b[2J");
}

void term::restore() {
    if (!active.exchange(false)) return;   // already restored (e.g. Ctrl+C, then normal exit)
    // reset colour, auto-wrap back on, show cursor, return to the normal screen
    platform::write("\x1b[0m\x1b[?7h\x1b[?25h\x1b[?1049l");
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
