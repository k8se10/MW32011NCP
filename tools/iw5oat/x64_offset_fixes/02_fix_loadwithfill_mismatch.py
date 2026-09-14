#!/usr/bin/env python3
"""
MW32011NCP / iw5oat, 2026-09-14 -- targeted repair for one gap in
01_fix_field_offsets.py's own struct-array pattern (Case 4).

That regex requires an `arrayFill.AtOffset(0 + N * index)` call to appear
"close enough" (non-greedy DOTALL) after a `LoadWithFill(N * count)` to
pair them into one match. In 10 files, the true pair was farther apart
than the match found, or the match anchored on the wrong instance --
leaving the FillStruct/AtOffset half correctly converted to
`sizeof(Type) * index` while the paired LoadWithFill line one or two
statements earlier stayed a literal `N * count`. Confirmed via direct
before/after inspection of all 10 sites (e.g. fxeffectdef's
LoadArray_FxElemDef: `LoadWithFill(256 * count)` next to an already-fixed
`arrayFill.AtOffset(0 + sizeof(FxElemDef) * index)`).

Run this after 01 -- it specifically looks for a still-literal
`LoadWithFill(N * count)` followed (within the same function body) by an
ALREADY-FIXED `arrayFill.AtOffset(0 + sizeof(TYPE) * index)`, and makes the
LoadWithFill argument match that same TYPE.
"""
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from common import load_db_files  # noqa: E402

PAIR_RE = re.compile(
    r"m_stream\.LoadWithFill\((\d+)u?\s*\*\s*count\)(.{0,2000}?)"
    r"arrayFill\.AtOffset\(0\s*\+\s*sizeof\((\w+)\)\s*\*\s*index\)",
    re.DOTALL,
)


def repl(m):
    _literal, middle, type_name = m.group(1), m.group(2), m.group(3)
    return f"m_stream.LoadWithFill(sizeof({type_name}) * count){middle}arrayFill.AtOffset(0 + sizeof({type_name}) * index)"


def main():
    dry_run = "--apply" not in sys.argv
    total = 0
    for f in load_db_files():
        text = f.read_text(encoding="utf-8", errors="replace")
        matches = PAIR_RE.findall(text)
        if matches:
            print(f"  {f.name}: {len(matches)} mismatch(es) fixed")
            total += len(matches)
            if not dry_run:
                f.write_text(PAIR_RE.sub(repl, text), encoding="utf-8")
    print(f"\nTotal mismatches fixed: {total}")
    if dry_run:
        print("Dry run only -- re-run with --apply to write changes.")


if __name__ == "__main__":
    main()
