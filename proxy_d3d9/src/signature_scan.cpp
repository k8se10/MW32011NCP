// signature_scan.cpp -- see signature_scan.h for the policy/design rationale.

#include "signature_scan.h"

#include <windows.h>
#include <vector>
#include <cstdio>
#include <cstring>
#include <cctype>

// Forwarder defined in dllmain.cpp -- same shared-log-file convention every other
// translation unit in this project already uses (see analog_input_hooks.cpp's own
// identical extern declaration).
extern void LogFromController(const char* msg);

namespace SigScan {

namespace {

struct ParsedPattern {
    std::vector<uint8_t> bytes;
    std::vector<bool> isWildcard;
    bool valid = false;
};

// Parses a "48 83 3D ?? ?? ?? ?? 00" style string into byte+wildcard-mask vectors.
// Deliberately strict: any token that isn't exactly 2 hex digits or exactly "??" is
// a malformed pattern -- fails loudly (valid=false) rather than silently skipping a
// bad token, since a silently-mis-parsed pattern is exactly the "hook installed
// against garbage" failure mode CLAUDE.md SS5 already warns about for the resolve
// side; the same standard applies to the pattern's own encoding.
ParsedPattern Parse(const char* pattern)
{
    ParsedPattern result;
    if (!pattern) return result;

    const char* p = pattern;
    while (*p) {
        while (*p == ' ' || *p == '\t') ++p;
        if (!*p) break;

        if (p[0] == '?' && p[1] == '?') {
            result.bytes.push_back(0);
            result.isWildcard.push_back(true);
            p += 2;
        } else if (isxdigit(static_cast<unsigned char>(p[0])) && isxdigit(static_cast<unsigned char>(p[1]))) {
            char hex[3] = {p[0], p[1], 0};
            result.bytes.push_back(static_cast<uint8_t>(strtoul(hex, nullptr, 16)));
            result.isWildcard.push_back(false);
            p += 2;
        } else {
            // Malformed token -- bail out rather than guess.
            return ParsedPattern{};
        }

        // Must be at a token boundary now (space, tab, or end of string) -- a
        // 3rd hex digit stuck to a valid 2-digit token is also a malformed
        // pattern, not something to silently truncate.
        if (*p && *p != ' ' && *p != '\t') {
            return ParsedPattern{};
        }
    }

    result.valid = !result.bytes.empty();
    return result;
}

bool MatchesAt(const uint8_t* haystack, const ParsedPattern& pat)
{
    for (size_t i = 0; i < pat.bytes.size(); ++i) {
        if (!pat.isWildcard[i] && haystack[i] != pat.bytes[i]) return false;
    }
    return true;
}

}  // namespace

Result FindPattern(const char* pattern, uintptr_t moduleBase, size_t moduleSize, int expectedOccurrences)
{
    Result out;

    ParsedPattern pat = Parse(pattern);
    if (!pat.valid) {
        char buf[256];
        sprintf_s(buf, "[sigscan] FAILED to parse pattern (malformed token): \"%s\"", pattern ? pattern : "(null)");
        LogFromController(buf);
        return out;
    }
    if (moduleBase == 0 || moduleSize < pat.bytes.size()) {
        char buf[256];
        sprintf_s(buf, "[sigscan] FAILED: invalid module range (base=0x%llX size=%llu) for pattern \"%s\"",
                   static_cast<unsigned long long>(moduleBase), static_cast<unsigned long long>(moduleSize), pattern);
        LogFromController(buf);
        return out;
    }

    const uint8_t* base = reinterpret_cast<const uint8_t*>(moduleBase);
    const size_t patLen = pat.bytes.size();
    const size_t scanEnd = moduleSize - patLen;

    uintptr_t firstMatch = 0;
    int matchCount = 0;

    for (size_t off = 0; off <= scanEnd; ++off) {
        // Cheap first-byte reject before the full per-byte compare -- this scan runs
        // once at startup per signature, not per-frame, so raw throughput isn't a
        // real concern, but there's no reason to make it needlessly slow either.
        if (!pat.isWildcard[0] && base[off] != pat.bytes[0]) continue;

        if (MatchesAt(base + off, pat)) {
            ++matchCount;
            if (matchCount == 1) {
                firstMatch = moduleBase + off;
            }
            if (matchCount > expectedOccurrences) {
                // Already ambiguous -- no need to keep scanning the whole module,
                // the verdict (fail) is already decided.
                break;
            }
        }
    }

    if (matchCount == 0) {
        char buf[256];
        sprintf_s(buf, "[sigscan] FAILED: 0 matches for pattern \"%s\" in module range [0x%llX, 0x%llX)",
                   pattern, static_cast<unsigned long long>(moduleBase),
                   static_cast<unsigned long long>(moduleBase + moduleSize));
        LogFromController(buf);
        return out;
    }
    if (matchCount != expectedOccurrences) {
        char buf[320];
        sprintf_s(buf, "[sigscan] FAILED: pattern \"%s\" matched %d times, expected exactly %d -- "
                   "signature is ambiguous, refusing to guess which one is correct", pattern, matchCount, expectedOccurrences);
        LogFromController(buf);
        return out;
    }

    char buf[256];
    sprintf_s(buf, "[sigscan] OK: pattern \"%s\" resolved to 0x%llX (%d match%s)",
               pattern, static_cast<unsigned long long>(firstMatch), matchCount, matchCount == 1 ? "" : "es");
    LogFromController(buf);

    out.address = firstMatch;
    out.found = true;
    return out;
}

Result FindPatternInMainModule(const char* pattern, int expectedOccurrences)
{
    HMODULE mod = GetModuleHandleA(nullptr);
    if (!mod) {
        LogFromController("[sigscan] FAILED: GetModuleHandleA(nullptr) returned null -- can't resolve main module");
        return Result{};
    }

    // Walk the PE headers ourselves rather than depend on <psapi.h>'s
    // GetModuleInformation (an extra import this module doesn't otherwise need) --
    // this is the same DOS-header-then-NT-header walk any PE loader does.
    auto base = reinterpret_cast<uintptr_t>(mod);
    auto dosHeader = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    if (dosHeader->e_magic != IMAGE_DOS_SIGNATURE) {
        LogFromController("[sigscan] FAILED: main module's DOS header signature is wrong -- can't be a real PE image");
        return Result{};
    }
    auto ntHeaders = reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + dosHeader->e_lfanew);
    if (ntHeaders->Signature != IMAGE_NT_SIGNATURE) {
        LogFromController("[sigscan] FAILED: main module's NT header signature is wrong -- can't be a real PE image");
        return Result{};
    }

    size_t imageSize = ntHeaders->OptionalHeader.SizeOfImage;
    return FindPattern(pattern, base, imageSize, expectedOccurrences);
}

}  // namespace SigScan
