#!/usr/bin/env python3
"""
MW32011NCP / iw5oat, 2026-09-14 -- automated x64 field-offset fix.

ZoneCodeGenerator's generated *_load_db.cpp files (per-asset-type loaders,
one per IW5 asset type) hardcode literal byte offsets computed assuming
x86 layout (4-byte pointers, no arch-aware padding) for every
`fillAccessor.Fill(...)`/`fillAccessor.FillPtr(...)`/`fillAccessor.FillArray(...)`
call, and a literal struct-size byte count for the `m_stream.LoadWithFill(N)`
call that precedes each `FillStruct_<Type>` invocation. This is wrong for
x64 (8-byte pointers, real alignment padding) -- confirmed both by native
Ghidra decompile (the dispatch record, the XAssetList header -- see
re_notes/x64_migration/fastfile_format_research.md in the parent repo) and
by the real compiler's own layout differing from the generator's assumed
one (RawFile: generator assumed 16 bytes/x86 offsets, real x64 compiler
layout is 24 bytes with a different offset for `buffer`).

Rather than hand-deriving and hand-patching each asset type's own offset
table, this script rewrites every offset literal into a compiler-verified
`offsetof(Type, field)` (or `sizeof(Type)` for LoadWithFill) expression
instead of a fixed number -- letting the REAL x64 MSVC compiler compute the
correct value for whatever target the code is actually built for, rather
than trying to precompute and hardcode a number ourselves. This is safe
because every field-access expression this handles is exactly one level
deep (`var<Type>->field`, `var<Type>->field[N]`, or `var<Type>->field[N][M]`
with only compile-time-constant indices -- all valid offsetof
member-designators), and the loop-indexed exceptions
(`var<Type>->field[i], BASE + STRIDE * i`) are pointer arrays, so
BASE/STRIDE become `offsetof(Type, field)` / `sizeof(void*)`.

Scope: *_load_db.cpp files only (the LOAD path -- what Unlinker's own
extraction needs). *_write_db.cpp (re-packing zones) has the same bug class
but is out of scope -- not needed for extraction.

Run order: this is 01 of the x64_offset_fixes/ sequence -- see this
directory's own README.md for why a sequence is needed at all (later
scripts fix real gaps this one's own regexes miss, including one genuine
cross-function-boundary false-positive this script's own struct-array
pattern can produce -- fixed by 03_fix_ptrarrayfill_crosscontam.py).
"""
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from common import load_db_files, discover_var_to_type, ensure_cstddef_include  # noqa: E402

import re  # noqa: E402

var_to_type = discover_var_to_type()

# Case 1: loop-indexed pointer-array calls, e.g.
#   fillAccessor.FillPtr(varMaterialTechniqueSet->techniques[i], 12 + 4 * i);
LOOP_RE = re.compile(
    r"fillAccessor\.(Fill|FillPtr)\(var([A-Za-z_][A-Za-z0-9_]*)->([A-Za-z_][A-Za-z0-9_]*)\[i\],\s*\d+u?\s*\+\s*\d+u?\s*\*\s*i\);"
)

# Case 2: plain/fixed-index calls, e.g.
#   fillAccessor.FillPtr(varRawFile->buffer, 12);
#   fillAccessor.FillPtr(varclipMap_t->dynEntDefList[0], 200);
#   fillAccessor.FillPtr(varGfxWorldDpvsDynamic->dynEntVisData[0][1], 28);
FIXED_RE = re.compile(
    r"fillAccessor\.(Fill|FillPtr)\(var([A-Za-z_][A-Za-z0-9_]*)->([A-Za-z_][A-Za-z0-9_\[\]]*),\s*\d+u?\);"
)

# LoadWithFill size, e.g.
#   FillStruct_RawFile(m_stream.LoadWithFill(16));
LOADWITHFILL_RE = re.compile(
    r"FillStruct_([A-Za-z_][A-Za-z0-9_]*)\(m_stream\.LoadWithFill\(\d+u?\)\)"
)

# Case 3: FillArray calls, e.g.
#   fillAccessor.FillArray(varwater_t->winddir, 36);
FILLARRAY_RE = re.compile(
    r"fillAccessor\.FillArray\(var([A-Za-z_][A-Za-z0-9_]*)->([A-Za-z_][A-Za-z0-9_\[\]]*),\s*\d+u?\);"
)

