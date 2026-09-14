#!/usr/bin/env python3
"""
MW32011NCP / iw5oat, 2026-09-14 -- fixes a real cross-function-boundary
false positive in 01_fix_field_offsets.py's own struct-array pattern.

`STRUCT_ARRAY_RE` (Case 4 in 01) runs before the ptrArrayFill-specific
patterns (Case 5) touch the text. At that point, `ptrArrayFill =
m_stream.LoadWithFill(4 * count);` (still unconverted) also matches Case
4's own `m_stream.LoadWithFill(N * count)` half -- and because that regex
uses DOTALL non-greedy matching with no function-boundary anchor, it can
search forward PAST the correct (differently-named) `ptrArrayFill.FillPtr(...)`
calls and pair instead with the next `FillStruct_<Type>(arrayFill.AtOffset(0
+ N * index))` call found ANYWHERE LATER IN THE FILE -- often in a
completely unrelated function -- producing
`ptrArrayFill = m_stream.LoadWithFill(sizeof(WrongType) * count)` instead
of the correct `sizeof(void*) * count`. Confirmed live: this was the actual
cause of MaterialTechniqueSet's LoadPtrArray_MaterialTechnique reading a
`sizeof(MaterialPass) * count`-sized buffer for what is actually a 54-
element array of `MaterialTechnique*` pointers, corrupting every read after
it.

`ptrArrayFill` is, by construction (always immediately followed by
`ptrArrayFill.FillPtr(...)` calls, confirmed via direct inspection of every
site), always a plain pointer array -- so every one of its LoadWithFill
sizes must be `sizeof(void*) * count`, unconditionally, regardless of what
type name a stray cross-function match put there. Run this after 01 (and
after/independent of 02).
"""
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from common import load_db_files  # noqa: E402

RE = re.compile(r"ptrArrayFill = m_stream\.LoadWithFill\(sizeof\(\w+\) \* count\);")
CORRECT = "ptrArrayFill = m_stream.LoadWithFill(sizeof(void*) * count);"


def main():
    dry_run = "--apply" not in sys.argv
    total = 0
    for f in load_db_files():
        text = f.read_text(encoding="utf-8", errors="replace")
        wrong = [m for m in RE.findall(text) if "sizeof(void*)" not in m]
        if wrong:
            print(f"  {f.name}: {len(wrong)} fixed -> {wrong}")
            total += len(wrong)
            if not dry_run:
                f.write_text(RE.sub(CORRECT, text), encoding="utf-8")
    print(f"\nTotal fixed: {total}")
    if dry_run:
        print("Dry run only -- re-run with --apply to write changes.")


if __name__ == "__main__":
    main()
