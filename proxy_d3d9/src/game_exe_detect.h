#pragma once

// game_exe_detect.h -- 2026-09-15, real groundwork for Multiplayer: detects which
// game executable actually loaded this proxy DLL, so hook installation can be gated
// per-binary instead of blindly assuming iw5sp.exe.
//
// Real bug this closes: iw5sp.exe and iw5mp.exe share the same install directory
// and therefore the same deployed d3d9.dll (CLAUDE.md's own standing "SP and MP are
// separate efforts" policy, S10.8 -- iw5mp.exe is a separately-compiled binary, no
// address/signature from Campaign/Survival work carries over). Before this file
// existed, dllmain.cpp called InstallAnalogInputHooksX64()/InstallAnalogInputHooks()
// completely unconditionally, regardless of which binary was actually running --
// every one of that file's several thousand lines of signature-scanned hooks was
// found and verified against iw5sp.exe ONLY, and MP live/injection work has never
// been authorized or attempted (CLAUDE.md's MP scope decision: static RE first,
// opt-in-only live work once it starts, and no gameplay hooks exist for iw5mp.exe
// yet at all). Live-reported 2026-09-15: MP loads this DLL fine (XInput polling and
// other exe-agnostic init succeed, hence "controller connected" showing even under
// iw5mp.exe), but crashes navigating menus -- consistent with at least one of this
// project's many SP-only signature scans spuriously matching unrelated bytes
// somewhere in iw5mp.exe's own, differently-compiled code and installing a hook at
// a location that behaves completely differently there, exactly the class of risk
// CLAUDE.md S5's "validate a scanned signature actually resolved... fail loudly"
// policy defends against for a BAD match, but can't defend against a coincidental
// GOOD match in the wrong binary entirely.
//
// This module answers one question -- "which real .exe loaded us" -- via
// GetModuleFileNameA(nullptr, ...) against the current process's own main module
// (the same call LogInit() in dllmain.cpp already makes for the log-file path),
// comparing its basename case-insensitively against the two known real binary
// names. Deliberately NOT signature/content-based (no need to open or hash the
// exe) -- the file name itself is what determines DLL search-order deployment and
// is what every existing part of this project already keys off of (installation
// docs, the game's own install layout).

enum class GameExecutable {
    Unknown,  // couldn't determine, or a name that doesn't match either known
              // binary (e.g. this DLL loaded into some completely different
              // process by accident) -- treated as "refuse to hook", same
              // fail-safe default as MP until MP hooks genuinely exist
    SP,       // iw5sp.exe -- Campaign/Survival, this project's fully-supported target
    MP,       // iw5mp.exe -- Multiplayer, no gameplay hooks exist/are authorized yet
};

// Detects and caches which real game executable loaded this DLL. Safe to call
// multiple times (idempotent, cheap) but intended to be called once, early in
// DLL_PROCESS_ATTACH (dllmain.cpp), before any hook-installation function runs.
GameExecutable DetectGameExecutable();

// Returns the same cached result DetectGameExecutable() last computed, without
// re-querying the OS. Asserts (debug builds) if called before DetectGameExecutable()
// has ever run. Prefer this in any code that just needs to branch on the result
// after startup (hook installers, diagnostics) rather than re-detecting.
GameExecutable GetDetectedGameExecutable();

// Human-readable name for logging ("iw5sp.exe", "iw5mp.exe", "unknown"), independent
// of whatever the real on-disk file name's casing happened to be.
const char* GameExecutableName(GameExecutable exe);
