// menu_parser.cpp -- see menu_parser.h for the full scope/design comment.
#include "menu_parser.h"

#include <cstdio>
#include <cstring>
#include <cctype>
#include <cstdlib>
#include <memory>

namespace {

// ---- Tokenizer ---------------------------------------------------------------------
//
// Real .menu files use no comment syntax observed in any sampled file (survival_armory_
// weapon.menu, waves.menu, stance.menu, pc_options_video_ingame.menu) -- not handled
// here; if a future file turns out to have one, tokens from inside it would just get
// parsed as garbage fields and skipped by the generic fallback (see ParseFieldGeneric
// below), not a crash.
enum class TokKind { End, Ident, Number, String, Punct };

struct Token
{
    TokKind kind = TokKind::End;
    std::string text;   // raw text (identifier name, number text, unescaped string content, or the punct char)
    double num = 0.0;   // valid only when kind == Number
};

class Tokenizer
{
public:
    explicit Tokenizer(const std::string& src) : m_src(src) {}

    // Returns the token at `pos` without consuming, lexing lazily/caching as we go --
    // simplest correct approach for a file this size (the largest real .menu file is a
    // few hundred KB at most) is to just lex the whole thing up front.
    void LexAll()
    {
        size_t i = 0;
        const size_t n = m_src.size();
        while (i < n) {
            char c = m_src[i];
            if (std::isspace(static_cast<unsigned char>(c))) { ++i; continue; }

            if (c == '"') {
                ++i;
                std::string s;
                while (i < n && m_src[i] != '"') {
                    // No escape-sequence handling observed as needed in sampled files;
                    // a literal backslash is passed through as-is rather than
                    // interpreted, which is safe (never crashes) even if wrong for some
                    // unseen file.
                    s.push_back(m_src[i]);
                    ++i;
                }
                if (i < n) ++i; // skip closing quote
                Token t; t.kind = TokKind::String; t.text = s;
                m_tokens.push_back(t);
                continue;
            }

            if (std::isdigit(static_cast<unsigned char>(c)) ||
                (c == '.' && i + 1 < n && std::isdigit(static_cast<unsigned char>(m_src[i + 1])))) {
                size_t start = i;
                while (i < n && (std::isdigit(static_cast<unsigned char>(m_src[i])) || m_src[i] == '.')) ++i;
                std::string numText = m_src.substr(start, i - start);
                Token t; t.kind = TokKind::Number; t.text = numText; t.num = std::atof(numText.c_str());
                m_tokens.push_back(t);
                continue;
            }

            if (std::isalpha(static_cast<unsigned char>(c)) || c == '_' || c == '@') {
                size_t start = i;
                while (i < n && (std::isalnum(static_cast<unsigned char>(m_src[i])) || m_src[i] == '_' || m_src[i] == '@')) ++i;
                Token t; t.kind = TokKind::Ident; t.text = m_src.substr(start, i - start);
                m_tokens.push_back(t);
                continue;
            }

            // Two-char operators (only matters for the expression-boundary skipper --
            // Phase 1 never evaluates these, just needs to recognize them as "still
            // part of an expression" so it doesn't stop early).
            if (i + 1 < n) {
                char c2 = m_src[i + 1];
                if ((c == '=' && c2 == '=') || (c == '!' && c2 == '=') ||
                    (c == '&' && c2 == '&') || (c == '|' && c2 == '|') ||
                    (c == '<' && c2 == '=') || (c == '>' && c2 == '=')) {
                    Token t; t.kind = TokKind::Punct; t.text = std::string(1, c) + std::string(1, c2);
                    m_tokens.push_back(t);
                    i += 2;
                    continue;
                }
            }

            // Single-char punctuation/operator -- everything else not otherwise
            // recognized is lexed one character at a time (e.g. '-', '+', '*', '/',
            // '!', '<', '>', ',', ';', '{', '}', '(', ')'). Unknown stray bytes fall
            // through here too, harmlessly.
            Token t; t.kind = TokKind::Punct; t.text = std::string(1, c);
            m_tokens.push_back(t);
            ++i;
        }
    }

    size_t Count() const { return m_tokens.size(); }
    const Token& At(size_t idx) const
    {
        static const Token kEnd{};
        if (idx >= m_tokens.size()) return kEnd;
        return m_tokens[idx];
    }

private:
    const std::string& m_src;
    std::vector<Token> m_tokens;
};

// ---- Parser --------------------------------------------------------------------------
//
// Recursive-descent over the token stream above. Two "skip" primitives do all the
// heavy lifting for the parts of the grammar Phase 1 doesn't interpret:
//   - SkipBalancedBraces: consumes a `{ ... }` block whose CONTENTS are never
//     inspected beyond brace-depth tracking -- used for onOpen/onClose/onESC and any
//     other unrecognized brace-delimited field, so nested if/else/exec/etc. inside
//     them can never desync the outer parser no matter how complex.
//   - SkipExpressionUntilBoundary: a real (tiny) recursive-descent parse of the
//     expression sub-language's SHAPE (primaries, unary !/-, binary operators,
//     parenthesized groups, function calls with comma-separated args) WITHOUT
//     evaluating any of it -- just to correctly find where one expression ends and
//     the next field/keyword begins. Used for `visible <expr-or-literal>` (which has
//     no explicit terminator in real files) and as the generic fallback for any
//     itemDef/menuDef field this parser doesn't explicitly model.
class MenuParser
{
public:
    explicit MenuParser(const Tokenizer& tok) : m_tok(tok) {}