# Case 4: struct-array fill pair, e.g.
#   const auto arrayFill = m_stream.LoadWithFill(12 * count);
#   ...
#   FillStruct_MaterialTextureDef(arrayFill.AtOffset(0 + 12 * index));
STRUCT_ARRAY_RE = re.compile(
    r"m_stream\.LoadWithFill\(\d+u?\s*\*\s*count\)(.*?)"
    r"FillStruct_([A-Za-z_][A-Za-z0-9_]*)\(arrayFill\.AtOffset\(0\s*\+\s*\d+u?\s*\*\s*index\)\)",
    re.DOTALL,
)

# Case 5: pointer-array fill (x86 4-byte stride -> x64 sizeof(void*))
#   const auto ptrArrayFill = m_stream.LoadWithFill(4 * count);
#   ptrArrayFill.FillPtr(varXPtr[index], 4 * index);
#   m_stream.AddPointerLookup(&varXPtr[index], ptrArrayFill.BlockBuffer(4 * index));
PTR_ARRAY_LOADWITHFILL_RE = re.compile(r"ptrArrayFill = m_stream\.LoadWithFill\(\d+u?\s*\*\s*count\);")
PTR_ARRAY_FILLPTR_RE = re.compile(r"ptrArrayFill\.FillPtr\(([^,]+),\s*\d+u?\s*\*\s*index\);")
PTR_ARRAY_BLOCKBUFFER_RE = re.compile(r"ptrArrayFill\.BlockBuffer\(\d+u?\s*\*\s*index\)")

# Case 6: nested FillStruct at a fixed byte offset, e.g.
#   varMaterialTextureDefInfo = &varMaterialTextureDef->u;
#   FillStruct_MaterialTextureDefInfo(fillAccessor.AtOffset(8));
# (field may carry a fixed numeric index, e.g. `passArray[0]` -- a valid
# offsetof member-designator, distinct from Case 7's loop-indexed `[i]`)
NESTED_FILLSTRUCT_RE = re.compile(
    r"(var[A-Za-z_]\w*\s*=\s*&var([A-Za-z_]\w*)->([A-Za-z_]\w*(?:\[\d+\])?);(\s*\n\s*))"
    r"FillStruct_(\w+)\(fillAccessor\.AtOffset\(\d+u?\)\);"
)

# Case 7: nested FillStruct array (loop-indexed sub-struct), e.g.
#   varMaterialPass = &varMaterialTechnique->passArray[i];
#   FillStruct_MaterialPass(fillAccessor.AtOffset(8 + i * 20));
NESTED_FILLSTRUCT_LOOP_RE = re.compile(
    r"(var[A-Za-z_]\w*\s*=\s*&var([A-Za-z_]\w*)->([A-Za-z_]\w*)\[i\];(\s*\n\s*))"
    r"FillStruct_(\w+)\(fillAccessor\.AtOffset\(\d+u?\s*\+\s*(?:i\s*\*\s*\d+u?|\d+u?\s*\*\s*i)\)\);"
)

# Case 8: AddPointerLookup registering a field's own BlockBuffer offset, e.g.
#   m_stream.AddPointerLookup(&varClipInfo->planes, fillAccessor.BlockBuffer(4));
BLOCKBUFFER_LOOP_RE = re.compile(
    r"m_stream\.AddPointerLookup\(&var([A-Za-z_]\w*)->([A-Za-z_]\w*)\[i\],\s*"
    r"fillAccessor\.BlockBuffer\(\d+u?\s*\+\s*\d+u?\s*\*\s*i\)\);"
)
BLOCKBUFFER_FIXED_RE = re.compile(
    r"m_stream\.AddPointerLookup\(&var([A-Za-z_]\w*)->([A-Za-z_][A-Za-z0-9_\[\]]*),\s*"
    r"fillAccessor\.BlockBuffer\(\d+u?\)\);"
)


def loop_repl(m):
    method, var_suffix, field = m.group(1), m.group(2), m.group(3)
    type_name = var_to_type.get(var_suffix, var_suffix)
    return f"fillAccessor.{method}(var{var_suffix}->{field}[i], offsetof({type_name}, {field}) + sizeof(void*) * i);"


def fixed_repl(m):
    method, var_suffix, field_expr = m.group(1), m.group(2), m.group(3)
    type_name = var_to_type.get(var_suffix, var_suffix)
    return f"fillAccessor.{method}(var{var_suffix}->{field_expr}, offsetof({type_name}, {field_expr}));"


def loadwithfill_repl(m):
    type_name = m.group(1)
    return f"FillStruct_{type_name}(m_stream.LoadWithFill(sizeof({type_name})))"


