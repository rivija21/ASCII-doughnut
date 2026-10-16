#!/bin/sh
# Builds ./ascii-sims for macOS with clang (it comes with Xcode or the Command Line Tools;
# if it's missing, macOS offers to install them the first time you run this).
#   -arch arm64 -arch x86_64   one "universal" program that runs on Apple-chip AND Intel Macs
cd "$(dirname "$0")" || exit 1
c++ -std=c++17 -O2 -Wall -Wextra -arch arm64 -arch x86_64 \
    src/main.cpp src/terminal.cpp src/platform_mac.cpp src/donut.cpp src/matrix.cpp src/fire.cpp src/pendulum.cpp \
    -o ascii-sims || { echo "Build FAILED."; exit 1; }
echo "Built ./ascii-sims"
