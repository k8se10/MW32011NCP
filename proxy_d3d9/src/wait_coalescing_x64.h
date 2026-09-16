#pragma once

// Wait-coalescing/archive-priority-boost, x64-only, 2026-09-16 -- ported with
// credit (see wait_coalescing_x64.cpp's own header comment) from
// legoliamneeson/MW3_Standalone_D3D9_Project (github.com/legoliamneeson/
// MW3_Standalone_D3D9_Project, src/runtime.hpp). Real, well-known Windows
// low-latency technique: the game's own render/backend threads busy-poll via
// Sleep(1)/WaitForSingleObject(handle, 1), but the OS's default timer
// resolution (~15.6ms) means a nominal "1ms" wait often actually blocks far
// longer -- this substitutes a real high-resolution waitable-timer wait (or a
// plain SwitchToThread for the backend's own poll) ONLY for calls originating
// from the game's own confirmed poll-loop call sites, verified via a real
// return-address check, not touched at all for any other Sleep/Wait caller
// anywhere else in the process (including this mod's own threads).

// Installs the Sleep/WaitForSingleObject detours, resolving all 4 real call
// sites via this project's own "verify exact bytes before hooking" policy.
// Call once, after MH_Initialize(), under confirmed iw5sp.exe only (same
// gating as every other x64 gameplay/engine hook -- these signatures were
// only ever verified against iw5sp.exe's own binary).
void InstallWaitCoalescingHooksX64();

// Called from the IWD-read acceleration hook (a separate, not-yet-ported
// piece) whenever it serves real archive bytes -- feeds the same burst
// detection this file's own wait-coalescing logic uses to decide when to
// temporarily boost the calling (archive-worker) thread's priority and widen
// the coalesce window. Safe to call from any thread; a cheap no-op if
// WaitCoalescingEnabled is off. `bytes` is the real byte count served for
// this one read.
void NotifyArchiveIoActivityX64(unsigned int bytes);
