// netcode_fixes.h -- the shared entry point + host-services abstraction every
// individual fix module builds on. This exact interface is what lets each fix
// module's .cpp file compile unmodified into BOTH NSP's own standalone proxy DLL
// (this build) and a future MW32011NCP-plugin build (tools/ncp_plugin_netcode_fixes/,
// a separate task) -- the standalone dllmain.cpp populates FixHostServices with thin
// wrappers around raw MinHook/GetModuleHandleA; the NCP plugin populates it directly
// from the MW3NCP_PluginAPI* the host hands it (the shapes already match closely by
// design). Neither target's fix-module .cpp files should ever call MinHook or
// GetModuleHandle directly -- always go through this struct.

#pragma once

struct FixHostServices {
    // Thin wrapper around MH_CreateHook+MH_EnableHook (standalone) or
    // MW3NCP_PluginAPI::InstallHook (plugin). Returns 1 on success, 0 on failure.
    int (*InstallHook)(void* target, void* detour, void** outOriginal);

    // The game process's own main module base address.
    void* (*GetGameModuleBase)(void);

    // Shared logging -- writes to this build's own log file (standalone) or the
    // host's log (plugin).
    void (*Log)(const char* msg);
};

// Installs every confirmed, implemented fix. Safe to call once, early (this
// project's fixes don't touch rendering, so there's no need to wait for a D3D9
// device the way MW32011NCP's own rendering/input hooks do -- call this as soon as
// `host.GetGameModuleBase()` will return a valid, fully-loaded module, i.e.
// DLL_PROCESS_ATTACH is fine).
//
// CORRECTED 2026-09-05, live-observed on this pipeline's actual first live
// execution: DLL_PROCESS_ATTACH being fine for the SIGNATURE SCAN (the module
// is always fully mapped by then) does NOT mean every fix's OWN dependencies
// are ready that early. Finding 1's fix additionally depends on the game's
// own Steamworks init (SteamNetworking() returning a real interface), which
// runs later in the game's own startup than DllMain -- p2p_fix.cpp now
// retries on a background thread rather than assuming this call happens late
// enough. Call this function itself early is still correct; just don't
// assume every fix inside it can finish resolving synchronously.
void InstallNetcodeFixes(const FixHostServices& host);

// Individual fix installers -- each resolves its own target address(es) via the
// shared signature scanner against host.GetGameModuleBase(), installs its own
// hook(s) via host.InstallHook, and logs via host.Log. Declared here so
// InstallNetcodeFixes (in netcode_fixes.cpp) can call all of them; defined in their
// own respective .cpp files (one per finding, matching this project's own
// "document every last detail" per-issue structure).
void InstallP2PFix(const FixHostServices& host);              // Finding 1: iw5sp.exe Steamworks P2P receive-path overflow
void InstallMatchdatadoneAndMemberjoinFix(const FixHostServices& host); // Findings 2+3: iw5mp.exe, share ONE hook (see .cpp)

// Finding 4 (iw5mp.exe fragment-reassembly OOB write) is NOT implemented this pass
// -- see re_notes/vulnerability_research.md's own entry for why (the destination
// address is computed at runtime as a per-connection buffer base + attacker offset;
// the shared copy-primitive hook used for findings 2/3 can't recover the real
// buffer's bounds from that address alone, and a full-function replacement risks
// missing undocumented behavior in the function's own "magic-byte-range/version-
// compat checks" section this project doesn't yet fully understand). Real next step,
// not attempted here rather than shipping an unverified guess.
