// netcode_fixes.cpp -- see netcode_fixes.h. Just calls each individual fix
// installer; each one internally no-ops (with a clear log line, not a silent
// skip) if its own target binary's signature isn't found -- so this single
// entry point is safe to call unconditionally regardless of whether this DLL
// ends up loaded into iw5sp.exe or iw5mp.exe.

#include "netcode_fixes.h"

void InstallNetcodeFixes(const FixHostServices& host)
{
    host.Log("[nsp] Installing netcode security fixes...");
    InstallP2PFix(host);                          // Finding 1 (iw5sp.exe)
    InstallMatchdatadoneAndMemberjoinFix(host);    // Findings 2+3+4 (iw5mp.exe)
    host.Log("[nsp] Netcode security fix installation pass complete.");
}
