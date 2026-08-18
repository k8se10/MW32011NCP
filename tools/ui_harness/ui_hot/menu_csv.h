#pragma once

// menu_csv -- Phase 3 (2026-08-17) of the real .menu file renderer for
// tools/ui_harness. Loads the real CSV data tables `tablelookup()`/`tablelookupbyrow()`
// reference (e.g. "sp/survival_armories.csv") so those builtins (menu_expr.cpp,
// previously explicitly stubbed at 0 in Phase 2) can return REAL data instead of a
// placeholder.
//
// Lives under tools/ui_harness/ui_hot/ like the rest of this pass -- STL-permitted,
// never compiled into the real shipped proxy_d3d9.dll.
//
// Path resolution: real .menu files reference CSVs by a zone-relative path (e.g.
// "mp/factionTable.csv", "sp/survival_armories.csv") -- confirmed against the real
// extracted dump that MOST of these resolve directly under
// D:\Tools\OpenAssetTools\zone_dump\<path> (e.g. zone_dump\mp\perktable.csv exists),
// but "sp/survival_armories.csv" specifically is an exception -- it only exists at
// zone_dump\ui\sp\survival_armories.csv (an OpenAssetTools extraction quirk: this one
// asset landed under the "ui" zone's own output folder rather than a shared top-level
// "sp" folder). MenuCsv_Load tries the direct path FIRST, then a "ui\" prefixed
// fallback, rather than hardcoding a special case for this one file.
//
// "mp/factionTable.csv" itself does not exist ANYWHERE in the extracted dump (confirmed
// this session, both directly and via the "ui\" fallback) -- a real, known, low-impact
// gap (MP-team-color-related, irrelevant to Survival), not a bug. MenuCsv_Load returns
// nullptr for it; callers (menu_expr.cpp) degrade to a stubbed 0 with a one-time logged
// note, same "visible gap, not silently wrong" standard as Phase 2's unimplemented
// builtins.

#include <string>
#include <vector>

struct MenuCsvTable
{
    // Header row (row 0 of the real file, e.g. survival_armories.csv's "a0,b1,c2,...")
    // is INTENTIONALLY EXCLUDED here -- every real tablelookup/tablelookupbyrow call
    // site observed across the 319-file corpus indexes rows starting at the first real
    // DATA row as index 0 (confirmed: survival_armories.csv's own column 0 holds
    // sequential ids 0,1,2,... starting on the first data row, and
    // tablelookupbyrow("mp/cardTitleTable.csv", 1 + pageIndex*21, 0)-style calls are
    // only consistent with a data-row-relative index). `rows[i][c]` is data row i,
    // column c (both 0-based), all values kept as raw strings -- callers convert via
    // Value::AsNumber()/AsString() same as every other builtin's return.
    std::vector<std::vector<std::string>> rows;
};

// Loads and caches a CSV by its zone-relative path exactly as a real .menu file would
// reference it (case-insensitive lookup/cache key -- real files aren't consistent
// about e.g. "factionTable.csv" vs "factiontable.csv" casing, and Windows' filesystem
// doesn't care either). Returns nullptr (cached, so a missing file is only ever
// stat'd once) if the file can't be found under either candidate root. Never throws.
const MenuCsvTable* MenuCsv_Load(const std::string& zoneRelativePath);
