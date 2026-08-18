// menu_expr.cpp -- see menu_expr.h for the full scope comment.
//
// Builtin function inventory: a repo-wide scan (grep -rhoE '[A-Za-z_][A-Za-z0-9_]*\('
// across every file in D:\Tools\OpenAssetTools\zone_dump\ui\, deduped, FUNC_N calls
// excluded since those are per-file functionDef references, not builtins) found ~150
// distinct real builtin names -- far more than the ~20 an earlier research pass
// catalogued from just 4-5 sample files. Implementing all 150 with real semantics isn't
// practical (many need data this harness has no source for at all, e.g. friends-list/
// Elite-clan/Facebook social-network state) -- instead:
//   - The ~25 most-frequent AND most-relevant-to-Survival-armory-screens builtins get
//     REAL GameState-backed semantics below (player/team/card-info/localvar/dvar/
//     match-rules queries, real arithmetic, real select()/when()).
//   - tablelookup()/tablelookupbyrow()/tablegetrownum() now use REAL CSV data (Phase 3,
//     2026-08-17, see menu_csv.h) -- a missing CSV (confirmed: only mp/factionTable.csv,
//     doesn't exist anywhere in the extracted dump) still degrades to a logged-once stub,
//     never silently faked. Commented in full at the call site below.
//   - Every other unrecognized function name falls through to a generic default
//     (Value::Num(0), i.e. "false"/"not right now" -- a reasonable default for a harness
//     with no real match running: not in a killcam, not spectating, no vehicle, etc.)
//     -- and is LOGGED ONCE (deduped) via fprintf(stderr,...) so an unimplemented
//     function is visible/discoverable, never silently pretending to be correct data.
#include "menu_expr.h"
#include "menu_csv.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <set>

double Value::AsNumber() const
{
    return isString ? std::atof(str.c_str()) : num;
}

std::string Value::AsString() const
{
    if (isString) return str;
    // Real .menu text fields (e.g. `exp text <expr>;`) often feed a numeric result
    // straight into a text display -- match printf's own "%g"-ish shortest-round-trip
    // behavior closely enough for a debug/dev tool (exact float formatting parity with
    // the real engine's own number-to-string isn't a goal here).
    char buf[64];
    if (num == static_cast<double>(static_cast<long long>(num))) {
        snprintf(buf, sizeof(buf), "%lld", static_cast<long long>(num));
    } else {
        snprintf(buf, sizeof(buf), "%g", num);
    }
    return buf;
}

