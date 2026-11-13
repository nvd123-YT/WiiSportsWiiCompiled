import sys
from pathlib import Path

import game
import lz11

GAME = game.load()
SOURCE = GAME.compressed_modules
TARGET = GAME.modules


def main():
    TARGET.mkdir(parents=True, exist_ok=True)
    failures = 0
    if not SOURCE.is_dir():
        print(f"no REL directory found at {SOURCE}; continuing with a DOL-only title")
        return 0

    for source in sorted(SOURCE.iterdir()):
        if source.name.endswith(".rel.lz"):
            try:
                data = lz11.decompress(source.read_bytes())
            except (ValueError, IndexError) as error:
                print(f"{source.name}: {error}", file=sys.stderr)
                failures += 1
                continue
            target_name = source.with_suffix("").name
        elif source.is_file() and source.suffix == ".rel":
            data = source.read_bytes()
            target_name = source.name
        else:
            continue

        target = TARGET / target_name
        if not target.exists() or target.read_bytes() != data:
            target.write_bytes(data)

    count = len(list(TARGET.glob("*.rel")))
    print(f"prepared {count} REL module(s) in {TARGET}")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
