#!/bin/sh
# Double-click this file in Finder: it opens in Terminal, builds the program if needed and runs it.
cd "$(dirname "$0")" || exit 1
if [ ! -x ascii-sims ] || [ -n "$(find src build.sh -newer ascii-sims)" ]; then
    ./build.sh || { echo "Press Return to close."; read -r _; exit 1; }
fi
printf '\033[8;44;140t'   # ask the window for 140 x 44 characters: more cells, more detail
exec ./ascii-sims
