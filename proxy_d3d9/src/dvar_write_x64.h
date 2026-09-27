#pragma once

// dvar_write_x64.h -- 2026-09-27, the real x64 dvar WRITE path (known_issues_x64.md
// issue #6). See dvar_write_x64.cpp's header comment for the full RE trail.
//
// Every Queue* function is callable from ANY thread. The write itself is always
// applied later on the engine's main thread, at the top of the next Com_Frame body
// (the engine's own Dvar_SetVariant silently drops writes to flagged dvars from any
// other thread -- see the .cpp). A second write to the same dvar before the drain
// replaces the first (coalesced by case-insensitive name), so the queue never grows
// past the number of distinct dvars written in one frame.
//
// Return value: true = queued (the drain logs the applied value and a read-back);
// false = rejected immediately, with the reason logged once per reason (the write
// path is unavailable in this exe, the name/value is too long, or the queue is full).
//
// The writes use DVAR_SOURCE_INTERNAL (0), the same source the engine's own typed
// setters use and the same "param_5 = 0" semantics x86's SetDvarBool/Float/String
// wrappers have (real_settings.h): the LIVE value is written directly, bypassing the
// DVAR_LATCHED diversion and the read-only/cheat/write-protect checks that only apply
// to external (console/script) sources.

extern "C" bool QueueDvarBoolWriteX64(const char* name, bool value);
extern "C" bool QueueDvarIntWriteX64(const char* name, int value);
extern "C" bool QueueDvarFloatWriteX64(const char* name, float value);
// Parsed by the engine per the dvar's own type (Dvar_SetFromStringFromSource), so it
// is correct for string, enum (by name or index), and numeric dvars alike.
extern "C" bool QueueDvarStringWriteX64(const char* name, const char* value);

// True once every target resolved and the main-thread drain hook is live.
extern "C" bool IsDvarWritePathAvailableX64();

// Resolves the four engine setters and installs the Com_Frame-body drain hook.
// SP only: called from InstallAnalogInputHooksX64 after Dvar_FindVar has resolved.
// MinHook must already be initialized.
void InstallDvarWriteX64();
