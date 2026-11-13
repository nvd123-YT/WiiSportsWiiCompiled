import argparse
import csv
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
ANALYSIS_VERSION = "2"


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


def valid_function_export(path):
    try:
        with path.open(newline="", encoding="utf-8-sig") as handle:
            reader = csv.DictReader(handle)
            if not reader.fieldnames or not {"address", "size"}.issubset(reader.fieldnames):
                return False, 0
            count = 0
            for row in reader:
                address = (row.get("address") or "").strip()
                size = (row.get("size") or "").strip()
                if not address.lower().startswith("0x"):
                    return False, count
                value = int(address[2:], 16)
                length = int(size, 10)
                if value < 0 or value > 0xFFFFFFFF or length <= 0:
                    return False, count
                count += 1
            return count > 0, count
    except (OSError, csv.Error, ValueError, TypeError):
        return False, 0


def print_log_tails(paths, limit=80):
    for path in paths:
        if not path.is_file():
            continue
        try:
            lines = path.read_text(encoding="utf-8", errors="replace").splitlines()
        except OSError as error:
            print(f"[ghidra] Could not read {path}: {error}", file=sys.stderr, flush=True)
            continue
        if not lines:
            continue
        print(f"[ghidra] --- last {min(limit, len(lines))} lines of {path.name} ---", flush=True)
        for line in lines[-limit:]:
            print(f"[ghidra-log] {line}", flush=True)


def run_headless(command, label, log_paths=()):
    print(f"[ghidra] {label}", flush=True)
    try:
        result = subprocess.run(command, check=False)
    except OSError as error:
        print(f"[ghidra] Could not start {label}: {error}", file=sys.stderr, flush=True)
        print_log_tails(log_paths)
        return 1
    if result.returncode:
        print(
            f"[ghidra] {label} failed with exit code {result.returncode}. "
            "The relevant Ghidra log excerpts follow:",
            file=sys.stderr,
            flush=True,
        )
        print_log_tails(log_paths)
    return result.returncode


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
    analysis_version_file = GAME.analysis / ".ghidra-import-version"
    stored_version = ""
    try:
        stored_version = analysis_version_file.read_text(encoding="utf-8").strip()
    except OSError:
        pass

    if not args.force:
        valid, count = valid_function_export(function_export) if function_export.is_file() else (False, 0)
        if valid and stored_version == ANALYSIS_VERSION:
            print(f"Reusing {count} Ghidra function records from analysis version {stored_version}")
            return 0
        if valid:
            print(
                "Existing function metadata was produced by an older or unknown "
                "analysis pipeline; rerunning Ghidra to refresh it."
            )
        elif function_export.exists():
            print(f"Existing function export is invalid; rebuilding it: {function_export}")

    # Once a reanalysis is requested, remove any previous export first.
    # A failed Ghidra run must not leave metadata that a later normal launch
    # could mistake for the output of the failed reanalysis.
    try:
        function_export.unlink(missing_ok=True)
    except OSError as error:
        print(f"error: could not remove stale function export {function_export}: {error}", file=sys.stderr)
        return 1

    dol = GAME.dol
    if not dol.is_file():
        print(f"error: Wii game DOL not found: {dol}", file=sys.stderr)
        print("Extract the user-supplied disc image before running Ghidra.", file=sys.stderr)
        return 1
    if dol.stat().st_size < 0x100:
        print(f"error: Wii game DOL is shorter than its 0x100-byte header: {dol}", file=sys.stderr)
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
    import_log = GAME.analysis / "ghidra-import.log"
    import_script_log = GAME.analysis / "ghidra-import-scripts.log"
    export_log = GAME.analysis / "ghidra-export.log"
    export_script_log = GAME.analysis / "ghidra-export-scripts.log"

    # This project supplies its own DOL memory map and function discovery.
    # Ghidra's generic analyzer otherwise runs against the raw BinaryLoader
    # image first and emits unrelated failures before LoadDol remaps memory.
    command = [
        str(ghidra),
        str(projects),
        project_name,
        "-import", str(dol),
        "-loader", "BinaryLoader",
        "-loader-baseAddr", "0x0",
        "-processor", LANGUAGE,
        "-overwrite",
        "-noanalysis",
        "-log", str(import_log),
        "-scriptlog", str(import_script_log),
        "-scriptPath", str(SCRIPTS),
        "-preScript", "LoadDol.java", str(dol),
        "-postScript", "FindFunctions.java",
        "-postScript", "ApplySymbols.java", str(GAME.dolphin_symbols), str(GAME.analysis / "symbols.csv"),
    ]
    result = run_headless(command, "importing DOL and discovering functions", (import_log, import_script_log))
    if result != 0:
        return result

    # Export to a temporary file so a failed/partial export can never leave
    # stale metadata that a later launcher run mistakenly reuses.
    candidate_export = function_export.with_suffix(".csv.tmp")
    candidate_export.unlink(missing_ok=True)
    export = [
        str(ghidra),
        str(projects),
        project_name,
        "-process", "main.dol",
        "-noanalysis",
        "-log", str(export_log),
        "-scriptlog", str(export_script_log),
        "-scriptPath", str(SCRIPTS),
        "-postScript", "ExportFunctions.java", str(candidate_export),
    ]
    result = run_headless(export, "exporting discovered function metadata", (export_log, export_script_log))
    if result != 0:
        candidate_export.unlink(missing_ok=True)
        return result

    valid, count = valid_function_export(candidate_export)
    if not valid:
        candidate_export.unlink(missing_ok=True)
        print(
            "[ghidra] Function export is empty or malformed; refusing to continue. "
            f"See {import_log} and {import_script_log}.",
            file=sys.stderr,
        )
        return 1
    candidate_export.replace(function_export)
    try:
        analysis_version_file.write_text(ANALYSIS_VERSION + "\n", encoding="utf-8")
    except OSError as error:
        print(f"[ghidra] Warning: could not store analysis version marker: {error}", file=sys.stderr)
    print(f"Exported {count} Wii Sports DOL function records using analysis version {ANALYSIS_VERSION} to {function_export}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
