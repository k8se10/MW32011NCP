#!/usr/bin/env python3
"""
MW32011NCP / iw5oat -- shared helpers for the x64_offset_fixes/ scripts.

Every script in this directory operates on ZoneCodeGenerator's own generated
output under `<repo>/tools/iw5oat/build/src/ZoneCode/Game/IW5/XAssets/*/`.
That tree is gitignored (regenerated build output, not tracked source) --
see README.md in this directory for why these scripts exist and why they
have to be re-run after every real ZoneCodeGenerator invocation rather than
being a one-time patch.
"""
import re
from pathlib import Path

# This file lives at tools/iw5oat/x64_offset_fixes/common.py -- walk up to
# tools/iw5oat/, then down into the generated ZoneCode output.
IW5OAT_ROOT = Path(__file__).resolve().parent.parent
ZONECODE_ROOT = IW5OAT_ROOT / "build" / "src" / "ZoneCode" / "Game" / "IW5"

_VAR_DECL_RE = re.compile(r"^\s*([A-Za-z_][A-Za-z0-9_]*)\*\s+var([A-Za-z_][A-Za-z0-9_]*);\s*$", re.MULTILINE)


def load_db_files():
    """Every generated *_load_db.cpp file (the LOAD path -- what Unlinker's
    own extraction needs). *_write_db.cpp (re-packing zones) has the same bug
    class but is out of scope for these scripts -- see the package README."""
    if not ZONECODE_ROOT.exists():
        raise SystemExit(
            f"ZoneCode output not found at {ZONECODE_ROOT} -- run ZoneCodeGenerator "
            f"for IW5 first (see tools/iw5oat/x64_offset_fixes/README.md)."
        )
    return sorted(ZONECODE_ROOT.glob("XAssets/*/*_load_db.cpp"))


def discover_var_to_type():
    """Maps each generated `var<Suffix>` local's Suffix back to its real
    struct type name, by scanning every `<Type>* var<Suffix>;` declaration in
    the generated *_load_db.h headers. Confirmed 1:1 (Suffix == Type name)
    across all 194 unique declarations this project has ever generated --
    kept as a real lookup (not just returning the suffix unchanged) so a
    future asset type breaking that convention fails loudly here instead of
    silently emitting a wrong offsetof() call."""
    var_to_type = {}
    for h in sorted(ZONECODE_ROOT.glob("XAssets/*/*_load_db.h")):
        text = h.read_text(encoding="utf-8", errors="replace")
        for m in _VAR_DECL_RE.finditer(text):
            type_name, suffix = m.group(1), m.group(2)
            existing = var_to_type.get(suffix)
            if existing is not None and existing != type_name:
                raise SystemExit(
                    f"var{suffix} maps to both {existing!r} and {type_name!r} "
                    f"(in {h}) -- the Suffix==Type convention these scripts rely "
                    f"on no longer holds; fix the affected regex by hand instead "
                    f"of trusting this lookup for that case."
                )
            var_to_type[suffix] = type_name
    return var_to_type


def ensure_cstddef_include(text: str) -> str:
    """offsetof() needs <cstddef>. Safe to insert unconditionally when the
    file doesn't already have it -- these are single-translation-unit
    generated .cpp files, never included by anything else."""
    if "offsetof(" in text and "#include <cstddef>" not in text:
        lines = text.split("\n")
        for i, line in enumerate(lines):
            if line.startswith("#include"):
                lines.insert(i + 1, "#include <cstddef>")
                break
        text = "\n".join(lines)
    return text
