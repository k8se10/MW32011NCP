// real_settings.h -- raw accessors for the real engine subsystems backing the vanilla
// Options screen (dvars + keybinds), for the in-progress full Options-screen
// replacement (re_notes/known_issues.md issue #66). Every address here was found via
// Ghidra decompile/disassembly of iw5sp.exe (2026-08-04), documented in full --
// including the exact raw disassembly that confirmed each function's real calling
// convention -- in re_notes/options_menu_full_map.md. None of these are hooks (no
// detour/trampoline involved, so CLAUDE.md's "no hardcoded hook addresses" scanning
// requirement doesn't apply the same way): they're direct calls into the engine's own
// existing functions, the same class of use as GetDvarInt/GetDvarString/
// ForwardKeyToMenu already hardcoded in analog_input_hooks.cpp.
//
// NOT yet live-tested end-to-end (options_menu_full_map.md's own standing caveat) --
// confidence is from decompiled/disassembled structural analysis only. Live-test each
// of these against the running game before relying on them for a shipped feature, per
// this project's own "Verify Live" standard (CLAUDE.md #10.4).
#pragma once

// ---- Dvar getters ------------------------------------------------------------
//
// Self-contained duplicates of the same raw Dvar_FindVar-based getters
// analog_input_hooks.cpp already has for its own gameplay-hook purposes (also named
// GetDvarInt/GetDvarFloat there) -- NOT reused directly because those live inside
// analog_input_hooks.cpp's own translation unit with linkage not established for
// cross-file use, and this module is meant to stand alone as "the real vanilla-
// settings accessor layer." Same real function (FUN_0062abe0), same custom
// EDI-register name-argument convention, same read-only-at-+0xc semantics.
int GetDvarBool(const char* name);
float GetDvarFloat(const char* name);
// Returns a pointer into the ENGINE's own live string, valid only until the next
// engine dvar write -- copy it immediately if the caller needs to keep it, same
// caveat as this project's other GetDvarString.
const char* GetDvarString(const char* name);

// ---- Dvar setters --------------------------------------------------------------
//
// All three are genuine __cdecl wrappers around the engine's own Dvar_FindVar +
// type-specific commit function (FUN_0062a8c0) -- confirmed via raw disassembly,
// see options_menu_full_map.md secs 4/6. Unlike GetDvarInt/GetDvarString
// (analog_input_hooks.cpp), which call the raw custom-register FUN_0062abe0
// directly and need an inline __asm block, these outer setters already handle that
// internally -- callable as plain functions.
//
// param_5 semantics (options_menu_full_map.md sec 6, decompiled from the shared
// commit sink FUN_0062a8c0): these three always pass 0, which SKIPS the real
// permission/DVAR_LATCHED-diversion block and writes the dvar's LIVE value
// immediately. That's correct for ordinary (non-latched) dvars -- Look/Voice/most
// bools -- but for DVAR_LATCHED dvars (the restart-required Advanced Video family:
// ui_r_aasamples, ui_r_vsync, sm_enable, ui_r_picmip*, etc.) this writes the live
// value directly rather than staging it into the pending slot the real
// apply-settings flow (all_restart_popmenu.menu) expects. A caller writing one of
// those MUST follow up with the same real vid_restart/snd_restart exec the real
// popup uses (Cbuf_AddText/Cbuf_Execute -- already available elsewhere in this
// project) -- see issue #66/task #20 for the full apply/restart design. This file
// does not know which dvars are latched; that classification belongs in the
// settings table that calls these, not here.
extern "C" void SetDvarBool(const char* name, int value);
extern "C" void SetDvarString(const char* name, const char* value);
extern "C" void SetDvarFloat(const char* name, float value);

// ---- Keybind table access --------------------------------------------------------
//
// Real table: DAT_00a98e4c, one 0xd28-byte block per local-player config index, 256
// (0x100) key slots per block, each slot 3 ints (12 bytes) wide -- slot int[0] holds
// the compiled numeric command ID currently bound to that key (0 = unbound). Table
// bounds and stride independently confirmed via the real unbindall handler's own
// loop bound (options_menu_full_map.md sec 7).

