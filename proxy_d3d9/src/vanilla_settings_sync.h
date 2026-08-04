// vanilla_settings_sync.h -- generic read/write bridge between the real engine
// (dvars + keybinds, via real_settings.h) and a string representation suitable for
// mw3ncp_config.ini, driven entirely by the kVanillaSettings table
// (vanilla_settings_table.h). One generic pair of functions instead of 40 hand-written
// per-setting getters/setters -- see vanilla_settings_table.h's own header comment for
// why a data-driven table was chosen over per-setting struct fields.
//
// Deliberately does NOT itself implement the staged-settings apply/restart flow
// (issue #66 task #20) -- SetVanillaSettingFromString writes the real dvar/keybind
// immediately via real_settings.h and returns; a caller writing a `staged` setting
// (VanillaSettingDef::staged) is responsible for triggering the real vid_restart/
// snd_restart exec afterward once that flow is built. Keeping this file's own
// responsibility narrow (string <-> real value, nothing else) so it stays reusable
// by both the ini mirror and the eventual replacement UI.
#pragma once
#include <cstddef>

struct VanillaSettingDef;

// Reads the setting's CURRENT REAL value (from the live dvar or keybind table, not
// from any cached/mirrored copy) and formats it into outBuf as plain text suitable
// for an ini value. For VanillaSettingKind::Keybind, writes both bound keynums
// (e.g. "87,-1" if only one key is bound, "-1,-1" if unbound) -- raw keynums, not
// display names, so this round-trips exactly through SetVanillaSettingFromString
// without depending on KeynumToDisplayName/KeyNameToKeynum being exact inverses of
// each other (unconfirmed either way).
void GetVanillaSettingValueString(const VanillaSettingDef& def, char* outBuf, size_t outBufSize);

// Parses `value` (as read from mw3ncp_config.ini, same format GetVanillaSettingValueString
// produces) and writes it to the real engine. For VanillaSettingKind::Keybind, binds
// each non-"-1" keynum found in the comma-separated list; does NOT first clear
// whatever is currently bound to the target command (matching the real "bind"
// command's own additive semantics) -- a full binary-identical restore-from-backup
// would need to unbind stale extra keys first, not yet implemented here.
void SetVanillaSettingFromString(const VanillaSettingDef& def, const char* value);