    // Debug aid (2026-08-17) -- prints a small window of tokens around wherever the
    // parser gave up, so a real parse failure can be root-caused instead of guessed
    // at blind. Used during Phase 1's own development to reach a clean 319/319 pass
    // across every real .menu file in the extracted zone dump; kept permanently
    // (only fires on an actual parse failure, silent otherwise) since a genuinely
    // novel keyword shape in some file outside that sample would hit this same
    // "gave up, here's exactly where" path rather than failing silently.
    void DebugDumpFailureContext() const
    {
        // Widened 2026-08-17 (Phase 2) -- expression-heavy real files can desync many
        // tokens before the actual root cause; a 6-token window was too narrow to
        // diagnose without re-deriving the surrounding expression by hand every time.
        size_t start = m_pos > 40 ? m_pos - 40 : 0;
        fprintf(stderr, "[menu_parser] FAILED at token #%zu:\n", m_pos);
        for (size_t i = start; i < m_pos + 6; ++i) {
            const Token& t = m_tok.At(i);
            const char* kindName = t.kind == TokKind::End ? "End" : t.kind == TokKind::Ident ? "Ident" :
                t.kind == TokKind::Number ? "Number" : t.kind == TokKind::String ? "String" : "Punct";
            fprintf(stderr, "  [%zu]%s %s=\"%s\"\n", i, i == m_pos ? " <-- HERE" : "", kindName, t.text.c_str());
        }
    }

    bool ParseFile(ParsedMenuFile& out)
    {
        ExpectPunct("{"); // whole real file is one outer brace pair
        if (!Ok()) return false;

        while (!AtEnd() && !IsPunct("}")) {
            if (IsIdent("menuDef")) {
                Advance();
                ParsedMenuDef menu;
                if (!ParseMenuDefBody(menu)) return false;
                out.menus.push_back(std::move(menu));
            } else if (IsIdent("functionDef")) {
                Advance();
                std::string fname;
                ExprPtr fexpr;
                if (!ParseFunctionDefBody(fname, fexpr)) return false;
                if (!fname.empty() && fexpr) out.functionDefs[fname] = std::move(fexpr);
            } else if (Cur().kind == TokKind::End) {
                break;
            } else {
                // Unknown top-level construct -- skip one generic field/value so a
                // stray/unrecognized top-level keyword can't desync the whole file.
                SkipOneGenericField();
            }
        }
        return true;
    }

private:
    const Tokenizer& m_tok;
    size_t m_pos = 0;
    bool m_ok = true;

    const Token& Cur() const { return m_tok.At(m_pos); }
    bool AtEnd() const { return Cur().kind == TokKind::End; }
    void Advance() { if (!AtEnd()) ++m_pos; }
    bool Ok() const { return m_ok; }

    bool IsIdent(const char* name) const
    {
        return Cur().kind == TokKind::Ident && _stricmp(Cur().text.c_str(), name) == 0;
    }
    bool IsPunct(const char* p) const
    {
        return Cur().kind == TokKind::Punct && Cur().text == p;
    }

    void ExpectPunct(const char* p)
    {
        if (!IsPunct(p)) { m_ok = false; return; }
        Advance();
    }

    // Reads exactly `count` numeric tokens (each optionally preceded by a lone '-'
    // punct token, since the tokenizer lexes '-' separately from the digits that
    // follow it -- see Tokenizer::LexAll's own comment). Real files always use plain
    // decimal literals for rect/color fields, never expressions, so this never needs
    // the full expression skipper.
    bool ReadNumbers(float* out, int count)
    {
        for (int i = 0; i < count; ++i) {
            float sign = 1.0f;
            if (IsPunct("-")) { sign = -1.0f; Advance(); }
            if (Cur().kind != TokKind::Number) return false;
            out[i] = sign * static_cast<float>(Cur().num);
            Advance();
        }
        return true;
    }

    // Consumes one balanced `{ ... }` block (the opening brace must be the CURRENT
    // token). Contents are never inspected -- correct for onOpen/onClose/onESC
    // (imperative statements, if/else, nested braces) and equally correct for
    // functionDef's body (name/value fields) and any other brace-delimited construct
    // this parser hasn't been taught to actually read.
    void SkipBalancedBracesAfterKeyword()
    {
        if (!IsPunct("{")) { m_ok = false; return; }
        int depth = 0;
        do {
            if (IsPunct("{")) ++depth;
            else if (IsPunct("}")) --depth;
            else if (AtEnd()) { m_ok = false; return; }
            Advance();
        } while (depth > 0);
    }

