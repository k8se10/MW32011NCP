// signature_scan.h -- runtime AOB (Array-of-Bytes) byte-pattern scanner.
//
// Copied and adapted from the sibling MW32011NCP project's own file of the same
// name (that project's own re_notes/known_issues_x64.md issue #1 documents the
// full policy history) -- every hook target this project resolves is found via a
// real runtime signature scan, ONCE at process startup, then cached for the rest
// of the session, not a continuous/repeated re-scan loop.
//
// Signature format: a space-separated hex byte string, "??" for a wildcard byte
// (matches anything). Example: "48 83 3D ?? ?? ?? ?? 00 4C 8B F1" -- always
// wildcard any byte range that encodes a PC-relative/absolute address (CALL/JMP
// rel32, RIP-relative LEA/MOV displacements) since those shift between binary
// builds even when the surrounding instruction bytes are identical -- a fixed
// small immediate or a stack-relative (RSP/RBP) displacement does NOT need
// wildcarding, it's not an address at all.

#pragma once

#include <cstdint>
#include <cstddef>

namespace SigScan {

// Result of a single resolve: the found address (nullptr if not found/invalid), and
// whether the caller should treat that as fatal for whatever hook depended on it.
// A null result must never silently fall through to installing a hook at address 0.
struct Result {
    uintptr_t address = 0;
    bool found = false;
};

// Scans the given module's memory image (as mapped into THIS process, i.e. after the
// OS loader has already relocated/loaded it) for `pattern`. Returns the address of
// the FIRST match. If `expectedOccurrences` is 1 (the default and the common case),
// a second match anywhere in the scanned range is treated as an ambiguous signature
// and the scan fails (found=false) rather than silently returning whichever one
// happened to come first.
Result FindPattern(const char* pattern, uintptr_t moduleBase, size_t moduleSize, int expectedOccurrences = 1);

// Convenience wrapper: scans the CURRENT process's own main module (the game .exe
// that loaded this DLL) for `pattern`.
Result FindPatternInMainModule(const char* pattern, int expectedOccurrences = 1);

// Adds a byte offset to a resolved signature match and returns the result as a
// typed function pointer.
template <typename FnT>
inline FnT ResolveAs(const Result& r, ptrdiff_t offset = 0)
{
    if (!r.found || r.address == 0) return nullptr;
    return reinterpret_cast<FnT>(r.address + offset);
}

}  // namespace SigScan
