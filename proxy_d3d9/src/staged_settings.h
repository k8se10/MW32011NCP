// staged_settings.h -- the "Apply Settings?" deferred-write mechanism for staged
// (restart-required) vanilla settings (re_notes/known_issues.md issue #66 task #20).
//
// Real precedent: all_restart_popmenu.menu (re_notes/options_menu_full_map.md sec 1)
// -- backing out of Video/Audio/Advanced Video with a pending change shows a real
// "Apply Settings?" Yes/No popup; Yes commits + execs snd_restart (video's own
// restart is presumed folded into the real menu's generated cfg, not called
// separately in that popup); No reverts by re-execing a saved pre-change cfg.
//
// This project's own equivalent does NOT write the real dvar the moment the player
// adjusts a staged setting in the (future) replacement screen -- it holds the value
// PENDING here instead, only committing to the real dvar (and firing the real
// vid_restart/snd_restart) once CommitStagedSettings() is called. Chosen over
// "write immediately, revert on Cancel" because it never leaves a real dvar in an
// uncommitted state if the player quits mid-adjustment, and needs no revert/snapshot
// logic at all -- Cancel just discards state that was never written anywhere real.
#pragma once
#include <cstddef>

// Records a pending value for a staged setting (index into kVanillaSettings, which
// must have VanillaSettingDef::staged == true -- silently ignored otherwise, since
// non-staged settings should be written immediately via SetVanillaSettingFromString
// instead, not staged here). Does NOT touch the real dvar. Safe to call repeatedly
// for the same index; the latest call wins.
void SetStagedSettingPending(int settingIndex, const char* value);

// True if any staged setting currently has an uncommitted pending value. Callers use
// this to decide whether to show the "Apply Settings?" prompt at all when backing
// out -- matching the real menu, nothing pending means back out silently, no popup.
bool HasPendingStagedChanges();

// Discards every pending value without touching any real setting -- the "No" path.
void DiscardStagedChanges();

// Writes every pending value to its real dvar (SetVanillaSettingFromString), then
// fires whichever real restart command(s) the committed settings actually need:
// vid_restart if any pending Video/AdvancedVideo setting was committed, snd_restart
// if any pending Audio setting was committed. Clears all pending state afterward.
// The "Yes" path.
void CommitStagedSettings();

// The value a UI should DISPLAY for a setting: its pending value if one is staged,
// otherwise its current real live value -- so an unconfirmed change stays visible
// instead of reverting the instant the player releases the stick.
void GetStagedOrLiveValueString(int settingIndex, char* outBuf, size_t outBufSize);