    // Consumes tokens up to and including the next top-level (paren-depth 0) ';' --
    // used for functionDef's `value <expr>;` and itemDef's `exp <field> <expr>;`,
    // both of which are explicitly ';'-terminated in the real grammar (unlike
    // `visible`, which is not -- see SkipExpressionUntilBoundary).
    void SkipUntilTopLevelSemicolon()
    {
        int depth = 0;
        while (!AtEnd()) {
            if (IsPunct("(")) ++depth;
            else if (IsPunct(")")) --depth;
            else if (IsPunct(";") && depth <= 0) { Advance(); return; }
            Advance();
        }
        m_ok = false; // ran off the end without finding the terminator -- malformed file
    }

    // Real (tiny) recursive-descent SHAPE parser for the expression sub-language --
    // does not evaluate anything, only consumes exactly the tokens that make up one
    // complete expression so the caller knows where the NEXT field begins. Handles:
    // unary '!'/'-', parenthesized groups, function calls (ident followed by '(' ')'
    // with comma-separated args, each itself a full expression), string/number/ident
    // primaries, and binary operators (+ - * / == != && || < > <= >=) chaining
    // additional primaries. Stops as soon as, after a complete primary, the next
    // token is NOT a binary operator (i.e. it must be the start of the next
    // field/keyword, or a block/statement terminator like ';' '}' or a bare
    // primary that doesn't attach to anything -- return with whatever was consumed).
    void SkipExpressionUntilBoundary()
    {
        if (!SkipPrimary()) { m_ok = false; return; }
        for (;;) {
            if (IsBinaryOperatorToken()) { Advance(); if (!SkipPrimary()) { m_ok = false; return; } continue; }
            break;
        }
        // Real files are inconsistent about this: `visible when(...);` (this exact
        // shape, confirmed in BOTH stance.menu and survival_armory_weapon.menu) ends
        // with an explicit ';' even though `visible <literal>` (no when()) does not
        // (confirmed in stance.menu -- "visible 1" sits directly against the next
        // field, "ownerdraw", with nothing between). Rather than trying to predict
        // which shape a given field uses, optionally consume one trailing ';' here
        // if present -- safe for nested/recursive calls too (a legitimately nested
        // sub-expression is always terminated by ',' or ')', never a stray ';'
        // before those, so this can never mis-fire mid-expression). Without this,
        // Phase 1's SECOND real parse failure (survival_armory_weapon.menu's very
        // first `visible when(...);` itemDef) left the ';' dangling as an
        // unconsumed pseudo-token, which cascaded into misreading every subsequent
        // field by one token, eventually hard-failing several fields later.
        if (IsPunct(";")) Advance();
    }

    bool IsBinaryOperatorToken() const
    {
        if (Cur().kind != TokKind::Punct) return false;
        const std::string& s = Cur().text;
        // '?'/':' (ternary) are handled the same as arithmetic/logical operators here
        // -- this skipper never evaluates precedence/semantics, only needs to know
        // "another primary follows," which is equally true for `cond ? a : b`.
        return s == "+" || s == "-" || s == "*" || s == "/" || s == "%" || s == "^" ||
               s == "==" || s == "!=" || s == "&&" || s == "||" ||
               s == "<" || s == ">" || s == "<=" || s == ">=" ||
               s == "?" || s == ":";
    }

    bool SkipPrimary()
    {
        if (IsPunct("!") || IsPunct("-") || IsPunct("~")) { Advance(); return SkipPrimary(); } // unary
        if (IsPunct("(")) {
            Advance();
            SkipExpressionUntilBoundary();
            if (!m_ok) return false;
            if (!IsPunct(")")) { m_ok = false; return false; }
            Advance();
            return true;
        }
        if (Cur().kind == TokKind::Number || Cur().kind == TokKind::String) { Advance(); return true; }
        if (Cur().kind == TokKind::Ident) {
            Advance();
            // Zero or more call suffixes -- confirmed real usage (menu_special_
            // features.menu): `getprofiledata("highestMission")()`, a function call
            // whose RETURN VALUE is itself called again with empty parens. A single
            // `if (IsPunct("("))` (not `while`) only consumed the FIRST call and left
            // the second `()` dangling as an unconsumed boundary token, hard-failing
            // the parse a few tokens later once it desynced the caller's field loop.
            while (IsPunct("(")) {
                Advance();
                if (!IsPunct(")")) {
                    for (;;) {
                        SkipExpressionUntilBoundary();
                        if (!m_ok) return false;
                        if (IsPunct(",")) { Advance(); continue; }
                        break;
                    }
                }
                if (!IsPunct(")")) { m_ok = false; return false; }
                Advance();
            }
            return true;
        }
        return false; // no valid primary here -- caller treats as boundary/error
    }