// How many real keys (0, 1, or 2 -- this engine reports "OR"-bound pairs, matching
// the real UI's own "KEY_OR" display convention) are currently bound to `command`
// (e.g. "+sprint"), writing up to 2 real keynums into outKeynums[2]. Custom
// register calling convention (EAX=command, ECX=configIndex, EBX=&outKeynums) --
// confirmed via raw disassembly of FUN_0057e640, wrapped in an inline __asm block
// the same way GetDvarInt already wraps FUN_0062abe0.
int GetKeybind(const char* command, int configIndex, int outKeynums[2]);

// Binds `keynum` to `command` for the given config index. Resolves `command` to its
// real numeric ID via the same resolver GetKeybind uses internally
// (Key_CommandStringToId-equivalent) -- an unrecognized command string silently does
// nothing, matching the real "bind" console command's own behavior for a bad
// argument (not a bug to guard against, the real engine already tolerates it).
void SetKeybind(const char* command, int configIndex, int keynum);

// Clears whatever command (if any) is bound to `keynum` -- equivalent to the real
// "unbind" console command's effect on the table (confirmed: unbind's own real
// handler writes 0 into the same table slot SetKeybind writes to).
void UnbindKeynum(int keynum, int configIndex);

// Key_StringToKeynum-equivalent (e.g. "mouse1", "ctrl", "a") -> real keynum, or -1
// if not recognized. Same real function the native "bind"/"unbind" console commands
// use to parse their own key-name argument.
int KeyNameToKeynum(const char* keyName);

// keynum -> real display name string (e.g. what the real UI itself would show for a
// bound key), written into outBuf (must be at least 0x80 bytes per the real
// function's own internal buffer convention).
void KeynumToDisplayName(int keynum, char* outBuf, int outBufSize);

// ---- Console command queue ------------------------------------------------------
//
// Cbuf_AddText-equivalent (FUN_00457c90, confirmed via raw disassembly -- plain
// __cdecl, void(int localClientNum, const char* text)): appends a real console
// command line to the engine's own per-client command buffer, drained and dispatched
// automatically on the engine's own next frame (Cbuf_Execute already runs every
// frame as part of the normal game loop -- this project does not need to, and must
// not, call that itself). This is the SAME mechanism the real Options UI's own
// ".menu" scripts use for `exec ...` actions (confirmed directly: the real
// all_restart_popmenu.menu's "Yes" action literally does `exec snd_restart;`) --
// unlike the small, mostly UI/profile/debug-only 132-entry command set this
// project's own earlier investigation (analog_input_hooks.cpp, "Investigation
// record") found "weapnext"/"togglemenu" missing from, `vid_restart`/`snd_restart`
// are exactly the class of system command that real menu flow is proven to reach
// through this path.
//
// `command` should end with a newline, matching the real function's own line-based
// parsing (confirmed via disassembly: it byte-scans for a "p0 "/"p1 " client-index
// prefix override, otherwise uses the passed-in localClientNum directly). Always
// passes localClientNum=0 -- correct for SP, this project's only current scope (see
// CLAUDE.md's "SP and MP are separate efforts").
void QueueConsoleCommand(const char* command);

// ---- Localized-string lookup ----------------------------------------------------
//
// SEH_GetString-equivalent (issue #68, 2026-08-05 language pass) -- resolves a real
// internal reference key (e.g. "MENU_QUIT", "MENU_RELOAD_WEAPON",
// "PLATFORM_LEADERBOARDS_SHORTCUT" -- confirmed real keys, see
// re_notes/known_issues.md issue #68 for how each was found via the zone_dump
// localizedstrings extraction) to whatever text the CURRENTLY ACTIVE game language
// renders for it. This is the actual fix for glyph/hint detection breaking under
// non-English languages: compare rendered hint text against THIS function's live
// output instead of a hardcoded English literal, and the comparison is correct for
// every language the game supports, automatically, with no per-language table
// needed. Confirmed via raw disassembly of FUN_00532230 -- plain __cdecl, single
// string arg, always returns a valid non-null pointer (falls back to echoing the
// raw key back if the key isn't found in the string table, never null).
const char* GetLocalizedString(const char* referenceKey);
