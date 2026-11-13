# Wii Sports Recomp

A native recompilation project for **Wii Sports** on macOS, Windows and Linux. The launcher takes a Wii Sports disc image that you provide, translates the PowerPC game code into native C++, and builds a local executable against the shared Wii runtime.

> **Status: experimental.** The launcher and cross-platform packaging build in CI, and the runtime/translator test suite passes. That does not yet mean all five sports are fully playable.

## Download

Open this repository's **Releases** tab and choose the newest release for your platform.

| Platform | File | Notes |
| --- | --- | --- |
| macOS Apple Silicon | `WiiSportsRecomp-macos-arm64.dmg` | Apple Silicon Macs |
| macOS Intel | `WiiSportsRecomp-macos-x86_64.dmg` | Intel Macs |
| Windows x64 | `wiisportsrecomp-launcher.exe` | A ZIP containing the same launcher is also provided |
| Linux x86_64 | `wiisportsrecomp-launcher-x86_64.AppImage` | Make it executable before opening |

SHA-256 checksum files are attached to each release.

### Install and build the game

1. Download and open the package for your operating system.
2. On macOS, drag **Wii Sports Recomp.app** into Applications using the shortcut in the DMG. On Windows, run the EXE. On Linux, run the AppImage.
3. In the launcher, choose your own Wii Sports disc image. The supported file types are ISO, WBFS, RVZ, CISO, WIA and GCM.
4. Keep the suggested install folder or choose another folder with enough free space, then select **Install**.
5. When the build finishes, select **Play**.

The launcher verifies the disc, extracts it locally, prepares the compiler and analysis tools, performs Ghidra analysis when needed, translates the DOL and REL modules, and compiles the native game. Missing tools are downloaded from their official upstream releases and checked against pinned SHA-256 hashes. The launcher does **not** download the game itself.

On the first online install, the launcher also downloads and manages **Eclipse Temurin JDK 21**, **Ghidra 12.1.4**, and the **GameCube Loader 1.3.1 extension**. They are cached under the selected game folder in `build/deps/toolchain`; later builds reuse them. Java and Ghidra do not need to be installed system-wide or added to your shell environment. The downloads total roughly 780 MB before extraction. Offline mode works after the required tools have been cached or supplied in the offline tools pack.

A fresh install should have approximately **8 GB of free space** for the game build and managed analysis tools. If you already have a complete extracted filesystem, approximately 7 GB should be available.

### Managed Java and Ghidra

The GUI launcher automatically provisions its analysis environment on first setup:

- **Eclipse Temurin JDK 21.0.13+9**, configured as the launcher's `JAVA_HOME`.
- **Ghidra 12.1.4**, configured as `GHIDRA_HOME`.
- **GameCube Loader 1.3.1**, installed into Ghidra's `Ghidra/Extensions` folder. It supplies the GameCube DOL/REL loader and the `PowerPC:BE:32:Gekko_Broadway` processor language used by this project.

Each archive is downloaded from its upstream release and verified against a pinned SHA-256 digest before extraction. The launcher keeps the tools in its managed cache under `build/deps/toolchain`, so no admin privileges, system Java installation, or manual `GHIDRA_HOME` setup are required. If you already have a valid Ghidra 12.1 installation with the GameCube Loader and JDK 21, the importer can also use your configured environment.

Ghidra's settings directory is isolated at `build/deps/toolchain/ghidra-settings`. This prevents a second GameCube Loader in the user's normal Ghidra profile from colliding with the launcher's managed extension. The launcher does not delete or alter extensions in the user's existing Ghidra profile.

The analysis importer reuses a valid `games/wiisports/analysis/dol_functions.csv` on later builds. To deliberately run the analysis again:

```sh
python tools/ghidra_import.py --force
```

The standalone shell build helper does not run the launcher's dependency manager; when using it directly, provide JDK 21 and Ghidra 12.1.4 with the GameCube Loader yourself.

## macOS window behavior

The SDL window and event loop are now created and run on the process's main thread, while the translated guest CPU runs on a separate worker with its own 512 MiB stack. This addresses the previous macOS symptom where the game process appeared in Activity Monitor but no game window became visible because window creation/event handling happened on a detached worker thread.

