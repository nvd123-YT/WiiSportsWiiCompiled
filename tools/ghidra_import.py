import argparse
import os
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "recompiler"))

import game

GAME = game.load()
SCRIPTS = ROOT / "ghidra" / "scripts"
LANGUAGE = "PowerPC:BE:32:Gekko_Broadway"


def resolve_ghidra(home):
    executable = "analyzeHeadless.bat" if os.name == "nt" else "analyzeHeadless"
    candidates = [
        home / "support" / executable,
        home / "support" / "analyzeHeadless",
    ]
    for candidate in candidates:
        if candidate.is_file():
            return candidate
    raise FileNotFoundError(
        f"Ghidra headless launcher was not found under {home}. "
        "Set GHIDRA_HOME to the Ghidra installation directory."
    )


def main():
    parser = argparse.ArgumentParser()
    common_homes = [
        Path.home() / "Tools" / "ghidra_12.1.4_PUBLIC",
        Path.home() / "ghidra_12.1.4_PUBLIC",
        Path.home() / "Applications" / "ghidra_12.1.4_PUBLIC",
        Path.home() / "Tools" / "ghidra",
        Path.home() / "Applications" / "ghidra",
        Path("/opt/ghidra"),
        Path("/usr/local/ghidra"),
        ROOT / "ghidra" / "install",
        Path("C:/Tools/ghidra_12.1.4_PUBLIC"),
        Path("C:/Program Files/Ghidra/ghidra_12.1.4_PUBLIC"),
        Path("C:/Program Files/Ghidra"),
    ]
    search_roots = [
        Path.home() / "Tools", Path.home() / "Applications",
        Path.home(), Path("/opt"), Path("/usr/local"),
        Path("C:/Tools"), Path("C:/Program Files/Ghidra"),
    ]
    for search_root in search_roots:
        if search_root.is_dir():
            common_homes.extend(sorted(
                (candidate for candidate in search_root.glob("ghidra*") if candidate.is_dir()),
                key=lambda candidate: candidate.name,
            ))
    from_path = shutil.which("analyzeHeadless") or shutil.which("analyzeHeadless.bat")
    if from_path:
        common_homes.insert(0, Path(from_path).resolve().parent.parent)

    unique_homes = []
    seen_homes = set()
    for home in common_homes:
        identity = str(home.expanduser()).casefold()
        if identity not in seen_homes:
            unique_homes.append(home.expanduser())
            seen_homes.add(identity)

    configured_home = os.environ.get("GHIDRA_HOME")
    detected_home = next(
        (home for home in unique_homes
         if (home / "support" / "analyzeHeadless").is_file()
         or (home / "support" / "analyzeHeadless.bat").is_file()),
        unique_homes[0],
    )
    default_home = configured_home or str(detected_home)
    parser.add_argument("--ghidra-home", default=default_home)
    parser.add_argument("--projects", default=str(ROOT / "ghidra" / "projects"))
    parser.add_argument("--force", action="store_true", help="Re-run Ghidra even when a valid function export already exists")
    args = parser.parse_args()

    function_export = GAME.analysis / "dol_functions.csv"
    if not args.force and function_export.is_file():
        try:
            import csv
            with function_export.open(newline="", encoding="utf-8") as handle:
                rows = list(csv.DictReader(handle))
            if rows and all("address" in row and "size" in row for row in rows):
                print(f"Reusing {len(rows)} existing Ghidra function records from {function_export}")
                return 0
        except (OSError, csv.Error) as error:
            print(f"Existing function export is unreadable ({error}); running Ghidra again.")

    dol = GAME.dol
    if not dol.is_file():
        print(f"error: Wii game DOL not found: {dol}", file=sys.stderr)
        print("Extract the user-supplied disc image before running Ghidra.", file=sys.stderr)
        return 1

    try:
        ghidra = resolve_ghidra(Path(args.ghidra_home).expanduser())
    except FileNotFoundError as error:
        print(f"error: {error}", file=sys.stderr)
        return 1

    projects = Path(args.projects).expanduser()
    projects.mkdir(parents=True, exist_ok=True)
    GAME.analysis.mkdir(parents=True, exist_ok=True)
    project_name = GAME.key

    command = [
        str(ghidra),
        str(projects),
        project_name,
        "-import", str(dol),
        "-loader", "BinaryLoader",
        "-loader-baseAddr", "0x0",
        "-processor", LANGUAGE,
        "-overwrite",
        "-scriptPath", str(SCRIPTS),
        "-preScript", "LoadDol.java", str(dol),
        "-postScript", "FindFunctions.java",
        "-postScript", "ApplySymbols.java", str(GAME.dolphin_symbols), str(GAME.analysis / "symbols.csv"),
    ]
    if subprocess.call(command) != 0:
        return 1

    export = [
        str(ghidra),
        str(projects),
        project_name,
        "-process", "main.dol",
        "-noanalysis",
        "-scriptPath", str(SCRIPTS),
        "-postScript", "ExportFunctions.java", str(GAME.analysis / "dol_functions.csv"),
    ]
    result = subprocess.call(export)
    if result == 0:
        print(f"Exported Wii Sports DOL function metadata to {GAME.analysis / 'dol_functions.csv'}")
    return result


if __name__ == "__main__":
    sys.exit(main())