    // ---- Phase 2: real expression AST parser --------------------------------------
    //
    // Precedence-climbing recursive descent, low to high: ternary ?: > || > && > ^ >
    // == != > < > <= >= > + - > * / % > unary ! - ~ > primary. Builds real Expr nodes
    // (menu_expr.h) instead of Phase 1's shape-only SkipPrimary/SkipExpressionUntilBoundary
    // -- kept those two functions ABOVE unchanged (still used by SkipOneGenericField's
    // last-resort fallback for fields this parser doesn't yet extract real value from,
    // e.g. `align`/`ownerdraw` when they're expression-valued rather than a plain number).
    static int BinOpPrecedence(const std::string& op)
    {
        if (op == "||") return 1;
        if (op == "&&") return 2;
        if (op == "^") return 3;
        if (op == "==" || op == "!=") return 4;
        if (op == "<" || op == ">" || op == "<=" || op == ">=") return 5;
        if (op == "+" || op == "-") return 6;
        if (op == "*" || op == "/" || op == "%") return 7;
        return -1;
    }

    ExprPtr ParsePrimaryExpr()
    {
        if (IsPunct("!") || IsPunct("-") || IsPunct("~")) {
            std::string op = Cur().text;
            Advance();
            ExprPtr operand = ParsePrimaryExpr(); // unary binds tighter than any binary op
            if (!operand) { m_ok = false; return nullptr; }
            auto e = std::make_unique<Expr>();
            e->kind = ExprKind::Unary; e->stringValue = op;
            e->children.push_back(std::move(operand));
            return e;
        }
        if (IsPunct("(")) {
            Advance();
            ExprPtr inner = ParseTernaryExpr();
            if (!inner) { m_ok = false; return nullptr; }
            // C-style comma operator inside a parenthesized group -- confirmed real
            // (menu_so_leaderboard_hd.menu/sd.menu: OpenAssetTools' own decompiled
            // `.menu` text contains `(cond <= expr, 1, 0)` -- a paren group with TWO
            // top-level commas inside it, not a 3-arg call). Not a general grammar
            // feature this language is documented to use elsewhere (every other real
            // file's parenthesized groups hold exactly one sub-expression) -- most
            // likely an OpenAssetTools decompile-serialization quirk for this
            // specific deeply-nested boolean-then-arithmetic shape, not something
            // worth reverse-engineering exactly. Degrades gracefully: evaluates
            // left-to-right, keeps only the LAST sub-expression's value (real C comma-
            // operator semantics) so the file still parses instead of hard-failing --
            // accepted, documented residual imprecision for a pattern this rare (2
            // near-duplicate files out of 319).
            while (IsPunct(",")) {
                Advance();
                ExprPtr next = ParseTernaryExpr();
                if (!next) { m_ok = false; return nullptr; }
                inner = std::move(next);
            }
            if (!IsPunct(")")) { m_ok = false; return nullptr; }
            Advance();
            return inner;
        }
        if (Cur().kind == TokKind::Number) {
            auto e = std::make_unique<Expr>();
            e->kind = ExprKind::Number; e->numberValue = Cur().num;
            Advance();
            return e;
        }
        if (Cur().kind == TokKind::String) {
            auto e = std::make_unique<Expr>();
            e->kind = ExprKind::String; e->stringValue = Cur().text;
            Advance();
            return e;
        }
        if (Cur().kind == TokKind::Ident) {
            std::string name = Cur().text;
            Advance();
            if (!IsPunct("(")) {
                auto e = std::make_unique<Expr>();
                e->kind = ExprKind::Ident; e->stringValue = name;
                return e;
            }
            Advance(); // consume '('
            auto call = std::make_unique<Expr>();
            call->kind = ExprKind::Call; call->stringValue = name;
            if (!IsPunct(")")) {
                for (;;) {
                    ExprPtr arg = ParseTernaryExpr();
                    if (!arg) { m_ok = false; return nullptr; }
                    call->children.push_back(std::move(arg));
                    if (IsPunct(",")) { Advance(); continue; }
                    break;
                }
            }
            if (!IsPunct(")")) { m_ok = false; return nullptr; }
            Advance();
            // Chained call suffixes (`f(x)()`, confirmed real: menu_special_features.menu).
            // No first-class function values here -- further () groups are consumed (so
            // the parser never desyncs) but don't change the AST; the FIRST call's result
            // is what gets evaluated. Documented Phase 2 limitation (see menu_expr.h).
            while (IsPunct("(")) {
                Advance();
                if (!IsPunct(")")) {
                    for (;;) {
                        ExprPtr discard = ParseTernaryExpr();
                        if (!discard) { m_ok = false; return nullptr; }
                        if (IsPunct(",")) { Advance(); continue; }
                        break;
                    }
                }
                if (!IsPunct(")")) { m_ok = false; return nullptr; }
                Advance();
            }
            return call;
        }
        m_ok = false;
        return nullptr;
    }

