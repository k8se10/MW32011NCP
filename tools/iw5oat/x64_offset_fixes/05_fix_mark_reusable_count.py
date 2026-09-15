#!/usr/bin/env python3
"""
MW32011NCP / iw5oat, 2026-09-15 -- guards MarkArray_<Type>(varX->count) call
sites in generated `*_mark_db.cpp` files against a genuine, confirmed-live
crash: when the array pointer field is DSL-declared `reusable` (this
format's shared/deduplicated-array mechanism -- see e.g.
`snd_alias_list_t.txt`'s own `set reusable head; set count head count;`),
the Load phase correctly only trusts the local `count` field in the FRESH
(FOLLOWING) branch -- when the pointer instead resolves via an
already-loaded alias/offset lookup, the array itself is shared with
whichever earlier asset originally owned it, but this asset's OWN raw
`count` field was still read unconditionally from the wire as part of the
struct's fixed header and is NOT guaranteed meaningful in that branch.

ZoneCodeGenerator's Mark-phase code generation doesn't carry this
FOLLOWING-vs-lookup distinction across into `Mark_<Type>()` at all -- it
unconditionally calls `MarkArray_<Element>(varX->count)` whenever the
pointer is non-null, regardless of how it was resolved. Confirmed live via
direct diagnostic tracing (`re_notes/x64_migration/fastfile_format_research.md`
SS5.30): `so_survival_mp_alpha.ff` asset 785 (a `snd_alias_list_t`) reads a
genuinely garbage `count = -1` immediately after FillStruct even though
`head` itself resolves correctly via `ConvertOffsetToPointerLookup` to a
real, valid heap pointer. `MarkArray_snd_alias_t`'s own `count` parameter
is `size_t` (unsigned) -- the implicit `int -1` -> `size_t` conversion at
the call site produces `0xFFFFFFFFFFFFFFFF`, and the resulting near-infinite
loop walks `var++` far past any real allocation within a handful of
iterations, segfaulting. Confirmed via a full native `iw5sp.exe` decompile
trail (`re_notes/ghidra_scripts/decomp_snd_alias_list_*.txt`,
`decomp_soundfileref_140096900.txt`) that every relevant struct's real byte
layout (snd_alias_list_t=24, snd_alias_t=0x98, SoundFile=0x18,
StreamedSound=0x10) matches this fork's own C++ struct sizes exactly --
ruling out a stream-cursor-desync theory in favor of this specific,
narrower Mark-phase gap.

Fix: wrap each confirmed-shape `MarkArray_<Element>(varX-><count-field>);`
call with `if (<count-field> > 0 && <count-field> <= <MAX_SANE_COUNT>)`,
evaluated against the field's own declared (signed, for every case checked
so far) type, so a garbage count -- confirmed live in BOTH directions, a
negative value (`-1`, `so_survival_mp_alpha.ff`) AND an implausibly large
positive one (`805330033`, `so_nyse_ny_manhattan.ff` -- an int this large
reinterpreted as a signed count still passes a bare `> 0` check, so that
alone isn't sufficient) -- is skipped, gracefully declining to (re-)walk a
questionable array, rather than trusted into a runaway loop. This can never
affect genuinely correct data (every real `snd_alias_t` array actually
observed this session tops out at a handful of entries, nowhere near
MAX_SANE_COUNT), and skipping a redundant re-mark of an already-loaded
shared array is architecturally expected to be harmless -- its real
elements were already registered as dependencies when the original,
first-loading asset processed them.

**Deliberately narrow in scope, not a blanket fix.** A DSL sweep found 18
IW5 asset types declare at least one `reusable` field
(`re_notes/known_issues_x64.md`'s own newest round has the full list) --
this script only touches call sites matching the exact confirmed-buggy
shape (`MarkArray_<Ident>(var<Ident2>-><simple field access>);`, no
compound expressions, no array indexing) rather than assuming every
`reusable`-adjacent MarkArray_ call shares this exact bug. The other 17
types are a real, documented, NOT-yet-individually-verified lead for a
future round -- each would need its own native cross-check before being
folded into this same fix, the same care this round took for
snd_alias_list_t specifically.

Run this after 01-04 (disjoint call shape and file glob -- targets
`*_mark_db.cpp`, not `*_load_db.cpp` -- order relative to the others
doesn't matter).
"""
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from common import ZONECODE_ROOT  # noqa: E402

