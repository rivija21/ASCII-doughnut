// platform.h - The few things terminal.cpp needs from the operating system.
//
// Each operating system has its own file that implements these three functions, plus
// term::init(), term::restore(), term::size() and term::readKey() from terminal.h:
//
//   platform_windows.cpp   Windows console: WriteFile, _getch, Sleep...      (built by build.bat)
//   platform_mac.cpp       macOS terminal:  write, termios, signals...       (built by build.sh)
//
// The build script compiles exactly one of them. Every other file is the same on both systems.
#pragma once

#include <chrono>
#include <string>

namespace platform {
void write(const std::string& s);                       // send bytes to the terminal (all of them)
void sleepFor(std::chrono::steady_clock::duration d);   // do nothing for (about) d
bool trueColour();   // does the terminal understand exact RGB colours (ESC[38;2;r;g;bm)?
}  // namespace platform
