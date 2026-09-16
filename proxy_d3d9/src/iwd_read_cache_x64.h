#pragma once

// Persistent .iwd archive read cache, x64-only, 2026-09-16 -- ported with
// credit (see iwd_read_cache_x64.cpp's own header comment) from
// legoliamneeson/MW3_Standalone_D3D9_Project (github.com/legoliamneeson/
// MW3_Standalone_D3D9_Project, src/runtime.hpp). Real technique: the first
// time the game opens a given .iwd archive file, this maps the WHOLE file
// read-only into this process's own address space once (a real Windows
// file-mapping, not a copy); every subsequent read the game's own .iwd
// streaming loader makes against that file (identified via a signature-
// verified real return-address check, not a blanket "any .iwd read") is then
// served directly from that mapped view via memcpy instead of going through
// a real disk I/O syscall.

// Installs the CreateFileA/W, ReadFile, SetFilePointer(Ex), and CloseHandle
// hooks needed to track .iwd handles and serve the persistent cache. Call
// once, after MH_Initialize(), under confirmed iw5sp.exe only (same gating
// as every other x64 hook -- these signatures were only ever verified
// against iw5sp.exe's own binary).
void InstallIwdReadCacheHooksX64();