namespace {

std::string ToLowerCopy(const std::string& s)
{
    std::string r = s;
    for (char& c : r) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return r;
}

void LogUnknownFunctionOnce(const std::string& name)
{
    static std::set<std::string> logged;
    std::string lower = ToLowerCopy(name);
    if (logged.count(lower)) return;
    logged.insert(lower);
    fprintf(stderr, "[menu_expr] unimplemented builtin '%s' -- defaulting to 0/false (Phase 2 stub, not real data)\n", name.c_str());
}

// Evaluates every arg (real .menu function calls always evaluate their arguments
// eagerly -- none of the modeled builtins below are lazy/short-circuit at the
// argument level, unlike && / || which ARE short-circuited explicitly in EvaluateExpr).
std::vector<Value> EvalArgs(const Expr* call, MenuGameState& state, const FunctionDefTable& funcs, int depth)
{
    std::vector<Value> args;
    args.reserve(call->children.size());
    for (const auto& c : call->children) args.push_back(EvaluateExpr(c.get(), state, funcs, depth + 1));
    return args;
}

Value CallBuiltin(const std::string& nameRaw, const std::vector<Value>& args, MenuGameState& state,
                   const FunctionDefTable& funcs, int depth)
{
    std::string name = ToLowerCopy(nameRaw);
    auto argNum = [&](size_t i, double def = 0.0) { return i < args.size() ? args[i].AsNumber() : def; };
    auto argStr = [&](size_t i, const std::string& def = std::string()) { return i < args.size() ? args[i].AsString() : def; };

    // ---- functionDef reference (FUNC_N()) -- checked FIRST, not a builtin at all -----
    if (!funcs.empty()) {
        auto it = funcs.find(nameRaw); // functionDef names are case-sensitive real identifiers ("FUNC_0" etc.)
        if (it != funcs.end()) {
            if (depth > 64) { // runaway/cyclic FUNC_N chain guard -- see menu_expr.h's own comment
                fprintf(stderr, "[menu_expr] functionDef recursion depth exceeded calling '%s' -- returning 0\n", nameRaw.c_str());
                return Value::Num(0);
            }
            return EvaluateExpr(it->second, state, funcs, depth + 1);
        }
    }

    // ---- core language-level builtins ------------------------------------------------
    if (name == "select") return args.size() >= 3 ? (args[0].AsBool() ? args[1] : args[2]) : Value::Num(0);
    if (name == "when") return args.empty() ? Value::Num(0) : args[0]; // identity passthrough -- wraps `visible when(...)`
    if (name == "min") return Value::Num(std::min(argNum(0), argNum(1)));
    if (name == "max") return Value::Num(std::max(argNum(0), argNum(1)));
    if (name == "sin") return Value::Num(std::sin(argNum(0)));
    if (name == "cos") return Value::Num(std::cos(argNum(0)));
    if (name == "int") return Value::Num(static_cast<double>(static_cast<long long>(argNum(0))));
    if (name == "float") return Value::Num(argNum(0));
    if (name == "string") return Value::Str(argStr(0));
    if (name == "milliseconds" || name == "gettime" || name == "localclientuimilliseconds") return Value::Num(state.NowMs());

    // ---- team / player-card / match-state (real Survival-armory-relevant queries) ----
    if (name == "team") {
        std::string key = ToLowerCopy(argStr(0));
        if (key == "name") return Value::Str(state.teamName);
        return Value::Str(state.teamName);
    }
    if (name == "player") {
        std::string key = ToLowerCopy(argStr(0));
        if (key == "teamname") return Value::Str(state.teamName);
        return Value::Num(0);
    }
    if (name == "otherteam") return Value::Str(state.teamName == "TEAM_ALLIES" ? "TEAM_AXIS" : "TEAM_ALLIES");
    if (name == "getplayercardinfo") {
        char key[64]; snprintf(key, sizeof(key), "%d_%d_%d", static_cast<int>(argNum(0)), static_cast<int>(argNum(1)), static_cast<int>(argNum(2)));
        auto it = state.playerCardInfo.find(key);
        return Value::Num(it != state.playerCardInfo.end() ? it->second : 0.0);
    }
    if (name == "getmapcustom") {
        auto it = state.mapCustom.find(argStr(0));
        return Value::Str(it != state.mapCustom.end() ? it->second : std::string());
    }
    if (name == "getmatchrulesdata") {
        char key[128]; snprintf(key, sizeof(key), "%s_%s", argStr(0).c_str(), argStr(1).c_str());
        auto it = state.matchRulesData.find(key);
        return Value::Str(it != state.matchRulesData.end() ? it->second : std::string());
    }
    if (name == "isusingmatchrulesdata") return Value::Num(state.usingMatchRulesData ? 1 : 0);
    if (name == "localvarint" || name == "localvarfloat") {
        auto it = state.localVarNum.find(argStr(0));
        return Value::Num(it != state.localVarNum.end() ? it->second : 0.0);
    }
    if (name == "localvarbool") {
        auto it = state.localVarBool.find(argStr(0));
        return Value::Num((it != state.localVarBool.end() && it->second) ? 1 : 0);
    }
    if (name == "localvarstring") {
        auto it = state.localVarStr.find(argStr(0));
        return Value::Str(it != state.localVarStr.end() ? it->second : std::string());
    }
    if (name == "dvarint" || name == "dvarfloat" || name == "dvarbool") {
        auto it = state.dvarNum.find(argStr(0));
        return Value::Num(it != state.dvarNum.end() ? it->second : 0.0);
    }
    if (name == "dvarstring") {
        auto it = state.dvarStr.find(argStr(0));
        return Value::Str(it != state.dvarStr.end() ? it->second : std::string());
    }
    if (name == "menuisopen" || name == "menuistopmost") {
        auto it = state.menuOpenState.find(argStr(0));
        return Value::Num((it != state.menuOpenState.end() && it->second) ? 1 : 0);
    }

    // ---- player-progression stand-ins (Phase 3, see MenuGameState's own comment) -----
    // getplayerdata's real arity varies by caller: 1-arg ("experience"), 2-arg
    // ("armory"+"weapon", weaponName), 3-arg (classLoc, index, "inUse") were all
    // observed across the corpus. Not modeling per-key real data (see MenuGameState
    // comment) -- the 1-arg "experience" shape returns the fake XP value directly;
    // every other shape (ownership/equip-style boolean checks) returns the blunt
    // "owns everything" toggle instead of a real per-item answer.
    if (name == "getplayerdata") {
        if (args.size() == 1 && ToLowerCopy(argStr(0)) == "experience") return Value::Num(state.fakeExperience);
        return Value::Num(state.fakeOwnsEverything ? 1.0 : 0.0);
    }
    // Real level-from-XP curve is NOT RE'd -- this is a placeholder monotonic formula
    // (not a guess at the real thresholds) purely so "raise fake XP -> level goes up ->
    // level-gated rows unlock" is directionally testable in the harness; do not treat
    // the specific divisor as meaningful.
    if (name == "levelforexperienceso") return Value::Num(std::floor(argNum(0) / 1000.0));

    // ---- text-display helpers -- real string/locstring semantics need a loaded string
    // table this harness doesn't have; return the key itself so text is at least
    // readable in the harness rather than blank, clearly not a real localization.
    if (name == "locstring") return Value::Str(argStr(0));
    if (name == "truncatetextwithellipsis") return Value::Str(argStr(0));
    if (name == "getcharbyindex") { std::string s = argStr(0); int i = static_cast<int>(argNum(1)); return Value::Str((i >= 0 && i < static_cast<int>(s.size())) ? std::string(1, s[i]) : std::string()); }
    // getTextWidth/getTextWidthModCase/getwrappedtextheight need the harness's real font
    // metrics (Phase 1's MenuGfx_MeasureTextWidthPx exists for THIS purpose but isn't
    // wired here -- these builtins' results feed layout math this Phase 2 pass doesn't
    // resolve iteratively) -- explicit 0 stub, not a guess.
    if (name == "gettextwidth" || name == "gettextwidthmodcase" || name == "getwrappedtextheight") return Value::Num(0.0);

    // ---- table lookups -- Phase 3 (2026-08-17): real CSV data, via menu_csv.h. Arg
    // order/semantics confirmed by reading every real tablelookup(byrow)? call site
    // across the 319-file corpus (menu_csv.h's own header comment has the full account):
    //   tablelookup(csvPath, keyCol, keyValue, resultCol) -- find the first DATA row
    //     (header excluded, see menu_csv.h) whose keyCol matches keyValue, return its
    //     resultCol. Real files use this both for literal-id lookups (survival_armories.csv's
    //     own col0 holds sequential row ids) and genuine string-keyed lookups (weapon
    //     asset names, faction chars, etc.) -- string comparison via Value::AsString()
    //     handles both, since a whole-number Value formats identically to how the CSV
    //     itself stores small integer ids ("0"/"1"/"2", no decimal point).
    //   tablelookupbyrow(csvPath, rowIndex, col) -- direct data-row-index (0-based,
    //     header excluded) + column access, no key matching. Confirmed via real files'
    //     own `tablelookupbyrow("mp/cardTitleTable.csv", N + pageIndex*21, 0)` paging
    //     pattern -- rowIndex is a plain sequential data-row offset.
    //   tablegetrownum(csvPath, col, value) -- inverse of tablelookup: returns the
    //     data-row INDEX (0-based) of the first row whose col matches value, or -1 if
    //     no row matches (real files use this to test "does a row for X exist" before
    //     using the index elsewhere, e.g. cardTitle flag lookups -- -1 reads as a clean
    //     "not found" sentinel for that pattern; not independently confirmed against
    //     the real engine's own not-found return value).
    if (name == "tablelookup") {
        const MenuCsvTable* table = MenuCsv_Load(argStr(0));
        if (!table) return Value::Num(0.0); // missing CSV (e.g. mp/factionTable.csv) -- logged once by MenuCsv_Load
        int keyCol = static_cast<int>(argNum(1));
        std::string keyValue = argStr(2);
        int resultCol = static_cast<int>(argNum(3));
        for (const auto& row : table->rows) {
            if (keyCol >= 0 && keyCol < static_cast<int>(row.size()) && row[keyCol] == keyValue) {
                if (resultCol >= 0 && resultCol < static_cast<int>(row.size())) return Value::Str(row[resultCol]);
                return Value::Str(std::string());
            }
        }
        return Value::Str(std::string()); // no matching row -- real files test this via `!= ""`, matches that pattern
    }
    if (name == "tablelookupbyrow") {
        const MenuCsvTable* table = MenuCsv_Load(argStr(0));
        if (!table) return Value::Num(0.0);
        int rowIndex = static_cast<int>(argNum(1));
        int col = static_cast<int>(argNum(2));
        if (rowIndex < 0 || rowIndex >= static_cast<int>(table->rows.size())) return Value::Str(std::string());
        const auto& row = table->rows[rowIndex];
        if (col < 0 || col >= static_cast<int>(row.size())) return Value::Str(std::string());
        return Value::Str(row[col]);
    }
    if (name == "tablegetrownum") {
        const MenuCsvTable* table = MenuCsv_Load(argStr(0));
        if (!table) return Value::Num(-1.0);
        int col = static_cast<int>(argNum(1));
        std::string value = argStr(2);
        for (size_t i = 0; i < table->rows.size(); ++i) {
            const auto& row = table->rows[i];
            if (col >= 0 && col < static_cast<int>(row.size()) && row[col] == value) return Value::Num(static_cast<double>(i));
        }
        return Value::Num(-1.0);
    }

    // ---- everything else: no real data source in this harness -- default to
    // "false"/"not right now", logged once so it's a visible, discoverable gap.
    LogUnknownFunctionOnce(nameRaw);
    return Value::Num(0.0);
}

} // namespace