    ExprPtr ParseBinaryExpr(int minPrec)
    {
        ExprPtr lhs = ParsePrimaryExpr();
        if (!lhs) return nullptr;
        for (;;) {
            if (Cur().kind != TokKind::Punct) break;
            int prec = BinOpPrecedence(Cur().text);
            if (prec < 0 || prec < minPrec) break;
            std::string op = Cur().text;
            Advance();
            ExprPtr rhs = ParseBinaryExpr(prec + 1); // left-associative
            if (!rhs) { m_ok = false; return nullptr; }
            auto e = std::make_unique<Expr>();
            e->kind = ExprKind::Binary; e->stringValue = op;
            e->children.push_back(std::move(lhs));
            e->children.push_back(std::move(rhs));
            lhs = std::move(e);
        }
        return lhs;
    }

    ExprPtr ParseTernaryExpr()
    {
        ExprPtr cond = ParseBinaryExpr(1);
        if (!cond) return nullptr;
        if (IsPunct("?")) {
            Advance();
            ExprPtr thenE = ParseTernaryExpr();
            if (!thenE || !IsPunct(":")) { m_ok = false; return nullptr; }
            Advance();
            ExprPtr elseE = ParseTernaryExpr();
            if (!elseE) { m_ok = false; return nullptr; }
            auto e = std::make_unique<Expr>();
            e->kind = ExprKind::Ternary;
            e->children.push_back(std::move(cond));
            e->children.push_back(std::move(thenE));
            e->children.push_back(std::move(elseE));
            return e;
        }
        return cond;
    }

    // `exp <field> <expr>;` / menuDef-level `exp rect <axis> <expr>;` -- called with
    // Cur() sitting on the token AFTER the "exp" keyword itself (caller already advanced
    // past "exp"). Field-path shape confirmed via a repo-wide grep of every real `exp`
    // line: "rect"/"forecolor"/"backcolor" are ALWAYS followed by one more identifier
    // naming the component (x/y/w/h or r/g/b/a, case varies in real files -- lowercased
    // here); every other field name has no component token, the expression starts
    // immediately. `;`-terminated (confirmed, same as functionDef's `value <expr>;`).
    bool ParseExpOverride(std::vector<MenuExpOverride>& outList)
    {
        if (Cur().kind != TokKind::Ident) { m_ok = false; return false; }
        std::string fieldName = Cur().text;
        Advance();
        std::string lowerField = ToLowerAscii(fieldName);
        std::string component;
        if (lowerField == "rect" || lowerField == "forecolor" || lowerField == "backcolor" || lowerField == "glowcolor") {
            if (Cur().kind != TokKind::Ident) { m_ok = false; return false; }
            component = ToLowerAscii(Cur().text);
            Advance();
        }
        ExprPtr expr = ParseTernaryExpr();
        if (!expr) return false;
        if (IsPunct(";")) Advance(); // real files are consistently ';'-terminated here
        outList.push_back(MenuExpOverride{ std::move(fieldName), std::move(component), std::move(expr) });
        return Ok();
    }

    // `visible <expr-or-literal>` -- NOT consistently ';'-terminated in real files (see
    // SkipExpressionUntilBoundary's own comment on this same inconsistency); caller
    // already advanced past "visible".
    bool ParseVisibleField(ExprPtr& outExpr)
    {
        outExpr = ParseTernaryExpr();
        if (!outExpr) return false;
        if (IsPunct(";")) Advance();
        return Ok();
    }

    static std::string ToLowerAscii(const std::string& s)
    {
        std::string r = s;
        for (char& c : r) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return r;
    }

    // Arity classification for every itemDef/menuDef field keyword NOT already
    // explicitly modeled in ParseItemDefBody/ParseMenuDefBody below (rect, name,
    // type, text, background, textscale, forecolor, backcolor, exp, decoration,
    // onOpen/onClose/onESC/mouseEnter/mouseExit/action all have their own explicit
    // handling). Empirically derived by grepping EVERY real .menu file in
    // D:\Tools\OpenAssetTools\zone_dump\ui\ for each shape (bare keyword lines,
    // keyword+1-number lines, keyword+4-number lines, keyword+string lines) -- not
    // guessed. This is what actually fixed Phase 1's first real parse failure
    // (survival_armory_weapon.menu's outOfBoundsClick/popup/legacySplitScreenScale,
    // all genuinely zero-argument, were being mis-consumed as the FOLLOWING field's
    // value by an earlier, purely-generic "skip one expression" fallback -- see this
    // function's replacement, SkipOneGenericField, for why a real table beats a
    // blind heuristic here). A keyword not found in ANY of these lists still falls
    // back to the boundary-expression skipper as a last resort (see
    // SkipOneGenericField) -- accepted, documented residual risk for a genuinely
    // novel keyword in one of the many files not specifically sampled.
    static bool InList(const char* const* list, size_t count, const std::string& name)
    {
        for (size_t i = 0; i < count; ++i) if (_stricmp(list[i], name.c_str()) == 0) return true;
        return false;
    }

