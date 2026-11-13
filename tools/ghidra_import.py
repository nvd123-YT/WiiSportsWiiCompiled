import argparse
import os
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
    default_home = os.environ.get(
        "GHIDRA_HOME",
        str(Path.home() / "Tools" / "ghidra_12.1.4_PUBLIC" if sys.platform == "darwin" else ROOT / "ghidra" / "install"),
    )
    parser.add_argument("--ghidra-home", default=default_home)
    parser.add_argument("--projects", default=str(ROOT / "ghidra" / "projects"))
    args = parser.parse_args()

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
