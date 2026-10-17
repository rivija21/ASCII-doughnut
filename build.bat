@echo off
rem Builds ascii-sims.exe with g++ (MSYS2 / MinGW-w64).
rem   -static   bundle the C++ runtime so the .exe runs on any Windows PC
rem   -lwinmm   link the Windows multimedia library (for timeBeginPeriod)
cd /d "%~dp0"
g++ -std=c++17 -O2 -Wall -Wextra ^
    src\main.cpp src\terminal.cpp src\platform_windows.cpp src\donut.cpp src\matrix.cpp src\fire.cpp src\pendulum.cpp src\ripples.cpp ^
    -o ascii-sims.exe -static -lwinmm
if errorlevel 1 (
    echo Build FAILED.
    exit /b 1
)
echo Built ascii-sims.exe
