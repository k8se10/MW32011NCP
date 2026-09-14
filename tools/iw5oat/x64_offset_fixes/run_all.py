#!/usr/bin/env python3
"""
MW32011NCP / iw5oat -- runs the full x64_offset_fixes/ sequence in the
correct order. See README.md in this directory for what each step fixes
and why a fixed sequence is needed at all.

Usage:
    python run_all.py           # dry run (prints what each step would change)
    python run_all.py --apply   # actually rewrite the generated files
"""
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
STEPS = [
    "01_fix_field_offsets.py",
    "02_fix_loadwithfill_mismatch.py",
    "03_fix_ptrarrayfill_crosscontam.py",
    "04_fix_dynamicfill_sizes.py",
]


def main():
    args = sys.argv[1:]
    for step in STEPS:
        print(f"\n=== {step} ===")
        result = subprocess.run([sys.executable, str(HERE / step), *args])
        if result.returncode != 0:
            print(f"\n{step} failed (exit {result.returncode}) -- stopping.")
            sys.exit(result.returncode)


if __name__ == "__main__":
    main()