# Matches: MarkArray_<Elem>(var<Owner>-><simpleField>);
# Deliberately conservative -- only a bare `var<Owner>->identifier` count
# expression (no `[index]`, no arithmetic) is touched.
CALL_RE = re.compile(
    r"(?P<indent>[ \t]*)MarkArray_(?P<elem>\w+)\((?P<owner_expr>var\w+->\w+)\);"
)


def mark_db_files():
    if not ZONECODE_ROOT.exists():
        raise SystemExit(
            f"ZoneCode output not found at {ZONECODE_ROOT} -- run ZoneCodeGenerator "
            f"for IW5 first (see tools/iw5oat/x64_offset_fixes/README.md)."
        )
    return sorted(ZONECODE_ROOT.glob("XAssets/*/*_mark_db.cpp"))


# Confirmed-buggy call sites, one per (file stem, exact original line) --
# deliberately an allowlist, not a blanket regex match across every
# MarkArray_ call, per this script's own "narrow, not blanket" scope note
# above. Add a new entry here only after independently confirming (native
# decompile struct-size cross-check, live crash repro, live re-test showing
# the guard fixes it with zero regressions) the same bug shape for another
# asset type -- don't extend this list on suspicion alone.
CONFIRMED_BUGGY_CALLS = {
    ("snd_alias_list_t_iw5_mark_db.cpp", "MarkArray_snd_alias_t(varsnd_alias_list_t->count);"),
}

# A generous, clearly-unreachable-by-real-data ceiling -- every real
# snd_alias_t array observed this session (both correctly-loaded FOLLOWING
# cases and the corrupted alias-lookup cases before this fix) never
# exceeded a handful of entries. 100,000 entries * sizeof(snd_alias_t)
# (152 bytes) would be a >15MB array for this one field alone -- several
# orders of magnitude beyond anything plausible for this asset type, while
# still catching every garbage value confirmed live so far (a huge
# unsigned wraparound from -1, and a random-looking 805330033).
MAX_SANE_COUNT = 100_000


def process_file(path: Path, dry_run: bool):
    text = path.read_text(encoding="utf-8", errors="replace")
    fixes = []

    def repl(m):
        original_call = m.group(0).strip()
        key = (path.name, original_call)
        if key not in CONFIRMED_BUGGY_CALLS:
            return m.group(0)

        indent = m.group("indent")
        elem = m.group("elem")
        owner_expr = m.group("owner_expr")
        fixes.append(f"MarkArray_{elem}({owner_expr}) guarded against count <= 0 or > {MAX_SANE_COUNT}")
        return (
            f"{indent}if ({owner_expr} > 0 && {owner_expr} <= {MAX_SANE_COUNT})\n"
            f"{indent}{{\n{indent}    MarkArray_{elem}({owner_expr});\n{indent}}}"
        )

    new_text = CALL_RE.sub(repl, text)
    if new_text != text:
        if not dry_run:
            path.write_text(new_text, encoding="utf-8")
        return fixes
    return []


def main():
    dry_run = "--apply" not in sys.argv
    total = 0
    for f in mark_db_files():
        fixes = process_file(f, dry_run)
        if fixes:
            print(f"  {f.name}:")
            for desc in fixes:
                print(f"    {desc}")
                total += 1
    print(f"\nTotal call sites guarded: {total}")
    if dry_run:
        print("Dry run only -- re-run with --apply to write changes.")


if __name__ == "__main__":
    main()