Value EvaluateExpr(const Expr* expr, MenuGameState& state, const FunctionDefTable& funcs, int depth)
{
    if (!expr || depth > 128) return Value::Num(0); // malformed AST or runaway depth -- degrade, never crash

    switch (expr->kind) {
        case ExprKind::Number: return Value::Num(expr->numberValue);
        case ExprKind::String: return Value::Str(expr->stringValue);
        case ExprKind::Ident: {
            // A bare (non-called) identifier isn't a real "variable" in this language --
            // real files only ever use bare idents as either a functionDef reference
            // (FUNC_N with no explicit call parens is not actually observed, but handled
            // defensively here the same way) or a stray token this evaluator can't give
            // real meaning to. Try functionDef lookup first, else fall back to treating
            // it as its own literal string (harmless, visible if it shows up in text).
            auto it = funcs.find(expr->stringValue);
            if (it != funcs.end()) return EvaluateExpr(it->second, state, funcs, depth + 1);
            return Value::Str(expr->stringValue);
        }
        case ExprKind::Unary: {
            Value v = EvaluateExpr(expr->children.empty() ? nullptr : expr->children[0].get(), state, funcs, depth + 1);
            if (expr->stringValue == "!") return Value::Num(v.AsBool() ? 0 : 1);
            if (expr->stringValue == "-") return Value::Num(-v.AsNumber());
            if (expr->stringValue == "~") return Value::Num(static_cast<double>(~static_cast<long long>(v.AsNumber())));
            return v;
        }
        case ExprKind::Binary: {
            const Expr* lhsExpr = expr->children.size() > 0 ? expr->children[0].get() : nullptr;
            const Expr* rhsExpr = expr->children.size() > 1 ? expr->children[1].get() : nullptr;
            const std::string& op = expr->stringValue;
            // Short-circuit && / || the same way the real expression language would --
            // matters for real .menu patterns like `x && getplayercardinfo(...)` where
            // evaluating the RHS unconditionally would be wasted work, not correctness
            // (this evaluator's builtins are all side-effect-free), but short-circuiting
            // is still the more faithful/expected semantic to implement.
            if (op == "&&") { Value l = EvaluateExpr(lhsExpr, state, funcs, depth + 1); if (!l.AsBool()) return Value::Num(0); return Value::Num(EvaluateExpr(rhsExpr, state, funcs, depth + 1).AsBool() ? 1 : 0); }
            if (op == "||") { Value l = EvaluateExpr(lhsExpr, state, funcs, depth + 1); if (l.AsBool()) return Value::Num(1); return Value::Num(EvaluateExpr(rhsExpr, state, funcs, depth + 1).AsBool() ? 1 : 0); }
            Value l = EvaluateExpr(lhsExpr, state, funcs, depth + 1);
            Value r = EvaluateExpr(rhsExpr, state, funcs, depth + 1);
            if (op == "+") {
                // Real files use '+' for both numeric addition and (occasionally) string
                // concatenation-shaped text composition -- if EITHER side is a genuine
                // string, concatenate; otherwise numeric add.
                if (l.isString || r.isString) return Value::Str(l.AsString() + r.AsString());
                return Value::Num(l.AsNumber() + r.AsNumber());
            }
            if (op == "-") return Value::Num(l.AsNumber() - r.AsNumber());
            if (op == "*") return Value::Num(l.AsNumber() * r.AsNumber());
            if (op == "/") { double d = r.AsNumber(); return Value::Num(d != 0.0 ? l.AsNumber() / d : 0.0); }
            if (op == "%") { long long d = static_cast<long long>(r.AsNumber()); return Value::Num(d != 0 ? static_cast<double>(static_cast<long long>(l.AsNumber()) % d) : 0.0); }
            if (op == "^") return Value::Num(static_cast<double>(static_cast<long long>(l.AsNumber()) ^ static_cast<long long>(r.AsNumber())));
            if (op == "==") return Value::Num((l.isString || r.isString) ? (l.AsString() == r.AsString() ? 1 : 0) : (l.AsNumber() == r.AsNumber() ? 1 : 0));
            if (op == "!=") return Value::Num((l.isString || r.isString) ? (l.AsString() != r.AsString() ? 1 : 0) : (l.AsNumber() != r.AsNumber() ? 1 : 0));
            if (op == "<") return Value::Num(l.AsNumber() < r.AsNumber() ? 1 : 0);
            if (op == ">") return Value::Num(l.AsNumber() > r.AsNumber() ? 1 : 0);
            if (op == "<=") return Value::Num(l.AsNumber() <= r.AsNumber() ? 1 : 0);
            if (op == ">=") return Value::Num(l.AsNumber() >= r.AsNumber() ? 1 : 0);
            return Value::Num(0);
        }
        case ExprKind::Ternary: {
            Value cond = EvaluateExpr(expr->children.size() > 0 ? expr->children[0].get() : nullptr, state, funcs, depth + 1);
            const Expr* branch = (expr->children.size() > 2) ? (cond.AsBool() ? expr->children[1].get() : expr->children[2].get()) : nullptr;
            return EvaluateExpr(branch, state, funcs, depth + 1);
        }
        case ExprKind::Call: {
            std::vector<Value> args = EvalArgs(expr, state, funcs, depth);
            return CallBuiltin(expr->stringValue, args, state, funcs, depth);
        }
    }
    return Value::Num(0);
}