    void SkipNumbers(int count)
    {
        for (int i = 0; i < count; ++i) {
            if (IsPunct("-")) Advance();
            if (Cur().kind != TokKind::Number) { m_ok = false; return; }
            Advance();
        }
    }

    // Generic fallback dispatch for any field keyword not already explicitly
    // modeled by name in ParseItemDefBody/ParseMenuDefBody's own if-chains.
    void SkipOneGenericField()
    {
        static const char* kZeroArg[] = {
            "autowrapped", "legacySplitScreenScale", "noscrollbars", "outOfBoundsClick",
            "popup", "textcinematicsubtitle", "textsavegame", "usepaging",
        };
        static const char* kOneNumber[] = {
            "align", "blurWorld", "border", "borderSize", "elementheight",
            "elementwidth", "fadeAmount", "fadeClamp", "fadeCycle", "fadeInAmount",
            "feeder", "fullscreen", "gamemsgwindowindex", "gamemsgwindowmode",
            "maxChars", "maxPaintChars", "newsfeed", "ownerdraw", "spacing", "speed",
            "style", "textalign", "textalignx", "textaligny", "textfont", "textstyle",
        };
        static const char* kFourNumbers[] = {
            "bordercolor", "disablecolor", "focuscolor", "glowcolor", "outlinecolor", "selectBorder",
        };
        static const char* kOneString[] = {
            "allowedBinding", "dvar", "dvarEnumList", "dvarTest", "group", "soundLoop",
        };
        // Confirmed real shape: a string label + 3 numbers (min/default/max-style
        // slider range, e.g. `dvarFloat "sensitivity" 5 1 30`). dvarFloatList's own
        // very different `{ "@LABEL" num ... }` block shape needs no explicit entry
        // here -- it's a brace block, already handled by the generic fallback's own
        // trailing-'{' check. No real dvarInt/dvarString/dvarIntList/dvarStringList
        // usage was found in the sampled files to confirm their own arity -- if a
        // future file uses one, it'll fall through to the last-resort boundary
        // skipper instead of crashing, just possibly mis-consuming a token or two.
        static const char* kStringPlusThreeNumbers[] = { "dvarFloat" };
        static const char* kBlockWithLeadingNumber[] = { "execKeyInt" };
        static const char* kBlockWithLeadingString[] = { "execKey" };
        static const char* kBlockOnly[] = {
            "onFocus", "onFocusDueToClose", "onRequestClose",
            // Confirmed real usage (grepped): keyword alone on its own line, '{' on
            // the NEXT line -- indistinguishable from a genuine zero-arg flag by a
            // single-line-only check, which is exactly what put these in kZeroArg by
            // mistake on the first pass and caused a real desync (see
            // SkipOneGenericField's own defensive "trailing {" check below, added as
            // a second line of defense against this exact class of misclassification
            // for any keyword not listed here too).
            "accept", "doubleclick", "hasFocus", "leaveFocus",
        };

        std::string kw = Cur().text;
        Advance(); // the keyword itself

        if (InList(kZeroArg, sizeof(kZeroArg) / sizeof(kZeroArg[0]), kw)) {
            // Defensive: a keyword believed zero-arg that's ACTUALLY immediately
            // followed by '{' is a block-triggering keyword this table doesn't know
            // about yet -- treat it as one instead of silently desyncing the rest of
            // the file. Real zero-arg flags are never themselves followed by '{'.
            if (IsPunct("{")) SkipBalancedBracesAfterKeyword();
            return;
        }
        if (InList(kOneNumber, sizeof(kOneNumber) / sizeof(kOneNumber[0]), kw)) { SkipNumbers(1); return; }
        if (_stricmp(kw.c_str(), "columns") == 0) {
            // Confirmed real shape (menu_so_leaderboard_sd.menu and others): `columns
            // N` is followed by N raw rows of 6 numbers each (a listbox column
            // layout table), with NO per-row keyword at all -- structurally distinct
            // from every other field in this grammar, needs its own explicit case
            // rather than a generic arity entry.
            if (Cur().kind != TokKind::Number) { m_ok = false; return; }
            int columnCount = static_cast<int>(Cur().num);
            Advance();
            for (int c = 0; c < columnCount; ++c) SkipNumbers(6);
            return;
        }
        if (InList(kFourNumbers, sizeof(kFourNumbers) / sizeof(kFourNumbers[0]), kw)) { SkipNumbers(4); return; }
        if (InList(kOneString, sizeof(kOneString) / sizeof(kOneString[0]), kw)) {
            if (Cur().kind != TokKind::String && Cur().kind != TokKind::Ident) { m_ok = false; return; }
            Advance();
            return;
        }
        if (InList(kStringPlusThreeNumbers, sizeof(kStringPlusThreeNumbers) / sizeof(kStringPlusThreeNumbers[0]), kw)) {
            if (Cur().kind != TokKind::String && Cur().kind != TokKind::Ident) { m_ok = false; return; }
            Advance();
            SkipNumbers(3);
            return;
        }
        if (InList(kBlockWithLeadingNumber, sizeof(kBlockWithLeadingNumber) / sizeof(kBlockWithLeadingNumber[0]), kw)) {
            SkipNumbers(1);
            if (!Ok()) return;
            SkipBalancedBracesAfterKeyword();
            return;
        }
        if (InList(kBlockWithLeadingString, sizeof(kBlockWithLeadingString) / sizeof(kBlockWithLeadingString[0]), kw)) {
            if (Cur().kind != TokKind::String && Cur().kind != TokKind::Ident) { m_ok = false; return; }
            Advance();
            SkipBalancedBracesAfterKeyword();
            return;
        }
        if (InList(kBlockOnly, sizeof(kBlockOnly) / sizeof(kBlockOnly[0]), kw)) {
            SkipBalancedBracesAfterKeyword();
            return;
        }

        // Genuinely unclassified keyword -- last-resort fallback. If the value
        // looks like a brace block, skip it as balanced braces (covers unnamed/
        // unexpected block fields robustly); otherwise skip one expression via the
        // boundary skipper. A genuinely zero-argument keyword not in kZeroArg above
        // would be mis-parsed here -- accepted, documented residual risk (see this
        // function's own header comment).
        if (IsPunct("{")) { SkipBalancedBracesAfterKeyword(); return; }
        SkipExpressionUntilBoundary();
    }

