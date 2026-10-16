// platform_mac.cpp - The macOS side of terminal.h (see platform.h for the overview).
// The Windows version of this file is platform_windows.cpp. Everything else is shared.
//
// A Mac terminal (Terminal, iTerm2, ...) already understands ANSI escape codes, so unlike on
// Windows there is nothing to switch on before drawing. The real work is the KEYBOARD.
// Normally a terminal is in "canonical" mode: it shows what you type and only hands a whole
// line to the program when you press Return. An animation wants every key straight away and
// unechoed, so we change the terminal's settings with termios, and put them back at the end.
#include "platform.h"
#include "terminal.h"

#include <poll.h>        // poll: wait a moment for a key
#include <signal.h>      // Ctrl+C, Ctrl+Z, window closed
#include <sys/ioctl.h>   // ioctl(TIOCGWINSZ): the window size
#include <termios.h>     // tcgetattr / tcsetattr: the terminal's settings
#include <unistd.h>      // read, write, isatty

#include <atomic>
#include <cctype>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>

namespace {

termios originalSettings;   // the terminal's settings before we changed them
std::atomic<bool> active{false};
bool rgbColour = true;

// The same escape codes as on Windows (term::init() in platform_windows.cpp explains each one).
const char kEnterCodes[] = "\x1b[?1049h\x1b[?25l\x1b[?7l\x1b[2J";
const char kLeaveCodes[] = "\x1b[0m\x1b[?7h\x1b[?25h\x1b[?1049l";

// write() may send only part of a big frame, or be interrupted by a signal, so keep going until
// everything is out. Only "async-signal-safe" calls (write, tcsetattr, signal, raise...) are used
// from here down to the signal handlers, because those handlers can interrupt the program at
// ANY point, even in the middle of allocating memory. printf or std::string would not be safe.
void writeAll(const char* data, size_t size) {
    while (size > 0) {
        const ssize_t n = ::write(STDOUT_FILENO, data, size);
        if (n < 0) {
            if (errno == EINTR || errno == EAGAIN) continue;
            return;   // the terminal has gone away: nothing sensible left to do
        }
        data += n;
        size -= size_t(n);
    }
}

void enterAnimationMode() {
    termios raw = originalSettings;
    raw.c_lflag &= ~(ICANON | ECHO);   // keys arrive one at a time as they're pressed, not shown
    raw.c_iflag &= ~IXON;              // otherwise Ctrl+S freezes the output until Ctrl+Q
    raw.c_cc[VMIN] = 0;                // read() returns at once, even when no key is waiting
    raw.c_cc[VTIME] = 0;
    tcsetattr(STDIN_FILENO, TCSANOW, &raw);
    writeAll(kEnterCodes, sizeof kEnterCodes - 1);
}

void leaveAnimationMode() {
    writeAll(kLeaveCodes, sizeof kLeaveCodes - 1);
    // TCSAFLUSH also throws away keys nobody read yet, so extra Q presses don't reach the shell.
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &originalSettings);
}

// Ctrl+C (SIGINT), closing the window (SIGHUP) or `kill` (SIGTERM): fix the terminal first,
// then let the signal do what it would have done anyway, which is end the program.
void onQuitSignal(int sig) {
    term::restore();
    signal(sig, SIG_DFL);
    raise(sig);
}

// Ctrl+Z (SIGTSTP) pauses the program and drops you back at the shell prompt, so give the shell
// a normal terminal first. Typing `fg` resumes the program right after raise(); then switch back.
void onSuspend(int) {
    const int savedErrno = errno;
    const bool wasActive = active;
    if (wasActive) leaveAnimationMode();
    signal(SIGTSTP, SIG_DFL);
    sigset_t set;
    sigemptyset(&set);
    sigaddset(&set, SIGTSTP);
    sigprocmask(SIG_UNBLOCK, &set, nullptr);   // a signal is blocked while its own handler runs
    raise(SIGTSTP);                            // the program stops HERE until you type `fg`
    signal(SIGTSTP, onSuspend);
    if (wasActive) enterAnimationMode();       // present() redraws every cell on the next frame
    errno = savedErrno;
}

