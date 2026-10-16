// main.cpp - The menu, plus the animation loop shared by every simulation.
//
// Build from the project folder with build.bat (Windows) or ./build.sh (macOS).

#include "sim.h"

#include <algorithm>
#include <cmath>
#include <string>

namespace {

constexpr int kFps = 30;
constexpr int kMinWidth = 40, kMinHeight = 12;
const Color kGrey{140, 140, 140};
const Color kWhite{235, 235, 235};

// Title in the FIGlet "standard" font.
const char* const kTitle[] = {
    R"(    _     ____    ____  ___  ___     ____   ___  __  __  ____  )",
    R"(   / \   / ___|  / ___||_ _||_ _|   / ___| |_ _||  \/  |/ ___| )",
    R"(  / _ \  \___ \ | |     | |  | |    \___ \  | | | |\/| |\___ \ )",
    R"( / ___ \  ___) || |___  | |  | |     ___) | | | | |  | | ___) |)",
    R"(/_/   \_\|____/  \____||___||___|   |____/ |___||_|  |_||____/ )",
};
constexpr int kTitleRows = 5;
constexpr int kTitleWidth = 63;

struct MenuItem {
    char key;
    const char* title;
    const char* teaches;
    std::unique_ptr<Simulation> (*make)();   // pointer to a factory function from sim.h
};

const MenuItem kItems[] = {
    {'1', "Flying 3D donut", "3D rotation, perspective, z-buffer, lighting", makeDonut},
    {'2', "Matrix digital rain", "particles, fading trails, colour themes", makeMatrix},
    {'3', "Doom fire", "cellular automaton, fixed time step, palettes", makeFire},
    {'4', "Double pendulum", "physics, RK4 integration, chaos", makePendulum},
};

bool tooSmall(int w, int h) { return w < kMinWidth || h < kMinHeight; }

void drawTooSmall(Canvas& cv) {
    cv.text(1, 1, "Window too small - make it bigger.", kWhite);
}

void drawStatusBar(Canvas& cv, const Simulation& sim, double fps, bool paused) {
    // PAUSED goes first so it's still visible when a narrow window cuts off the end of the line.
    const std::string s = std::string(paused ? " PAUSED |" : "") + " " + sim.name() + " | " + sim.help() +
                          " | P pause  Q menu | " + std::to_string(int(std::lround(fps))) + " fps";
    cv.text(0, cv.height() - 1, s, paused ? Color{255, 200, 60} : kGrey);
}

// The game loop. Runs one simulation until the user presses Q or Esc.
void runSimulation(Simulation& sim) {
    Canvas cv;
    FrameClock clock(kFps);
    int lastW = -1, lastH = -1;
    bool paused = false;
    double dt = 0;

    while (true) {
        // 1. Handle a resized window.
        int w, h;
        term::size(w, h);
        if (w != lastW || h != lastH) {
            lastW = w;
            lastH = h;
            cv.resize(w, h);
            if (!tooSmall(w, h)) sim.resize(w, h - 1);   // the last row is the status bar
            term::clearScreen();
        }

        // 2. Input.
        for (int k = term::readKey(); k != KEY_NONE; k = term::readKey()) {
            if (k == 'q' || k == KEY_ESC) return;
            if (k == 'p')
                paused = !paused;
            else
                sim.onKey(k);
        }

        // 3. Update, 4. draw, 5. show.
        cv.clear();
        if (tooSmall(w, h)) {
            drawTooSmall(cv);
        } else {
            // Clamp dt: if the window was dragged and the program stalled for a second,
            // we don't want the physics to jump a whole second in one go.
            if (!paused) sim.update(std::min(dt, 0.1));
            sim.draw(cv);
            drawStatusBar(cv, sim, clock.fps(), paused);
        }
        cv.present();

        // 6. Wait for the next frame.
        dt = clock.tick();
    }
}

// Shows the menu. Returns the chosen item, or nullptr to quit.
const MenuItem* menu() {
    Canvas cv;
    FrameClock clock(kFps);
    int lastW = -1, lastH = -1;
    double time = 0;

    while (true) {
        int w, h;
        term::size(w, h);
        if (w != lastW || h != lastH) {
            lastW = w;
            lastH = h;
            cv.resize(w, h);
            term::clearScreen();
        }

        for (int k = term::readKey(); k != KEY_NONE; k = term::readKey()) {
            if (k == 'q' || k == KEY_ESC) return nullptr;
            for (const MenuItem& item : kItems)
                if (k == item.key) return &item;
        }

        cv.clear();
        const int contentHeight = kTitleRows + 11;
        int y = std::max(0, (h - contentHeight) / 2);
        const int titleX = std::max(0, (w - kTitleWidth) / 2);

        // Rainbow title: the hue depends on position AND time, so the colours slide sideways.
        for (int row = 0; row < kTitleRows; ++row) {
            for (int col = 0; kTitle[row][col] != '\0'; ++col) {
                const float hue = float(time * 0.15 + col * 0.012 + row * 0.03);
                cv.set(titleX + col, y + row, kTitle[row][col], hsv(hue, 0.55f, 1.0f));
            }
        }
        y += kTitleRows + 1;

        const std::string subtitle = "terminal animations made of plain characters";
        cv.text(std::max(0, (w - int(subtitle.size())) / 2), y, subtitle, kGrey);
        y += 2;

        const int listX = std::max(0, (w - 72) / 2);
        for (const MenuItem& item : kItems) {
            cv.text(listX, y, std::string("[") + item.key + "]", Color{255, 200, 60});
            cv.text(listX + 5, y, item.title, kWhite);
            cv.text(listX + 28, y, item.teaches, kGrey);
            ++y;
        }
        cv.text(listX, y, "[Q]", Color{255, 200, 60});
        cv.text(listX + 5, y, "Quit", kWhite);
        y += 2;
        cv.text(listX, y, "Tip: make the window bigger, or the text smaller, for more detail.", kGrey);

        cv.present();
        time += clock.tick();
    }
}

}  // namespace

int main() {
    term::init();
    while (const MenuItem* item = menu()) {
        std::unique_ptr<Simulation> sim = item->make();
        runSimulation(*sim);
    }
    term::restore();
}
