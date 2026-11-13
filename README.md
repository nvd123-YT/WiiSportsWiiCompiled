# Wii Sports Recomp

A native macOS-first static recompilation target for **Wii Sports**, built on the reusable Wii hardware runtime in this repository.

<badge>Experimental integration branch</badge>

This branch is **not yet a playable release**. It adds a Wii Sports-specific target and macOS build pipeline. You must provide your own legally obtained Wii Sports disc dump. Game files, extracted data, generated code and Ghidra databases are not included.

## Target sports

The port must preserve all five original sports: **Tennis, Baseball, Bowling, Golf and Boxing**. The input target includes keyboard, conventional gamepads and real Wii Remotes. Boxing also requires differentiated left/right punches, blocking/guarding and Nunchuk input. These are acceptance requirements, not completed compatibility claims.

## Architecture

- **Translated game code:** the Wii Sports DOL and REL modules are lifted to native C++.
- **CPU/runtime:** shared PowerPC state, guest memory, MMIO, OS threads, interrupts and context handling.
- **Graphics:** shared GX command processor and SDL3/OpenGL renderer for macOS.
- **Audio:** shared DSP/audio pipeline with SDL3 host output.
- **System services:** shared IOS, disc, NAND, EXI and module-loading layers.
- **Input:** shared keyboard/gamepad/Wii Remote input services, to be validated against Wii Sports and its Boxing/Nunchuk requirements.

The game-specific files live under `games/wiisports/`. The shared hardware runtime lives under `engine/`. This target does not depend on Mario Kart Wii game-specific runtime code.

## macOS build

Requirements:

- Apple Xcode Command Line Tools.
- CMake, Ninja, Python 3 and Git.
- `nodtool` on `PATH` for Wii disc extraction.
- Ghidra 12 with the GameCube Loader and the `PowerPC:BE:32:Gekko_Broadway` language.
- A legally obtained Wii Sports disc image.

The default Ghidra path on the development Mac is `~/Tools/ghidra_12.1.4_PUBLIC`. Set `GHIDRA_HOME` to override it.

Put your disc image at:

```
games/wiisports/disc/wiisports.rvz
```

Or pass another image path as the first argument to the build helper:

```sh
bash tools/build_wii_sports_macos.sh "/path/to/your/Wii Sports disc.rvz"
```

The helper extracts the disc locally, imports the DOL into Ghidra, exports function address/size metadata, lifts the DOL and REL modules, recompiles DSP code, configures the macOS target and builds `build/out/wiisports`. It then attempts to launch the game.

Manual build details and the compatibility checklist are in [docs/WII_SPORTS_PORT.md](docs/WII_SPORTS_PORT.md).

## Ghidra integration model

Ghidra function discovery and symbols are retained as inputs to the build. The runtime requires native functions with the `void f_XXXXXXXX(wp::Cpu&)` calling convention, so the repository's PPC lifter generates runtime-compatible C++ from the original DOL/REL instructions. Ghidra decompiler pseudocode is not linked directly.

The generated function list is written to `games/wiisports/analysis/dol_functions.csv`. Verified symbol names belong in `games/wiisports/analysis/symbols.csv`; hardware/OS replacements must be individually justified in `hle_functions.csv`.

## Status and limitations

The shared engine already has general Wii runtime and GX/OpenGL, DSP, IOS, memory, disc, and input components. That does **not** prove Wii Sports compatibility. Wii Sports-specific MMIO, SDK calls, REL modules, graphics states, audio microcode, save paths and all five sports still need validation with the user's disc.

Do not suppress an unknown MMIO write or unresolved guest function to make the game appear to continue. Implement the documented register or resolve the guest address to a translated DOL/REL function or a justified hardware service.

No game data or proprietary assets are distributed by this repository.