// Can the terminal show exact 24-bit colours? Most modern ones can, and say so in $COLORTERM.
// Apple's Terminal only learned it in macOS 26; older versions show the wrong colours, so for
// them Canvas::present() picks the nearest of the 256 standard colours instead.
bool detectRgbColour() {
    const char* colorterm = std::getenv("COLORTERM");
    if (colorterm && (std::strstr(colorterm, "truecolor") || std::strstr(colorterm, "24bit"))) return true;
    const char* program = std::getenv("TERM_PROGRAM");
    if (program && std::strcmp(program, "Apple_Terminal") == 0) {
        const char* version = std::getenv("TERM_PROGRAM_VERSION");   // 455.1 on macOS 15, 488 on macOS 27
        return version && std::atoi(version) >= 460;
    }
    return true;
}

int readByte() {
    unsigned char c;
    return ::read(STDIN_FILENO, &c, 1) == 1 ? c : -1;   // -1: nothing waiting
}

// Like readByte(), but gives the rest of a key's escape code up to 20 ms to arrive.
int readByteSoon() {
    pollfd p{STDIN_FILENO, POLLIN, 0};
    if (poll(&p, 1, 20) <= 0) return -1;
    return readByte();
}

}  // namespace

// ------------------------------------------------------------------ platform.h

void platform::write(const std::string& s) { writeAll(s.data(), s.size()); }

void platform::sleepFor(std::chrono::steady_clock::duration d) { std::this_thread::sleep_for(d); }

bool platform::trueColour() { return rgbColour; }

// ------------------------------------------------------------------ console

void term::init() {
    if (!isatty(STDIN_FILENO) || !isatty(STDOUT_FILENO) || tcgetattr(STDIN_FILENO, &originalSettings) != 0) {
        std::fprintf(stderr, "Not running in a terminal. Open the Terminal app and run ./ascii-sims there.\n");
        std::exit(1);
    }
    rgbColour = detectRgbColour();
    signal(SIGINT, onQuitSignal);
    signal(SIGTERM, onQuitSignal);
    signal(SIGHUP, onQuitSignal);
    signal(SIGTSTP, onSuspend);
    active = true;
    enterAnimationMode();
}

void term::restore() {
    if (!active.exchange(false)) return;   // already restored (e.g. Ctrl+C, then normal exit)
    leaveAnimationMode();
}

void term::size(int& cols, int& rows) {
    winsize ws{};
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 0 && ws.ws_row > 0) {
        cols = ws.ws_col;
        rows = ws.ws_row;
    } else {
        cols = 80;
        rows = 24;
    }
}

// Keys arrive as bytes. Letters are one byte each, but the arrow keys send a short "escape code":
//   Up = ESC [ A    Down = ESC [ B    Right = ESC [ C    Left = ESC [ D
// (sometimes ESC O A..D instead). A lone ESC, with nothing after it, is the Esc key itself.
int term::readKey() {
    const int c = readByte();
    if (c < 0) return KEY_NONE;
    if (c != KEY_ESC) return std::tolower(c);

    const int next = readByteSoon();
    if (next != '[' && next != 'O') return KEY_ESC;
    // Skip any numbers and ';' (Shift+Up is ESC [ 1 ; 2 A) up to the final letter.
    int last;
    do {
        last = readByteSoon();
    } while (last >= 0 && (last < 0x40 || last > 0x7e));
    switch (last) {
        case 'A': return KEY_UP;
        case 'B': return KEY_DOWN;
        case 'C': return KEY_RIGHT;
        case 'D': return KEY_LEFT;
        default: return KEY_NONE;   // some other special key (F1, Home...): ignore it
    }
}
