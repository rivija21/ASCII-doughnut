# ASCII Sims

Four animations drawn with plain text characters in the Windows terminal, written in C++17:

| # | Simulation | What you see | Main idea it teaches |
|---|---|---|---|
| 1 | **Spinning 3D donut** | A shaded torus tumbling in 3D | 3D rotation, perspective projection, z-buffer, lighting |
| 2 | **Matrix digital rain** | Green streams of falling characters | Particles, fading trails, colour |
| 3 | **Doom fire** | Flames rising from the bottom | Cellular automaton, fixed time step, palettes |
| 4 | **Double pendulum** | A chaotic pendulum with a glowing trail | Differential equations, RK4 integration, chaos |

This README is a study guide. It explains how the program works and the maths and C++ behind it,
so you can come back later and understand every line.

---

## 1. Build and run

You need `g++` from MSYS2 (you already have it in `C:\msys64\ucrt64\bin`).

From the project folder:

```powershell
.\build.bat
.\ascii-sims.exe
```

`build.bat` runs this command, which you can also type yourself:

```powershell
g++ -std=c++17 -O2 -Wall -Wextra src\main.cpp src\terminal.cpp src\donut.cpp src\matrix.cpp src\fire.cpp src\pendulum.cpp -o ascii-sims.exe -static -lwinmm
```

| Flag | Meaning |
|---|---|
| `-std=c++17` | Use the C++17 language standard (needed for `std::clamp` and more) |
| `-O2` | Optimise: the donut computes ~50,000 points per frame, so speed matters |
| `-Wall -Wextra` | Turn on helpful warnings |
| `-static` | Put the C++ runtime inside the `.exe`, so it runs on PCs without MSYS2 |
| `-lwinmm` | Link the Windows multimedia library, for `timeBeginPeriod` (see §3.5) |

**Where to run it:** Windows Terminal, PowerShell or cmd. Git Bash's own window (mintty) is not a
real Windows console. Either run `winpty ./ascii-sims.exe` there, or use one of the others.

**Tip:** maximise the window, or zoom out with `Ctrl` + `-`. More cells means more detail.

## 2. Controls

| Where | Key | Action |
|---|---|---|
| Menu | `1`–`4` | Start a simulation |
| Menu | `Q` / `Esc` | Quit |
| Any simulation | `P` | Pause / resume |
| Any simulation | `Q` / `Esc` | Back to the menu |
| Donut | `+` / `-` | Spin faster / slower |
| Donut | `C` | Colour on / off |
| Matrix | `+` / `-` | Rain faster / slower |
| Matrix | `C` | Next colour theme (green, cyan, amber, pink) |
| Fire | `Space` | Fuel on / off (watch the fire die down and relight) |
| Fire | `↑` / `↓` | Taller / shorter flames |
| Fire | `←` / `→` | Wind |
| Pendulum | `R` | New random swing |
| Pendulum | `T` | Add or remove a "twin" pendulum that starts 0.001 rad away |
| Pendulum | `I` | Switch integrator: RK4 ↔ Euler (watch the energy) |
| Pendulum | `+` / `-` | Simulation speed ×2 / ÷2 |

---

## 3. How the program is organised

```
doughnut/
├── build.bat            one-click build
├── README.md            this file
└── src/
    ├── sim.h            the Simulation interface every animation implements, plus a Random helper
    ├── main.cpp         the menu and the game loop (runSimulation)
    ├── terminal.h/.cpp  Canvas (frame buffer), colours, keyboard, timing; the ONLY Windows-specific code
    ├── donut.cpp        simulation 1
    ├── matrix.cpp       simulation 2
    ├── fire.cpp         simulation 3
    └── pendulum.cpp     simulation 4
```

**Suggested reading order:** `sim.h` → `main.cpp` → `terminal.h` → `matrix.cpp` (the simplest) →
`fire.cpp` → `donut.cpp` → `pendulum.cpp` → `terminal.cpp` (the low-level details).

