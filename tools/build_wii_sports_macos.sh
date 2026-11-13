#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
GAME_DIR="$ROOT/games/wiisports"
DISC="${1:-$GAME_DIR/disc/wiisports.rvz}"
EXTRACTED="$GAME_DIR/extracted"
BUILD_DIR="$ROOT/build/out"
JOBS="$(sysctl -n hw.ncpu 2>/dev/null || printf '4')"

export WP_GAME=wiisports

fail() {
    printf 'error: %s\n' "$*" >&2
    exit 1
}

[ "$(uname -s)" = "Darwin" ] || fail "this helper is intended for macOS"
command -v python3 >/dev/null 2>&1 || fail "python3 is required"
command -v cmake >/dev/null 2>&1 || fail "cmake is required"
command -v ninja >/dev/null 2>&1 || fail "ninja is required"
mkdir -p "$GAME_DIR/disc" "$GAME_DIR/analysis" "$GAME_DIR/nand" "$GAME_DIR/textures"

if [ ! -f "$EXTRACTED/sys/main.dol" ]; then
    command -v nodtool >/dev/null 2>&1 || fail "nodtool is required to extract the disc; alternatively, put an already-extracted Wii Sports filesystem under $EXTRACTED"
    [ -f "$DISC" ] || fail "put your legally obtained Wii Sports disc dump at $DISC or pass its path as the first argument"
    mkdir -p "$EXTRACTED"
    nodtool extract "$DISC" "$EXTRACTED"
fi

[ -f "$EXTRACTED/sys/main.dol" ] || fail "extraction did not produce sys/main.dol"
[ -f "$EXTRACTED/sys/boot.bin" ] || fail "extraction did not produce sys/boot.bin"

NEED_GHIDRA=0
FUNCTION_LINES="$(awk 'END { print NR + 0 }' "$GAME_DIR/analysis/dol_functions.csv" 2>/dev/null || printf '0')"
if [ "$FUNCTION_LINES" -lt 2 ] || [ "${WII_SPORTS_REIMPORT_GHIDRA:-0}" = "1" ]; then
    NEED_GHIDRA=1
fi

if [ "$NEED_GHIDRA" = "1" ]; then
    printf '\n== Import Wii Sports into Ghidra and export function metadata ==\n'
    if [ "${WII_SPORTS_REIMPORT_GHIDRA:-0}" = "1" ]; then
        python3 "$ROOT/tools/ghidra_import.py" --force
    else
        python3 "$ROOT/tools/ghidra_import.py"
    fi
else
    printf '\n== Reusing existing Ghidra function export ==\n'
fi

printf '\n== Unpack REL modules ==\n'
python3 "$ROOT/recompiler/unpack_rels.py"

printf '\n== Lift DOL functions ==\n'
python3 "$ROOT/recompiler/recomp.py"

printf '\n== Lift all REL modules ==\n'
python3 "$ROOT/recompiler/recomp_rel.py" --all

printf '\n== Refresh DOL functions with discovered REL call targets ==\n'
python3 "$ROOT/recompiler/recomp.py"

printf '\n== Recompile DSP microcode ==\n'
python3 "$ROOT/recompiler/dsp/recomp_dsp.py"

printf '\n== Configure and build native macOS target ==\n'
cmake -S "$ROOT" -B "$BUILD_DIR" -G Ninja -DWP_GAME=wiisports -DCMAKE_BUILD_TYPE=Release
cmake --build "$BUILD_DIR" --target pi_tests --parallel "$JOBS"
ctest --test-dir "$BUILD_DIR" -R "^pi_tests$" --output-on-failure
cmake --build "$BUILD_DIR" --target wiisports --parallel "$JOBS"

printf '\n== Run Wii Sports ==\n'
"$BUILD_DIR/wiisports" "$EXTRACTED" "${WII_SPORTS_TIMEOUT:-600}"
