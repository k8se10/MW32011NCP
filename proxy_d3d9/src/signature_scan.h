// signature_scan.h -- runtime AOB (Array-of-Bytes) byte-pattern scanner.
//
// Per the locked 2026-09-03 policy (CLAUDE.md SS5/SS10.3, reversed from the prior
// 2026-08-25 hardcode-only stance after MW3's x86->x64 recompile invalidated every
// hardcoded address this project had found in one step): every x64 hook target is
// resolved via a real runtime signature scan, ONCE at process startup, then cached
// for the rest of the session -- not a continuous/repeated re-scan loop. This file
// is that scanner.
//
// Signature format: a space-separated hex byte string, "??" for a wildcard byte
// (matches anything). Example: "48 83 3D ?? ?? ?? ?? 00 4C 8B F1" -- see
// re_notes/x64_migration/impl_sig_*.txt for how a real signature is derived from a
// function's actual disassembly (DumpSigBytes.java), and always wildcard any byte
// range that encodes a PC-relative/absolute address (CALL/JMP rel32, RIP-relative
// LEA/MOV/CMP displacements) since those shift between binary builds even when the
// surrounding instruction bytes are identical -- a fixed small immediate or a
// stack-relative (RSP/RBP) displacement does NOT need wildcarding, it's not an
// address at all (a real false-positive DumpSigBytes.java's own quick reference-based
// heuristic hit once already -- see that script's own header comment).

#pragma once

#include <cstdint>
#include <cstddef>

namespace SigScan {

// Result of a single resolve: the found address (nullptr if not found/invalid), and
// whether the caller should treat that as fatal for whatever hook depended on it.
// Per CLAUDE.md SS5 ("validate a scanned signature actually resolved... before
// installing a hook on it -- fail loudly and refuse to hook rather than jumping to
// garbage"), a null result must never silently fall through to installing a hook at
// address 0.
struct Result {
    uintptr_t address = 0;
    bool found = false;
};

// Scans the given module's memory image (as mapped into THIS process, i.e. after the
// OS loader has already relocated/loaded it -- so signature bytes must match the
// loaded image, not the on-disk file layout) for `pattern` (the space-separated
// hex/?? string described above). `moduleBase`/`moduleSize` should come from the
// game's own module (GetModuleHandleA(nullptr) + NtHeaders->SizeOfImage, or an
// equivalent MODULEINFO query) -- restricting the scan to the actual loaded module
// avoids ever matching inside this DLL's own code or another loaded module by
// accident.
//
// Returns the address of the FIRST match. If `expectedOccurrences` is 1 (the
// default and the common case), a second match anywhere in the scanned range is
// treated as an ambiguous signature and the scan fails (found=false) rather than
// silently returning whichever one happened to come first -- a real signature should
// be unique; if it isn't, the pattern needs to be longer/more specific, not
// papered over here.
Result FindPattern(const char* pattern, uintptr_t moduleBase, size_t moduleSize, int expectedOccurrences = 1);

// Convenience wrapper: scans the CURRENT process's own main module (the game .exe
// that loaded this DLL) for `pattern`. This is the call every real hook-install site
// should use -- resolves the module base/size internally via GetModuleHandleA(nullptr)
// + a PE header walk, so callers never need to pass raw module info around by hand.
Result FindPatternInMainModule(const char* pattern, int expectedOccurrences = 1);

// Adds a byte offset to a resolved signature match and returns the result as a
// typed function pointer. Small convenience for the common "found the function's
// entry point, now cast it" step -- does no additional validation beyond what
// FindPattern/FindPatternInMainModule already did.
template <typename FnT>
inline FnT ResolveAs(const Result& r, ptrdiff_t offset = 0)
{
    if (!r.found || r.address == 0) return nullptr;
    return reinterpret_cast<FnT>(r.address + offset);
}

}  // namespace SigScan