def fillarray_repl(m):
    var_suffix, field_expr = m.group(1), m.group(2)
    type_name = var_to_type.get(var_suffix, var_suffix)
    return f"fillAccessor.FillArray(var{var_suffix}->{field_expr}, offsetof({type_name}, {field_expr}));"


def struct_array_repl(m):
    middle, type_name = m.group(1), m.group(2)
    return (
        f"m_stream.LoadWithFill(sizeof({type_name}) * count){middle}"
        f"FillStruct_{type_name}(arrayFill.AtOffset(0 + sizeof({type_name}) * index))"
    )


def ptr_loadwithfill_repl(_m):
    return "ptrArrayFill = m_stream.LoadWithFill(sizeof(void*) * count);"


def ptr_fillptr_repl(m):
    return f"ptrArrayFill.FillPtr({m.group(1)}, sizeof(void*) * index);"


def ptr_blockbuffer_repl(_m):
    return "ptrArrayFill.BlockBuffer(sizeof(void*) * index)"


def nested_fillstruct_repl(m):
    assign_line, outer_suffix, field, inner_type = m.group(1), m.group(2), m.group(3), m.group(5)
    outer_type = var_to_type.get(outer_suffix, outer_suffix)
    return f"{assign_line}FillStruct_{inner_type}(fillAccessor.AtOffset(offsetof({outer_type}, {field})));"


def nested_fillstruct_loop_repl(m):
    assign_line, outer_suffix, field, inner_type = m.group(1), m.group(2), m.group(3), m.group(5)
    outer_type = var_to_type.get(outer_suffix, outer_suffix)
    return (
        f"{assign_line}FillStruct_{inner_type}(fillAccessor.AtOffset("
        f"offsetof({outer_type}, {field}) + i * sizeof({inner_type})));"
    )


def blockbuffer_loop_repl(m):
    outer_suffix, field = m.group(1), m.group(2)
    outer_type = var_to_type.get(outer_suffix, outer_suffix)
    return (
        f"m_stream.AddPointerLookup(&var{outer_suffix}->{field}[i], "
        f"fillAccessor.BlockBuffer(offsetof({outer_type}, {field}) + sizeof(void*) * i));"
    )


def blockbuffer_fixed_repl(m):
    outer_suffix, field_expr = m.group(1), m.group(2)
    outer_type = var_to_type.get(outer_suffix, outer_suffix)
    return (
        f"m_stream.AddPointerLookup(&var{outer_suffix}->{field_expr}, "
        f"fillAccessor.BlockBuffer(offsetof({outer_type}, {field_expr})));"
    )


def process_file(path: Path, dry_run: bool):
    text = path.read_text(encoding="utf-8", errors="replace")

    counts = []
    for regex, repl in (
        (LOOP_RE, loop_repl),
        (FIXED_RE, fixed_repl),
        (LOADWITHFILL_RE, loadwithfill_repl),
        (FILLARRAY_RE, fillarray_repl),
        (STRUCT_ARRAY_RE, struct_array_repl),
        (PTR_ARRAY_LOADWITHFILL_RE, ptr_loadwithfill_repl),
        (PTR_ARRAY_FILLPTR_RE, ptr_fillptr_repl),
        (PTR_ARRAY_BLOCKBUFFER_RE, ptr_blockbuffer_repl),
        # loop variants before fixed variants (more specific first)
        (NESTED_FILLSTRUCT_LOOP_RE, nested_fillstruct_loop_repl),
        (NESTED_FILLSTRUCT_RE, nested_fillstruct_repl),
        (BLOCKBUFFER_LOOP_RE, blockbuffer_loop_repl),
        (BLOCKBUFFER_FIXED_RE, blockbuffer_fixed_repl),
    ):
        counts.append(len(regex.findall(text)))
        text = regex.sub(repl, text)

    total = sum(counts)
    if total == 0:
        return 0

    text = ensure_cstddef_include(text)
    if not dry_run:
        path.write_text(text, encoding="utf-8")
    return total


def main():
    dry_run = "--apply" not in sys.argv
    files = load_db_files()
    print(f"Found {len(files)} *_load_db.cpp files. Mode: {'DRY RUN' if dry_run else 'APPLYING'}")

    grand_total = 0
    files_touched = 0
    for f in files:
        total = process_file(f, dry_run)
        if total:
            files_touched += 1
            grand_total += total
            print(f"  {f.name}: {total} substitution(s)")

    print(f"\nTotal substitutions: {grand_total} across {files_touched} files (of {len(files)} scanned).")
    if dry_run:
        print("Dry run only -- re-run with --apply to write changes.")


if __name__ == "__main__":
    main()