    // `functionDef { name "FUNC_N" value <expr>; }` -- Phase 2 (Phase 1 skipped this
    // whole block via SkipBalancedBracesAfterKeyword). Unrecognized fields inside the
    // block fall to SkipOneGenericField so a future real file with an extra functionDef
    // field this parser doesn't know about still parses instead of hard-failing.
    bool ParseFunctionDefBody(std::string& outName, ExprPtr& outExpr)
    {
        ExpectPunct("{");
        if (!Ok()) return false;
        while (!AtEnd() && !IsPunct("}")) {
            if (IsIdent("name")) {
                Advance();
                if (Cur().kind != TokKind::String && Cur().kind != TokKind::Ident) { m_ok = false; return false; }
                outName = Cur().text;
                Advance();
            } else if (IsIdent("value")) {
                Advance();
                outExpr = ParseTernaryExpr();
                if (!outExpr) return false;
                if (IsPunct(";")) Advance(); // confirmed ';'-terminated in every real sample
            } else if (AtEnd()) {
                return false;
            } else {
                SkipOneGenericField();
            }
            if (!Ok()) return false;
        }
        ExpectPunct("}");
        return Ok();
    }

    bool ParseMenuDefBody(ParsedMenuDef& menu)
    {
        ExpectPunct("{");
        if (!Ok()) return false;

        while (!AtEnd() && !IsPunct("}")) {
            if (IsIdent("name")) {
                Advance();
                if (Cur().kind != TokKind::String && Cur().kind != TokKind::Ident) return false;
                menu.name = Cur().text;
                Advance();
            } else if (IsIdent("rect")) {
                Advance();
                float vals[6] = {};
                if (!ReadNumbers(vals, 6)) return false;
                menu.hasRect = true;
                menu.rectX = vals[0]; menu.rectY = vals[1]; menu.rectW = vals[2]; menu.rectH = vals[3];
                menu.horzMode = static_cast<int>(vals[4]); menu.vertMode = static_cast<int>(vals[5]);
            } else if (IsIdent("itemDef")) {
                Advance();
                ParsedItemDef item;
                if (!ParseItemDefBody(item)) return false;
                menu.items.push_back(std::move(item));
            } else if (Cur().kind == TokKind::Ident && (IsIdent("onOpen") || IsIdent("onClose") || IsIdent("onESC"))) {
                Advance();
                SkipBalancedBracesAfterKeyword(); // imperative sub-language, discarded -- Phase 2+ territory
            } else if (IsIdent("exp")) {
                // `exp rect X/Y/W/H <expr>;` -- confirmed real usage directly inside a
                // menuDef body too (survival_armory_weapon.menu's own menuDef, not
                // just its itemDefs), same ';'-terminated shape as itemDef's exp
                // lines. MUST be handled explicitly here, not left to the generic
                // fallback -- this was Phase 1's first real parse failure: without
                // this, the fallback only consumed "exp"+"rect" as two separate
                // one-token-each pseudo-fields, leaving "X"/"Y"/"W"/"H" to be
                // misinterpreted as fresh top-level keywords, which occasionally
                // re-synced onto a LATER real "rect" token and fed it non-numeric
                // tokens, hard-failing the whole parse. Phase 2: now a real Expr,
                // not just tokenized-and-discarded.
                Advance();
                if (!ParseExpOverride(menu.expOverrides)) return false;
            } else if (IsIdent("visible")) {
                // Phase 2: real Expr instead of the generic shape-only skip. Explicitly
                // modeled (not left to SkipOneGenericField's last resort) so the parsed
                // AST is actually captured for evaluation.
                Advance();
                if (!ParseVisibleField(menu.visibleExpr)) return false;
            } else if (AtEnd()) {
                return false;
            } else {
                SkipOneGenericField(); // fadeClamp/fadeCycle/fadeAmount/blurWorld/focuscolor/etc.
            }
            if (!Ok()) return false;
        }
        ExpectPunct("}");
        return Ok();
    }