The new architecture is included in the CI-built runtime, but the visible game window still needs a real-disc launch test on macOS. If startup fails, check the launcher's diagnostic report and the local game log rather than assuming that a running process means the game has booted successfully.

macOS packages are ad-hoc signed, not Developer ID signed or notarized. Gatekeeper may show a warning the first time. Windows may also show an unsigned-app warning.

## Sports and controller support

The target is intended to preserve all five Wii Sports activities:

- Tennis
- Baseball
- Bowling
- Golf
- Boxing

The launcher includes player profiles, gamepad-to-player assignment and controller setup. The shared runtime has keyboard, conventional gamepad and real Wii Remote input paths. However, these title-specific controls and all five sports still need hands-on compatibility testing against the user's own disc.

**Boxing has additional requirements:** real motion data, left/right punch differentiation, blocking/guarding and Nunchuk controls. These are required acceptance goals, not features claimed as fully verified.

## Current technical status

The project has a shared PowerPC CPU/lifter, guest memory, OS thread/context support, MMIO, IOS, disc/NAND services, REL module dispatch, GX graphics and SDL3 input. The macOS renderer uses SDL3 and OpenGL.

The Wii Sports DSP checksum catalogue is intentionally empty until the title's microcode signatures are verified. Audio microcode therefore uses the shared DSP interpreter fallback; native Wii Sports DSP recompilation is not yet claimed.

The Processor Interface interrupt cause and mask registers at `0xCC003000` and `0xCC003004` have a runtime model and regression tests. Full device interrupt aggregation and title-specific MMIO behavior still need verification. Unknown MMIO and unresolved guest function addresses must be diagnosed and implemented correctly, not silently ignored.

## Automatic builds and releases

The release workflow runs on pushes to `main`. Each successful run creates a **new, uniquely tagged GitHub Release** rather than replacing a rolling Nightly release. Each release contains:

- macOS Apple Silicon DMG
- macOS Intel DMG
- Windows x64 EXE and ZIP
- Linux x86_64 AppImage
- SHA-256 checksums for each platform

The workflow builds and verifies all four platform packages before it attempts to publish the release. The publisher requests `contents: write`.

The first release attempt built all four packages successfully but GitHub denied release creation with `403 Resource not accessible by integration`. The workflow now requests `contents: write` at workflow level while keeping the platform build jobs read-only. If a new main-branch run is still denied, open **Settings → Actions → General → Workflow permissions** and select **Read and write permissions**. Save the setting and start a fresh run (rerunning the old failed job retains its original token context). If an organization policy prevents changing the setting, add a fine-grained token limited to this repository with **Contents: Read and write** as the Actions secret `RELEASE_TOKEN`; the workflow supports that fallback.

## Build locally on macOS

Requirements: Xcode Command Line Tools, Python 3, CMake, Ninja, JDK 21, Ghidra 12.1.4 with the GameCube Loader, and a legally obtained Wii Sports disc image. The standalone helper also expects `nodtool` on `PATH` when extraction is required.

Put the disc image at `games/wiisports/disc/wiisports.rvz`, or pass its path as the first argument. Alternatively, put a fully extracted Wii Sports filesystem under `games/wiisports/extracted/`.

```sh
bash tools/build_wii_sports_macos.sh
```

The script reuses existing function metadata by default, translates the DOL and REL modules, runs the DSP translation step, builds the native target, runs the PI MMIO regression test, and launches `build/out/wiisports`.

For Ghidra reanalysis, set `WII_SPORTS_REIMPORT_GHIDRA=1`. For a shorter/longer runtime timeout, set `WII_SPORTS_TIMEOUT` (seconds).

## Local files and privacy

Disc images, extracted game files, generated C++, Ghidra projects/databases, save data, textures and memory dumps stay on the user's computer and are ignored by Git. Do not commit or redistribute those files. Release packages contain the launcher and project source, not Nintendo game data.

See [Wii Sports porting notes](docs/WII_SPORTS_PORT.md) and [third-party notices](THIRD_PARTY_NOTICES.md).
