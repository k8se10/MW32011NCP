#pragma once

// menu_expr -- Phase 2 (2026-08-17) of the real .menu file renderer for
// tools/ui_harness. Expression AST + AST-walking evaluator + a fake "game state"
// standing in for a live match, since the harness never has a real game running.
//
// Lives under tools/ui_harness/ui_hot/ like menu_parser/menu_render -- STL-permitted,
// never compiled into the real shipped proxy_d3d9.dll (see ui_hot.vcxproj).
//
// SCOPE: real evaluation of the arithmetic/comparison/boolean/ternary operators and a
// broad set of the real builtin functions found by scanning ALL 319 real .menu files in
// D:\Tools\OpenAssetTools\zone_dump\ui\ (not just the 4-5 sample files an earlier research
// pass used) -- see menu_expr.cpp's own top comment for the full inventory and which
// functions got real GameState-backed semantics vs. a documented stub. tablelookup()/
// tablelookupbyrow()/tablegetrownum() now resolve against real CSV data (Phase 3,
// menu_csv.h) -- see menu_expr.cpp for the confirmed arg-order/semantics per function.

#include <string>
#include <vector>
#include <map>
#include <memory>

enum class ExprKind { Number, String, Ident, Unary, Binary, Ternary, Call };

// One node. Which fields are meaningful depends on `kind`:
//   Number:  numberValue
//   String:  stringValue (literal content)
//   Ident:   stringValue (the bare identifier's name -- see EvaluateExpr's own comment on
//            how a bare (non-called) identifier is treated, since this language's real
//            engine doesn't have "variables" in the C sense)
//   Unary:   stringValue = operator ("!" "-" "~"), children[0] = operand
//   Binary:  stringValue = operator, children[0]/children[1] = lhs/rhs
//   Ternary: children[0]/[1]/[2] = cond/then/else
//   Call:    stringValue = function name, children = args (0 or more)
struct Expr
{
    ExprKind kind = ExprKind::Number;
    double numberValue = 0.0;
    std::string stringValue;
    std::vector<std::unique_ptr<Expr>> children;
};
using ExprPtr = std::unique_ptr<Expr>;

// This language is weakly typed the same way the real engine's expression system is --
// booleans are just 0/1 numbers, and a "string" result can still be tested truthy/falsy.
// Kept as a simple tagged union (not std::variant) to match this codebase's existing
// plain-struct style (menu_parser.h's own ParsedItemDef, etc.).
struct Value
{
    bool isString = false;
    double num = 0.0;
    std::string str;

    static Value Num(double v) { Value r; r.isString = false; r.num = v; return r; }
    static Value Str(std::string v) { Value r; r.isString = true; r.str = std::move(v); return r; }

    double AsNumber() const;
    bool AsBool() const { return isString ? !str.empty() : num != 0.0; }
    std::string AsString() const;
};

// Fake game-state stub, standing in for whatever a real match would supply. Editable
// LIVE from the harness (see main.cpp's new hotkeys) so a Survival armory screen's real
// conditional logic (team-locked weapons, already-unlocked attachments, etc.) can
// actually be exercised and visually verified instead of always evaluating against one
// fixed default state.
struct MenuGameState
{
    std::string teamName = "TEAM_ALLIES";       // player("teamname"), team("name")
    bool usingMatchRulesData = false;            // isusingmatchrulesdata()
    double fakeMillisBase = 0.0;                 // milliseconds() -- advance via the harness

    // getplayercardinfo(a,b,c) -- keyed "a_b_c" (string-composed, simplest correct map key
    // without a <tuple>-comparison dance for a struct this small-scale).
    std::map<std::string, double> playerCardInfo;
    std::map<std::string, std::string> mapCustom;      // getmapcustom(key)
    std::map<std::string, double> localVarNum;         // localvarint/localvarfloat, keyed by name
    std::map<std::string, bool> localVarBool;          // localvarbool, keyed by name
    std::map<std::string, std::string> localVarStr;    // localvarstring, keyed by name
    std::map<std::string, double> dvarNum;             // dvarint/dvarfloat/dvarbool, keyed by name
    std::map<std::string, std::string> dvarStr;         // dvarstring, keyed by name
    std::map<std::string, std::string> matchRulesData;  // getmatchrulesdata(a,b), keyed "a_b"
    std::map<std::string, bool> menuOpenState;          // menuisopen(name) -- all false unless set

    // Phase 3 (2026-08-17): getplayerdata(...)/levelforexperienceso() -- both were
    // "unimplemented, defaults to 0" in Phase 2, but real Survival-armory files gate
    // almost every weapon row's locked/owned/upgrade-available state through exactly
    // these two ("levelforexperienceso(getplayerdata(\"experience\")) >= tablelookup(...)"
    // and "getplayerdata(\"armory\"+\"weapon\", <weaponName>) > 0"), so leaving them at a
    // hard 0 makes every row look permanently locked regardless of MenuGameState toggles.
    // Rather than pre-populate a key per real weapon name (this harness doesn't know the
    // full real id scheme those keys use), two blunt, honestly-named toggles: a fake XP
    // value (feeds the 1-arg "experience" call) and a single "pretend everything's
    // owned" flag (feeds the 2-3 arg ownership-shaped calls) -- see menu_expr.cpp's
    // getplayerdata implementation for exactly which arg shape maps to which.
    double fakeExperience = 0.0;
    bool fakeOwnsEverything = false;

    double NowMs() const { return fakeMillisBase; }
};

// name -> parsed `value <expr>;` body. One table per .menu FILE (functionDefs are
// file-scoped in every real sample) -- see ParsedMenuFile::functionDefs in menu_parser.h.
using FunctionDefTable = std::map<std::string, const Expr*>;

// AST-walking evaluator. `depth` guards a functionDef that (accidentally, or via a real
// cycle across FUNC_N definitions) ends up calling itself -- this is dumped/test data,
// not engine-verified output, so a defensive limit is cheap insurance. Never throws --
// an unrecognized/malformed node degrades to Value::Num(0), matching this project's own
// "missing/bad input degrades gracefully" standard.
Value EvaluateExpr(const Expr* expr, MenuGameState& state, const FunctionDefTable& funcs, int depth = 0);