### 3.1 The big picture

```
            ┌────────────────────── main.cpp: runSimulation() ───────────────────────┐
            │                                                                         │
 keyboard ──┼─> onKey()  ──>  update(dt)  ──>  draw(canvas)  ──>  canvas.present()  ──┼──> console
            │               (change state)    (look at state,     (one big string     │
            │                                  paint characters)   of text + ANSI)    │
            │                    ^                                                    │
            │                    └──────────── FrameClock.tick() (sleep ~33 ms) <─────┘
            └─────────────────────────────────────────────────────────────────────────┘
```

Every animation, in every game engine, is the same loop: **read input → update → draw → wait → repeat**.
At 30 frames per second, the loop runs every 33 ms.

### 3.2 The `Simulation` interface ([src/sim.h](src/sim.h))

```cpp
class Simulation {
public:
    virtual const char* name() const = 0;
    virtual const char* help() const = 0;
    virtual void resize(int w, int h) = 0;   // window size changed
    virtual void onKey(int key) = 0;         // a key was pressed
    virtual void update(double dt) = 0;      // advance time by dt seconds
    virtual void draw(Canvas& cv) = 0;       // paint the current state
};
```

`= 0` makes these **pure virtual** functions: `Simulation` is only a promise ("anything that is a
Simulation can do these things"). `Donut`, `MatrixRain`, `Fire` and `DoublePendulum` each **inherit**
from it and **override** the functions. `runSimulation(Simulation& sim)` in `main.cpp` calls
`sim.update(dt)` without knowing which kind it has. C++ calls the right version at run time.
This is **polymorphism**.

Each `.cpp` file hides its class inside an **anonymous namespace** (`namespace { ... }`), which makes
it invisible to other files. It only exposes a **factory function** such as
`std::unique_ptr<Simulation> makeDonut()`. The `unique_ptr` owns the object and deletes it
automatically when you go back to the menu.

### 3.3 Why `update` and `draw` are separate

- `update(dt)` changes the state (angles, heat, positions). It knows nothing about characters.
- `draw()` only reads the state and turns it into characters. It never changes the physics.

Because of this split, pausing is simply "don't call `update`", and the physics doesn't depend on
how often the screen is redrawn.

### 3.4 `dt` and the fixed time step

`dt` is how many seconds really passed since the last frame (about 0.033 s, but it varies).
Two ways to use it:

1. **Variable step:** `angle += speed * dt`. Simple and fine for the donut and the Matrix rain.
2. **Fixed step** (fire and pendulum): physics must always advance in *equal* steps, because the fire
   rule and the RK4 maths assume a constant step. So we save time up and spend it in fixed chunks:

```cpp
leftover_ += dt;
while (leftover_ >= kStep) {   // e.g. kStep = 0.002 s for the pendulum
    leftover_ -= kStep;
    step();                    // always exactly kStep
}
```

The loop in `main.cpp` also caps `dt` at 0.1 s. If you drag the window and the program stalls for
a second, the physics doesn't then jump a whole second in one go.

### 3.5 Drawing in a terminal ([src/terminal.h](src/terminal.h), [src/terminal.cpp](src/terminal.cpp))

**Characters are pixels.** Each terminal cell holds one character in one colour. A cell is about
**twice as tall as it is wide**, so every simulation squashes its y-coordinates by ½ (or stretches x by 2).
Otherwise circles come out as tall ovals.

**The Canvas is a frame buffer.** `Canvas` is a `std::vector<Cell>` of size `w*h`. Cell `(x, y)` lives at
index `y * w + x`, one flat array stored row by row. A simulation draws the whole frame into it,
then `present()` turns it into **one string** and writes it with **one** `WriteFile` call.
If we printed each character straight to the screen, you would see half-finished frames (flicker).

**ANSI escape codes** are special text sequences starting with the ESC character (`\x1b`, ASCII 27).
The terminal treats them as commands instead of printing them:

| Sequence | Meaning |
|---|---|
| `ESC[?1049h` / `ESC[?1049l` | Switch to / from the **alternate screen** (how `vim` leaves your scrollback untouched) |
| `ESC[?25l` / `ESC[?25h` | Hide / show the cursor |
| `ESC[?7l` / `ESC[?7h` | Turn auto-wrap at the right edge off / on |
| `ESC[2J` | Clear the screen |
| `ESC[row;colH` | Move the cursor (1-based) |
| `ESC[38;2;R;G;Bm` | Set text colour to an exact RGB value ("true colour") |
| `ESC[0m` | Reset colours |
| `ESC[?2026h` / `ESC[?2026l` | Begin / end a "synchronised update" (terminals that don't know it ignore it) |

Windows only understands these after `SetConsoleMode(..., ENABLE_VIRTUAL_TERMINAL_PROCESSING)`, which
`term::init()` turns on.

**Small optimisations in `present()`:**
- A colour code is sent only when the colour *changes*, and never for spaces (they're invisible).
- Simulations **quantise** colours: they round them to 12–32 fixed levels, so neighbouring
  cells often share exactly the same colour.
- The bottom-right cell is skipped, because writing there makes some consoles scroll everything up a line.

**Cleaning up.** `term::restore()` undoes everything (normal screen, cursor visible, colours reset).
Pressing Ctrl+C would normally kill the program before that runs, leaving your terminal broken.
`SetConsoleCtrlHandler(onCtrlC, TRUE)` registers a function that Windows calls on Ctrl+C, and it
restores the terminal first.

**Keyboard.** `_kbhit()` ("is a key waiting?") and `_getch()` ("give me that key") come from `<conio.h>`,
the same as in your `snake.cpp`. Arrow keys arrive as two codes: `224` (or `0`) and then `72/80/75/77`.

**Timing.** `FrameClock::tick()` sleeps until the next frame is due. Windows' `Sleep()` normally
wakes only every ~15.6 ms, so asking for a 10 ms sleep can actually take 15.6 ms and frames
stutter. `timeBeginPeriod(1)` asks for 1 ms precision while the program runs.

---

## 4. Simulation 1: the spinning donut ([src/donut.cpp](src/donut.cpp))

Based on Andy Sloane's famous `donut.c` (2006). Steps to draw one frame:

### 4.1 Build a torus out of points

Take a small circle of radius `R1 = 1`, centred `R2 = 2` from the Y axis. Spin that circle around
the Y axis and it sweeps out a donut.

```
          Y                          side view of the tube:
          |   .-~-.                       θ goes around the small circle
          |  /     \  <- small circle     φ spins the circle around the Y axis
          |  \     /     radius R1
          |   '-.-'
          +-----------> X
          |<-R2->|
```

```
point on the small circle:   (R2 + R1·cos θ,  R1·sin θ,  0)
surface normal there:        (cos θ,  sin θ,  0)       ← arrow pointing straight out of the surface
```

The code loops `θ` from 0 to 2π (around the tube) and, inside that, `φ` from 0 to 2π (around the donut).

### 4.2 Rotate it

Every rotation is a 2D rotation of two of the coordinates. For example, around the X axis by angle `A`:

```
x' = x
y' = y·cos A − z·sin A          which is the matrix   | 1    0       0    |
z' = y·sin A + z·cos A                                | 0  cos A  −sin A  |
                                                      | 0  sin A   cos A  |
```

Each point is rotated around **Y by φ** (to build the donut), then around **X by A**, then
around **Z by B** (to tumble it). `A` and `B` grow over time at different speeds (1.2 and 0.6 rad/s),
so the motion never repeats exactly. The **normal** gets the same rotations, because it's attached
to the surface.

`Rotation` stores `cos` and `sin` together, because computing them is slow compared to `+` and `*`.
The `φ` values are the same for every `θ`, so their cos/sin are computed once per frame into
`phiTable_`.

### 4.3 Project it onto the screen (perspective)

The viewer's eye is at the origin looking along +z, and the donut is pushed `K2 = 5` units away.
By similar triangles, a point at `(x, y, z)` appears on the screen at:

```
screenX = w/2 + K1 · x / z
screenY = h/2 − K1 · y / z · 0.5      ← minus because screen rows go DOWN; 0.5 for tall cells
```

Dividing by `z` is what makes far things look small. `K1` is a zoom factor chosen so the donut fills
about ¾ of the window: `K1 = size · K2 · 3 / (8 · (R1 + R2))`.

### 4.4 Hide the back: the z-buffer

Many points land in the same cell, for example the front and the back of the donut. Only the
**nearest** one should show. `zbuf_` stores, for each cell, `1/z` of the nearest point drawn so far.
We store `1/z` instead of `z` because then "nothing drawn yet" can simply be `0` (infinitely far),
and bigger means closer:

```cpp
if (ooz <= nearest) continue;   // something closer is already here
nearest = ooz;                  // otherwise this point wins the cell
```

Every 3D game does this, on the graphics card, for every pixel.

### 4.5 Light it

How bright a surface looks depends on how directly it faces the light (Lambert's cosine law).
For unit vectors, the cosine of the angle between them is the **dot product**:

```
light direction  l = (0, 1, −1) / √2       (up, and towards the viewer)
luminance        L = n · l = (n.y − n.z) / √2      → −1 (facing away) … +1 (facing the light)
```

`L` then picks a character from `".,-~:;=!*#$@"`. These are ordered from least ink to most ink,
so the text looks darker or brighter. Faces pointing away from the light get `.`.

### 4.6 A bug worth understanding: sampling holes

The first version used fixed step sizes (`θ` += 0.07, `φ` += 0.02). When the donut tumbled
toward you, a **vertical stripe of dark characters** appeared across its bright front. The
nearest part of the donut is at `z = K2 − R1 − R2 = 2`, so it's projected 2.5× bigger than the
centre (`z = 5`). Neighbouring sample points then landed **more than one cell apart**. Some
cells got no front points at all, and the z-buffer showed the shadowed back of the donut through the gap.

The fix computes the step sizes from the worst case: neighbouring points must be less than 0.9 cell apart
even at `z = 2`:

```cpp
thetaStep = min(0.07, 0.9 · zNearest / (K1 · R1));          // around the tube
phiStep   = min(0.02, 0.9 · zNearest / (K1 · (R1 + R2)));   // around the donut
```

**Lesson:** when you sample a continuous surface, the samples must be denser than the pixels where
the surface looks biggest. A test that counted dark cells with bright cells on both sides found
395 such holes in 600 frames at 100×29 (15,346 at 210×55), and 0 after the fix.

---

## 5. Simulation 2: Matrix rain ([src/matrix.cpp](src/matrix.cpp))

The simplest of the four. It has two layers of state:

```
drops_            one Drop per column: { y (row of the head), speed (rows/second) }
glyph_, bright_   for every cell: which character it shows, and how bright it is (0..1)
```

Each `update(dt)`:

1. **Everything fades:** `bright -= 1.0 · dt`. A lit cell disappears after about 1 second.
2. **Drops fall:** `y += speed · dt`. Every row the head moves *into* gets a new random character
   at brightness 1. The code loops over all rows between the old and new head position, so a
   fast drop never skips cells (the same "don't skip cells" lesson as the donut).
3. When a drop falls off the bottom, it **respawns** above the top at a random height. How far
   above it starts decides how long that column stays dark.
4. A few random cells change character, so the trails shimmer.

The trail is not stored anywhere as a "trail". It is simply the cells the head lit recently that
haven't faded yet. Its length is `speed / fade` rows, so fast drops leave long trails.

In `draw()`, brightness is rounded to 16 levels and blended from the theme's dark colour to its
bright colour with `lerp`. The head of each drop is drawn almost white.

`lerp(a, b, t) = a + (b − a)·t` ("linear interpolation") blends between two values. You'll find it
in almost every graphics program.

---

## 6. Simulation 3: Doom fire ([src/fire.cpp](src/fire.cpp))

The effect from the PlayStation version of Doom (1995), described in Fabien Sanglard's article.

The screen is a grid of **heat** values from 0 (cold) to 1 (white-hot). The whole algorithm:

```
bottom row = fuel = 1.0 (full heat)
for every cell (x, y), except the top row:
    dx    = random −1, 0 or +1  (+ an extra ±1 sometimes, if there is wind)
    above = cell (x + dx, y − 1)
    heat[above] = heat[x, y] − random(0 … 2·cooling)
```

Heat moves **up** one row per step, loses a random amount, and wobbles sideways. The result looks
like flames. A rule where each cell's new value depends only on nearby cells is called a
**cellular automaton**. Conway's Game of Life is another one.

**Flame height.** Each row loses `cooling` heat on average, so heat starting at 1.0 reaches 0 after
about `1 / cooling` rows. To make flames reach `heightFrac` of the window, the code uses
`cooling = 1 / (heightFrac · h)`. `↑`/`↓` change `heightFrac`.

**Why one array is enough.** Normally a grid update needs two arrays (old and new), so you don't
read values you've already changed in this step. Here row `y` writes only into row `y − 1`, and we go
**top to bottom**. When we reach row `y`, nothing has written to it yet in this step (row `y + 1` comes later).
So every read gets last step's value.

**Fuel off.** `Space` makes the bottom row cool down gradually instead of staying at 1, and the
fire dies from the bottom up, just like on the Doom title screen.

**Rendering.** Heat picks both a character (`" .:-=+*#%@"`) and a colour from a 32-entry palette:
black → dark red → orange → yellow → white. The palette is built **once**, in the constructor,
with `gradient()`. Looking up a precomputed table is much cheaper than recomputing colours for
every cell on every frame.

**Fixed time step.** The fire always updates 40 times per second (§3.4), whatever the frame rate.
On a slow PC the flames still rise at the same speed.

---

## 7. Simulation 4: the double pendulum ([src/pendulum.cpp](src/pendulum.cpp))

### 7.1 State

Everything about the pendulum at one instant fits in four numbers:

```cpp
struct State { double th1, th2;   // angles from straight down (rad)
               double w1,  w2; }; // angular velocities (rad/s)
```

### 7.2 Equations of motion

Lagrangian mechanics gives the angular accelerations (with `Δ = θ1 − θ2`):

```
        −g(2m1+m2)·sin θ1 − m2·g·sin(θ1 − 2θ2) − 2·sin Δ·m2·(ω2²·L2 + ω1²·L1·cos Δ)
θ1'' = ──────────────────────────────────────────────────────────────────────────────
                          L1 · (2m1 + m2 − m2·cos 2Δ)

        2·sin Δ · (ω1²·L1·(m1+m2) + g·(m1+m2)·cos θ1 + ω2²·L2·m2·cos Δ)
θ2'' = ─────────────────────────────────────────────────────────────────
                          L2 · (2m1 + m2 − m2·cos 2Δ)
```

`derivative(s)` returns how fast each part of the state is changing: `(ω1, ω2, θ1'', θ2'')`.
The angles change at the angular velocities, and the velocities change at the accelerations.

These equations have no formula for "where is the pendulum at time t". The only way to find out is
to step forward in tiny time steps. That is **numerical integration**.

### 7.3 Euler vs RK4

**Explicit Euler:** "keep going in the direction you're heading now"

```
s_next = s + h · f(s)
```

The slope is only measured at the *start* of the step, and the error always leans the same way.
Here that means the energy slowly increases. Press `I` and watch the `drift` number climb until
the pendulum goes wild. (If it blows up to infinity, the simulation resets itself.)

**Runge–Kutta 4 (RK4):** measure the slope four times across the step, then take a weighted average:

```
k1 = f(s)                      slope at the start
k2 = f(s + h/2 · k1)           slope at the middle (using k1 to get there)
k3 = f(s + h/2 · k2)           slope at the middle again (better guess, using k2)
k4 = f(s + h   · k3)           slope at the end
s_next = s + h/6 · (k1 + 2·k2 + 2·k3 + k4)
```

If you halve the step `h`, Euler's error halves, but RK4's error shrinks **16×** (it is "4th order").
That's why RK4 is the standard choice for this kind of problem.

The code steps `h = 0.002 s` (500 steps per simulated second). The `operator+` and `operator*`
defined for `State` let these formulas be written in C++ almost exactly as above.

### 7.4 Energy as a correctness test

There is no friction, so the total energy (kinetic + potential) must stay constant forever:

```
T = ½·m1·L1²·ω1² + ½·m2·(L1²·ω1² + L2²·ω2² + 2·L1·L2·ω1·ω2·cos Δ)
V = −(m1+m2)·g·L1·cos θ1 − m2·g·L2·cos θ2
```

The top line shows `drift = E(now) − E(start)`. In a test run over 20 simulated seconds,
RK4 drifted about **0.000002 J**, while Euler **gained about 7.7 J (+30%)**. Checking a quantity
that physics says must stay constant is one of the best ways to test any simulation.

### 7.5 Chaos: the twin

Press `T` to add a second (blue) pendulum that starts **0.001 rad (0.06°) away** from the first one.
At first they move as one. Within a few seconds they visibly separate, and soon they have nothing
in common. In a test run they were 1.4 m apart after 20 s (the rods are only 1 m long).

This is **chaos**: the motion is completely deterministic (no randomness at all), yet tiny
differences grow exponentially, so long-term prediction is impossible in practice. The same
"butterfly effect" is why weather forecasts stop being reliable after about a week.
The twin isn't a numerical error either: RK4 is accurate here, and the physics itself magnifies the difference.

### 7.6 Drawing it

- **World → screen.** The pivot is the screen centre. `rowsPerMetre` is chosen so that 2 m
  (`L1 + L2`) fits in every direction. One metre is `rowsPerMetre` rows vertically but
  `2 × rowsPerMetre` columns horizontally (tall cells again).
- **Lines.** `drawLine` steps along the rod one cell at a time and plots a character there
  (a "DDA" line algorithm). The character depends on the angle, computed with
  `atan2(−dy·2, dx)` (the `·2` corrects for tall cells): `-`, `/`, `|` or `\`.
- **Trail.** The last 400 positions of the lower bob (one every 2 physics steps) are stored in a
  `std::deque`. A deque is like a vector, but it can also remove from the front cheaply. Older
  points are drawn dimmer and with smaller characters (`*` → `:` → `.`).

---

## 8. C++ features used (and where)

| Feature | Where | Why |
|---|---|---|
| Abstract class, `virtual`, `override` | `Simulation` in `sim.h` | One loop drives any simulation (polymorphism) |
| `std::unique_ptr`, `std::make_unique` | factory functions | Automatic memory clean-up, no `delete` |
| Anonymous namespace | every `.cpp` | Keeps helpers private to their file |
| Function pointers | `MenuItem::make` in `main.cpp` | The menu is a table, not a chain of `if`s |
| Flat 2D arrays `y * w + x` | `Canvas`, `heat_`, `zbuf_` | One allocation, fast, simple to resize |
| Operator overloading | `State` in `pendulum.cpp`, `Color ==` | Write maths like maths |
| Lambdas | `toScreen`, `mix`, `randomAngle` | Small local helper functions |
| `constexpr`, `sizeof(str) − 1` | constants at the top of each file | Compile-time constants; string length without the `'\0'` |
| `std::initializer_list` | `gradient({c1, c2, c3}, t)` | Pass a list of colours in braces |
| `std::deque` | pendulum trail | Cheap add at the back, remove at the front |
| `std::atomic<bool>` | `active` in `terminal.cpp` | The Ctrl+C handler runs on another thread |
| Raw string literals `R"( ... )"` | the title in `main.cpp` | Backslashes in ASCII art without escaping them |
| `<random>` (`std::mt19937`) | `Random` in `sim.h` | Better random numbers than `rand()` |
| `<chrono>` | `FrameClock` | Precise time measurement |

**A gotcha found while writing this:** the "most vexing parse".

```cpp
const Rotation rotA(float(angleA_));   // ✗ C++ reads this as a FUNCTION declaration!
const Rotation rotA{float(angleA_)};   // ✓ braces always mean "make an object"
```

The first line can be read as "a function `rotA` taking a `float` parameter named `angleA_`", and
C++ rules say that reading wins. g++ warned (`-Wvexing-parse`) and then failed to compile. When
in doubt, initialise with braces `{}`.

---

## 9. Things to try (exercises)

Start easy and work down the list:

1. **Donut:** change `kShades` to `" .:-=+*#%@"`. Move the light: use `(1, 1, −1)/√3`, i.e.
   `luminance = (normal.x + normal.y - normal.z) * 0.57735f`. Change `R1`, `R2`, `K2` and predict what happens first.
2. **Fire:** change the palette in the constructor to make blue gas flames or green flames.
3. **Matrix:** add a theme. Then try two drops per column.
4. **Pendulum:** add friction. In `derivative()`, subtract `0.1 * s.w1` from `a1` and `0.1 * s.w2`
   from `a2`, then watch the energy fall instead of staying constant. Try `m2 = 0.1` or `L2 = 0.5`.
5. **Pendulum:** halve `kStep` and compare Euler's drift before and after (it should roughly halve).
   Do the same with RK4 (it should shrink about 16×).
6. **Add a 5th simulation**, e.g. Conway's Game of Life:
   1. create `src/life.cpp` with `class Life : public Simulation { ... }` and `makeLife()`;
   2. declare `std::unique_ptr<Simulation> makeLife();` in `sim.h`;
   3. add `{'5', "Game of Life", "...", makeLife}` to `kItems` in `main.cpp`;
   4. add `src\life.cpp` to `build.bat`.
7. **Port to Linux/macOS:** only `terminal.cpp` needs rewriting. Use `ioctl(TIOCGWINSZ)` for the size,
   `termios` raw mode plus `read()` for keys, `write()` for output and `nanosleep` for timing.
   Everything else is standard C++.

## 10. Troubleshooting

| Problem | Fix |
|---|---|
| You see text like `←[38;2;255;…` | The console doesn't understand ANSI codes. Use Windows Terminal. |
| "Not running in a Windows console" | You started it from Git Bash (mintty) or redirected its output. Use Windows Terminal / PowerShell / cmd, or `winpty ./ascii-sims.exe`. |
| Low fps or stutter on a huge window | Zoom in (`Ctrl` + `+`) for fewer cells. The old console (conhost) is slower than Windows Terminal. |
| `g++` not found | Add `C:\msys64\ucrt64\bin` to your `PATH`. |
| The terminal looks broken after a crash (no cursor, odd screen) | Close and reopen the tab. Normally the Ctrl+C handler prevents this. |

## 11. Further reading

- Andy Sloane, *Donut math: how donut.c works*: https://www.a1k0n.net/2011/07/20/donut-math.html
- Fabien Sanglard, *How DOOM fire was done*: https://fabiensanglard.net/doom_fire_psx/
- myPhysicsLab, *Double pendulum* (derivation of the equations): https://www.myphysicslab.com/pendulum/double-pendulum-en.html
- Microsoft, *Console Virtual Terminal Sequences* (all the ANSI codes Windows supports):
  https://learn.microsoft.com/en-us/windows/console/console-virtual-terminal-sequences
