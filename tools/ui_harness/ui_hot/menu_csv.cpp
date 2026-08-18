// menu_csv.cpp -- see menu_csv.h for the full scope/path-resolution comment.
#include "menu_csv.h"

#include <cstdio>
#include <map>
#include <memory>

namespace {

std::string ToLowerAscii(const std::string& s)
{
    std::string r = s;
    for (char& c : r) c = static_cast<char>(::tolower(static_cast<unsigned char>(c)));
    return r;
}

// Splits one CSV line into fields. Handles double-quoted fields (embedded commas,
// and a doubled "" as an escaped literal quote, the standard CSV convention) -- only
// one real file in the corpus (sp/deathquotetable.csv) actually uses quoting, but
// getting this right is cheap and avoids a silent data-corruption trap if some other
// CSV this pass didn't sample also needs it.
std::vector<std::string> SplitCsvLine(const std::string& line)
{
    std::vector<std::string> fields;
    std::string cur;
    bool inQuotes = false;
    for (size_t i = 0; i < line.size(); ++i) {
        char c = line[i];
        if (inQuotes) {
            if (c == '"') {
                if (i + 1 < line.size() && line[i + 1] == '"') { cur += '"'; ++i; }
                else inQuotes = false;
            } else {
                cur += c;
            }
        } else {
            if (c == '"') inQuotes = true;
            else if (c == ',') { fields.push_back(cur); cur.clear(); }
            else if (c == '\r') { /* strip stray CR if not already trimmed */ }
            else cur += c;
        }
    }
    fields.push_back(cur);
    return fields;
}

std::unique_ptr<MenuCsvTable> LoadCsvFromDisk(const std::string& fullPath)
{
    FILE* f = nullptr;
    if (fopen_s(&f, fullPath.c_str(), "rb") != 0 || !f) return nullptr;

    auto table = std::make_unique<MenuCsvTable>();
    std::string line;
    bool sawHeader = false;
    char buf[4096];
    while (fgets(buf, sizeof(buf), f)) {
        line = buf;
        while (!line.empty() && (line.back() == '\n' || line.back() == '\r')) line.pop_back();
        if (!sawHeader) { sawHeader = true; continue; } // row 0 is a header/column-index row, see menu_csv.h
        if (line.empty()) continue;
        table->rows.push_back(SplitCsvLine(line));
    }
    fclose(f);
    return table;
}

std::map<std::string, std::unique_ptr<MenuCsvTable>>& Cache()
{
    static std::map<std::string, std::unique_ptr<MenuCsvTable>> cache;
    return cache;
}

} // namespace

const MenuCsvTable* MenuCsv_Load(const std::string& zoneRelativePath)
{
    std::string key = ToLowerAscii(zoneRelativePath);
    auto& cache = Cache();
    auto it = cache.find(key);
    if (it != cache.end()) return it->second.get(); // nullptr entry = confirmed-missing, cached

    // Primary root: most real CSVs resolve directly here (e.g. zone_dump\mp\perktable.csv).
    std::string primary = "D:\\Tools\\OpenAssetTools\\zone_dump\\" + zoneRelativePath;
    std::unique_ptr<MenuCsvTable> table = LoadCsvFromDisk(primary);
    if (!table) {
        // Fallback: a small number of assets (confirmed: sp/survival_armories.csv) only
        // exist under the "ui" zone's own extraction output -- see menu_csv.h.
        std::string fallback = "D:\\Tools\\OpenAssetTools\\zone_dump\\ui\\" + zoneRelativePath;
        table = LoadCsvFromDisk(fallback);
    }
    if (!table) {
        fprintf(stderr, "[menu_csv] CSV not found under either root: '%s' -- tablelookup() against it will stub to 0\n",
            zoneRelativePath.c_str());
    }
    const MenuCsvTable* result = table.get();
    cache[key] = std::move(table); // stores nullptr on failure too -- one stat attempt per unique path, ever
    return result;
}
