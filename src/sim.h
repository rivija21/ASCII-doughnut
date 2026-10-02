// sim.h - What every simulation looks like from the outside.
#pragma once

#include "terminal.h"

#include <memory>
#include <random>

// Every animation follows the same "game loop" (see runSimulation() in main.cpp):
//
//     resize(w, h)        once at the start, and again whenever the window changes size
//     every frame:
//         onKey(key)      for each key pressed since the last frame
//         update(dt)      move the simulation forward by dt seconds   (changes the state)
//         draw(canvas)    paint the current state onto the canvas     (only looks at the state)
//
// main.cpp only knows about this interface. It never needs to know what a donut or a
// pendulum is, so adding a new animation means writing one new class.
class Simulation {
public:
    virtual ~Simulation() = default;
    virtual const char* name() const = 0;
    virtual const char* help() const = 0;   // key hints shown in the status bar
    virtual void resize(int w, int h) = 0;
    virtual void onKey(int key) = 0;
    virtual void update(double dt) = 0;
    virtual void draw(Canvas& cv) = 0;
};

// "Factory" functions: each .cpp file hides its class and only exposes a way to create one.
std::unique_ptr<Simulation> makeDonut();
std::unique_ptr<Simulation> makeMatrix();
std::unique_ptr<Simulation> makeFire();
std::unique_ptr<Simulation> makePendulum();

// Small convenience wrapper around the C++ <random> library.
class Random {
public:
    float uniform(float lo, float hi) { return std::uniform_real_distribution<float>(lo, hi)(gen_); }
    int integer(int lo, int hi) { return std::uniform_int_distribution<int>(lo, hi)(gen_); }   // lo..hi inclusive

private:
    std::mt19937 gen_{std::random_device{}()};
};
