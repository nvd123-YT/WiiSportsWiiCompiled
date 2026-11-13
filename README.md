# Wii Sports Recomp

Native recompilation of **Wii Sports** for macOS, Windows and Linux.

> **Status:** Experimental — gameplay compatibility is still being validated.

The launcher builds the game locally from a Wii Sports disc image supplied by the user. No Nintendo game files are distributed or downloaded.

## Download and install

Open this repository's **Releases** tab and download the newest release for your platform:

- **macOS:** Apple Silicon or Intel DMG — open it and move **Wii Sports Recomp.app** into Applications.
- **Windows:** wiisportsrecomp-launcher.exe or the ZIP.
- **Linux:** wiisportsrecomp-launcher-x86_64.AppImage.

Launch the app, choose your legally obtained Wii Sports ISO/RVZ/WBFS image, select the install folder, and press **Install**. The launcher downloads and verifies required build tools from their official sources, extracts the disc, performs Ghidra analysis when a function export is not already present, translates the game and builds the native executable. The first build can take longer because Ghidra must identify and export the DOL's function boundaries. Your disc image, extracted data, Ghidra project, generated C++ and saves stay on your computer.

For the first build, install Ghidra 12 with the GameCube Loader and the PowerPC:BE:32:Gekko_Broadway language. On macOS the default location is ~/Tools/ghidra_12.1.4_PUBLIC. On other platforms set GHIDRA_HOME to the Ghidra installation directory. A valid existing games/wiisports/analysis/dol_functions.csv export is reused automatically; use python tools/ghidra_import.py --force to rerun Ghidra.

## Runtime architecture

The shared runtime provides a PowerPC CPU/lifter, guest memory, OS threads and context switching, MMIO, IOS, disc and NAND services, REL module dispatch, GX graphics, DSP audio, keyboard/gamepad input and Wii Remote support. On macOS it uses SDL3 and OpenGL.

The game target is configured under games/wiisports/; general runtime code lives under engine/. The default local workflow is:

```sh
bash tools/build_wii_sports_macos.sh
```

Place your disc image at games/wiisports/disc/wiisports.rvz, or pass its path as the first argument. A pre-extracted filesystem can instead be placed under games/wiisports/extracted/.

## Sports and input acceptance

The target is intended to preserve all five sports: **Tennis, Baseball, Bowling, Golf and Boxing**. Keyboard, conventional gamepads and real Wii Remotes must remain available. Boxing additionally requires real motion sensing, left/right punch differentiation, blocking/guarding and Nunchuk controls.

These are acceptance requirements, not claims that all five sports are already playable. The new Wii Sports target, runtime integration and PI MMIO regression tests are an experimental foundation. Compatibility must be verified against the user's own disc.

## Local data and privacy

Do not commit or distribute disc images, extracted game files, generated game code, save data, textures, Ghidra databases, or memory dumps. The repository ignores the game-specific local folders. Only the launcher, runtime, recompilation tools and source code belong in releases.

See the porting notes and third-party notices.
