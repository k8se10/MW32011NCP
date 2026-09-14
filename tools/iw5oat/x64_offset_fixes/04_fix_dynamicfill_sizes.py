#!/usr/bin/env python3
"""
MW32011NCP / iw5oat, 2026-09-14 -- fixes literal byte-size arguments inside
`LoadDynamicFill_<Type>` functions that 01's own regexes don't reach (a
distinct call shape: `m_stream.AppendToFill(N)`/`AppendToFill(N * count)`,
not `fillAccessor.Fill*`/`LoadWithFill`).

Each `LoadDynamicFill_<Type>` function that owns its own AppendToFill call
looks like:
    const auto fillAccessor = m_stream.AppendToFill(N).AtOffset(parentFill.Offset());
    ... reads N bytes of fixed header fields via fillAccessor ...
    return LoadDynamicFill_<Next>(fillAccessor.AtOffset(N)) + offsetof(Type, dynamicField);
    (or a dynamicArrayEntries-based tail read + the same offsetof pattern)

N is the x86-computed byte size of Type's fixed-size prefix (every field
before `dynamicField`), hardcoded by ZoneCodeGenerator. This is wrong on
x64 whenever that prefix's own real alignment requirement differs from
x86 -- not just "does the prefix contain a pointer directly", but "does
`dynamicField`'s own alignment requirement (driven by a pointer possibly
several levels of embedding deep) force different padding before it".
Confirmed real via XAnimDeltaPartQuat2 and siblings: an embedded struct
that itself starts with a pointer forces 8-byte alignment on x64 vs
4-byte on x86, even though the field IMMEDIATELY preceding it is just a
plain uint16_t.

Fix (part A): reuse the exact value the SAME function's own return
statement already computes via a real, compiler-verified
`offsetof(Type, dynamicField)` call, replacing every occurrence of the
literal N within that function body (the header AppendToFill AND any
later `.AtOffset(N)` re-seek into the same already-loaded buffer) with
that identical offsetof expression.

Fix (part B): separately, `m_stream.AppendToFill(dynamicArrayEntries * N)`
sites (the dynamic ARRAY's own per-element byte size, a different literal
from the header size above) are wrong wherever the element type contains a
pointer -- confirmed via MaterialTechnique's own passArray (elements are
MaterialPass, which contains 4 pointers; the literal 20 was x86's
sizeof(MaterialPass), needed to become sizeof(MaterialPass[1]) matching
the function's own return statement, which already computed it correctly
via sizeof() for the OTHER use of the same count). Every
`AppendToFill(dynamicArrayEntries * N)` site is checked against the
function's own return-statement sizeof(Type[1]) the same way.

Run this after 01 (order doesn't matter relative to 02/03 -- disjoint call
shapes).
"""
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from common import load_db_files  # noqa: E402

FUNC_RE = re.compile(
    r"(size_t Loader_\w+::LoadDynamicFill_(\w+)\(const ZoneStreamFillReadAccessor& parentFill\)\s*\{)"
    r"(.*?)"
    r"(\n\})",
    re.DOTALL,
)
HEADER_APPEND_RE = re.compile(r"m_stream\.AppendToFill\((\d+)\)\.AtOffset\(parentFill\.Offset\(\)\);")
RETURN_OFFSETOF_RE = re.compile(r"\+\s*offsetof\((\w+),\s*(\w+)\)\s*;\s*$", re.MULTILINE)
DYNAMIC_ARRAY_APPEND_RE = re.compile(r"m_stream\.AppendToFill\(dynamicArrayEntries \* (\d+)\);")
RETURN_SIZEOF_ARRAY_RE = re.compile(r"dynamicArrayEntries \* sizeof\((\w+)\[1\]\)")


def fix_header_size(body: str, fixes: list) -> str:
    append_m = HEADER_APPEND_RE.search(body)
    ret_m = RETURN_OFFSETOF_RE.search(body)
    if not append_m or not ret_m:
        return body
    literal = append_m.group(1)
    struct_name, field_name = ret_m.group(1), ret_m.group(2)
    offsetof_expr = f"offsetof({struct_name}, {field_name})"
    new_body, count = re.subn(rf"(?<![\w.]){re.escape(literal)}(?!\d)", offsetof_expr, body)
    if count:
        fixes.append((f"header size {literal} -> {offsetof_expr}", count))
    return new_body


def fix_dynamic_array_element_size(body: str, fixes: list) -> str:
    array_m = DYNAMIC_ARRAY_APPEND_RE.search(body)
    ret_m = RETURN_SIZEOF_ARRAY_RE.search(body)
    if not array_m or not ret_m:
        return body
    literal, elem_type = array_m.group(1), ret_m.group(1)
    sizeof_expr = f"sizeof({elem_type}[1])"
    old = f"m_stream.AppendToFill(dynamicArrayEntries * {literal});"
    new = f"m_stream.AppendToFill(dynamicArrayEntries * {sizeof_expr});"
    if old in body:
        fixes.append((f"dynamic-array element size {literal} -> {sizeof_expr}", 1))
        body = body.replace(old, new)
    return body


def process_file(path: Path, dry_run: bool):
    text = path.read_text(encoding="utf-8", errors="replace")
    file_fixes = []

    def func_repl(m):
        header, type_name_func, body, closing = m.group(1), m.group(2), m.group(3), m.group(4)
        fixes = []
        body = fix_header_size(body, fixes)
        body = fix_dynamic_array_element_size(body, fixes)
        for desc, count in fixes:
            file_fixes.append((type_name_func, desc, count))
        return header + body + closing

    new_text = FUNC_RE.sub(func_repl, text)
    if new_text != text:
        if not dry_run:
            path.write_text(new_text, encoding="utf-8")
        return file_fixes
    return []


def main():
    dry_run = "--apply" not in sys.argv
    total = 0
    for f in load_db_files():
        fixes = process_file(f, dry_run)
        if fixes:
            print(f"  {f.name}:")
            for type_name_func, desc, count in fixes:
                print(f"    {type_name_func}: {desc} ({count} occurrence(s))")
                total += count
    print(f"\nTotal literal replacements: {total}")
    if dry_run:
        print("Dry run only -- re-run with --apply to write changes.")


if __name__ == "__main__":
    main()
