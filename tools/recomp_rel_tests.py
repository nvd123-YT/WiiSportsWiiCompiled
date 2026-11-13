#!/usr/bin/env python3
"""Regression tests for Wii Sports REL generation edge cases."""

import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "recompiler"))

import recomp_rel


def test_dol_only_module_table():
    original_output = recomp_rel.OUTPUT
    with tempfile.TemporaryDirectory(prefix="wii-rel-table-") as temporary:
        output = Path(temporary) / "build" / "wiisports" / "generated" / "modules"
        recomp_rel.OUTPUT = output
        try:
            recomp_rel.write_table()
            table = output / "modules_table.cpp"
            assert table.is_file(), "DOL-only build did not create modules_table.cpp"
            generated = table.read_text(encoding="utf-8")
            assert "const size_t g_module_count = 0;" in generated
            assert "const ModuleDescriptor* const g_module_table[]" in generated
        finally:
            recomp_rel.OUTPUT = original_output


if __name__ == "__main__":
    test_dol_only_module_table()
    print("DOL-only REL module-table regression test passed")