    bool ParseItemDefBody(ParsedItemDef& item)
    {
        ExpectPunct("{");
        if (!Ok()) return false;

        while (!AtEnd() && !IsPunct("}")) {
            if (IsIdent("name")) {
                Advance();
                if (Cur().kind != TokKind::String && Cur().kind != TokKind::Ident) return false;
                item.name = Cur().text;
                Advance();
            } else if (IsIdent("rect")) {
                Advance();
                float vals[6] = {};
                if (!ReadNumbers(vals, 6)) return false;
                item.hasRect = true;
                item.rectX = vals[0]; item.rectY = vals[1]; item.rectW = vals[2]; item.rectH = vals[3];
                item.horzMode = static_cast<int>(vals[4]); item.vertMode = static_cast<int>(vals[5]);
            } else if (IsIdent("type")) {
                Advance();
                if (Cur().kind != TokKind::Number) return false;
                item.type = Cur().text;
                Advance();
            } else if (IsIdent("text")) {
                Advance();
                if (Cur().kind == TokKind::String || Cur().kind == TokKind::Ident) { item.text = Cur().text; Advance(); }
                else SkipExpressionUntilBoundary(); // a locstring()/expr-shaped text value -- Phase 2 territory
            } else if (IsIdent("background")) {
                Advance();
                if (Cur().kind == TokKind::String || Cur().kind == TokKind::Ident) { item.background = Cur().text; Advance(); }
                else SkipExpressionUntilBoundary();
            } else if (IsIdent("textscale")) {
                Advance();
                if (Cur().kind != TokKind::Number) return false;
                item.textScale = static_cast<float>(Cur().num);
                Advance();
            } else if (IsIdent("forecolor")) {
                Advance();
                float vals[4] = {};
                if (!ReadNumbers(vals, 4)) return false;
                item.hasForecolor = true;
                for (int i = 0; i < 4; ++i) item.forecolor[i] = vals[i];
            } else if (IsIdent("backcolor")) {
                Advance();
                float vals[4] = {};
                if (!ReadNumbers(vals, 4)) return false;
                item.hasBackcolor = true;
                for (int i = 0; i < 4; ++i) item.backcolor[i] = vals[i];
            } else if (IsIdent("decoration")) {
                // Real zero-argument flag (confirmed live in stance.menu: "decoration"
                // sits directly against the next field with nothing in between) --
                // explicitly modeled so the generic fallback (which would otherwise
                // wrongly consume the NEXT field's keyword as this one's "value")
                // never has to guess about it.
                Advance();
            } else if (IsIdent("exp")) {
                // `exp <field> <expr>;` dynamic override -- Phase 2: now a real Expr,
                // stored on the item for the renderer to evaluate each frame (see
                // menu_render.cpp), not just tokenized-and-discarded.
                Advance();
                if (!ParseExpOverride(item.expOverrides)) return false;
            } else if (IsIdent("visible")) {
                // Phase 2: real Expr, explicitly modeled (was the generic fallback's
                // last resort in Phase 1).
                Advance();
                if (!ParseVisibleField(item.visibleExpr)) return false;
            } else if (Cur().kind == TokKind::Ident && (IsIdent("onOpen") || IsIdent("onClose") || IsIdent("onESC") ||
                        IsIdent("mouseEnter") || IsIdent("mouseExit") || IsIdent("action"))) {
                Advance();
                if (IsPunct("{")) SkipBalancedBracesAfterKeyword();
                else SkipUntilTopLevelSemicolon(); // some of these appear as a single ';'-terminated statement, not a block
            } else if (AtEnd()) {
                return false;
            } else {
                SkipOneGenericField(); // style/ownerdraw/textstyle/textfont/textalign/group/border/origin/etc.
            }
            if (!Ok()) return false;
        }
        ExpectPunct("}");
        return Ok();
    }
};

} // namespace

bool ParseMenuFile(const char* path, ParsedMenuFile& outFile)
{
    outFile.menus.clear();

    FILE* f = nullptr;
    if (fopen_s(&f, path, "rb") != 0 || !f) return false;
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (size <= 0) { fclose(f); return false; }
    std::string src;
    src.resize(static_cast<size_t>(size));
    size_t readBytes = fread(&src[0], 1, static_cast<size_t>(size), f);
    fclose(f);
    src.resize(readBytes);

    Tokenizer tok(src);
    tok.LexAll();

    MenuParser parser(tok);
    bool ok = parser.ParseFile(outFile);
    if (!ok) parser.DebugDumpFailureContext();
    return ok;
}
