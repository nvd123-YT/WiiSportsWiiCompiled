#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
GAME_DIR="$ROOT/games/wiisports"
DISC="${1:-$GAME_DIR/disc/wiisports.rvz}"
EXTRACTED="$GAME_DIR/extracted"
BUILD_DIR="$ROOT/build/out"
JOBS="$(sysctl -n hw.ncpu 2>/dev/null || printf '4')"

export WP_GAME=wiisports
export GHIDRA_HOME="${GHIDRA_HOME:-$HOME/Tools/ghidra_12.1.4_PUBLIC}"

fail() {
    printf 'error: %s\n' "$*" >&2
    exit 1
}

[ "$(uname -s)" = "Darwin" ] || fail "this helper is intended for macOS"
command -v python3 >/dev/null 2>&1 || fail "python3 is required"
command -v cmake >/dev/null 2>&1 || fail "cmake is required"
command -v ninja >/dev/null 2>&1 || fail "ninja is required"
command -v nodtool >/dev/null 2>&1 || fail "nodtool is required for Wii disc extraction"
[ -x "$GHIDRA_HOME/support/analyzeHeadless" ] || fail "Ghidra headless launcher not found at $GHIDRA_HOME/support/analyzeHeadless; set GHIDRA_HOME"
[ -f "$DISC" ] || fail "put your legally obtained Wii Sports disc dump at $DISC or pass its path as the first argument"

mkdir -p "$GAME_DIR/disc" "$GAME_DIR/analysis" "$GAME_DIR/nand" "$GAME_DIR/textures"

if [ ! -f "$EXTRACTED/sys/main.dol" ]; then
    mkdir -p "$EXTRACTED"
    nodtool extract "$DISC" "$EXTRACTED"
fi

[ -f "$EXTRACTED/sys/main.dol" ] || fail "extraction did not produce sys/main.dol"
[ -f "$EXTRACTED/sys/boot.bin" ] || fail "extraction did not produce sys/boot.bin"

printf '\n== Import Wii Sports into Ghidra and export function metadata ==\n'
python3 "$ROOT/tools/ghidra_import.py"

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
