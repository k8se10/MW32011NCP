# Real Options Menu — Full Structure, Settings, and Dvar Setter Map

**Status:** Investigating. **(2026-08-04, sixth follow-up round)** Full render-suppression research for the Options menu is DONE — see §13. No menu-level "hidden" runtime flag was found, but the real per-menu render entry point (`FUN_0050b740`/`FUN_004a4150`) was traced completely and is directly hookable with a simple pointer-identity check, no return-address gating needed — this is now a confirmed-viable, low-risk implementation path, not an open question. **(2026-08-04, third follow-up round)** §6's open question (whether `param_5=0` is safe for restart-required Advanced Video dvars) is now RESOLVED via decompile — see §10. The real native menu-defocus mechanism (why moving the mouse over blank space clears a real item's focus) is confirmed to be genuine per-frame mouse-position hit-testing against every itemDef's rect, not a callable "clear focus" function — see §11 (the "acceptable fallback" outcome, not the "best" one). Menu flow, tab structure, and every real vanilla setting's dvar/type are confirmed via direct `.menu` script extraction (ground truth, not guesswork). Three real Dvar setter functions (bool/string/float) are found with strong confidence via Ghidra cross-reference. **Scope was expanded 2026-08-04 by explicit project-owner direction: this is now a full Options-flow replacement (nothing excluded), and the ini mirror is reframed as a real local settings backup/restore system (independent of Steam Cloud sync issues) — Movement/Actions keybinds and the Controls tab are back in scope, and Subtitles/Color Blind need full read+write, not write-only.** Following that direction: the Dvar setter's persistence semantics (§6) are now resolved with high confidence (not just flagged as open). Real keybind GET **and SET** functions are now fully identified and decompile-confirmed (§7). The real Subtitles/Color Blind read-side accessor (`getprofiledata`) is NOT resolved — traced to the same class of dead end this project already hit once before with GSC method dispatch (§8); the write side (`exec profile_toggle...`) was already usable pre-expansion. No PC-side Margin/safe-area setting exists in the real in-game Options menu at all (contradicts an earlier assumption this project carried from console/Xenia research — see "Corrections" below).

Source: `ui.ff` extracted via OpenAssetTools' `Unlinker.exe` (dump already present at `D:\Tools\OpenAssetTools\zone_dump\ui\`, both a main-menu variant (`ui/pc_options_*.menu`) and the in-game/pause-menu variant this project actually cares about (`pc_options_*_ingame.menu`, flat in the same folder) — this document is built entirely from the `_ingame` set unless noted.

---

## 1. Menu flow

- The real pause menu (`pausedmenu.menu`) opens Options via `open pc_options_video_ingame;` — **Video is the real first/default tab**, not a separate hub menu. There is no `pc_options.menu`/`options.menu` container asset; each tab's own `.menu` file is independently opened by name and each one separately embeds the shared tab-bar widget.
- **`ingame.txt`** (the real preload manifest for in-game menus) confirms the complete real tab set, 7 tabs: `pc_options_video_ingame`, `pc_options_audio_ingame`, `pc_options_controls_ingame`, `pc_options_voice_ingame`, `pc_options_advanced_video_ingame`, `pc_options_movement_ingame`, `pc_options_actions_ingame`, plus `pc_options_look_ingame` — that's 8 files, so `OPTIONS_LIST_0..OPTIONS_LIST_6` (7 slots referenced elsewhere in this project's own code) likely maps 7 of these to visible tab buttons with one hidden/merged (unconfirmed which).
- **`OPTIONS_LIST` IS the tab bar, CONFIRMED, not the settings list** — this project's prior suspicion was correct. Every `pc_options_*_ingame.menu` file references `OPTIONS_LIST` only via shared tab-bar-highlight/nav-group plumbing (`ui_buttonNavGroupName == "OPTIONS_LIST"`, `OPTIONS_LIST_0` focus checks for the drill-in/out `execKeyInt` logic) — never as a container for that screen's own settings rows. **This means issue #66's current implementation (appending a row after `OPTIONS_LIST`'s last item) is anchored to the TAB BAR, not any settings list — it's very likely why the added row felt structurally wrong ("doesn't fit with the UI").** The real settings rows in each tab are plain sequential `itemDef`s with no shared group name pattern found (no per-row `name` fields on most control itemDefs) — a fullscreen REPLACEMENT (this project's new direction) sidesteps this problem entirely since it no longer needs to anchor to any specific real itemDef group.
- Tab drill-in/out uses real keyboard Left(156)/Right(157) `execKeyInt` codes (already known to this project) — `Right` moves focus from the tab bar into the localVarString-remembered last-focused row of the CURRENT tab's content (`ui_options_focus`); `Left` does the reverse, remembering the current row's name first.
- Each tab's `onESC` handler checks `menuisopen(...)` across all 5 non-Look/Movement/Actions tabs before deciding whether to prompt a video/audio restart popup (`all_restart_popmenu`) — i.e. Video/Audio/Advanced Video changes are NOT applied live, they're staged and require this restart-confirmation flow. Relevant for the "mirror to ini" plan: a value written to `mw3ncp_config.ini` and to the real dvar via a setter would still need this same real restart/apply flow for picmip/resolution/etc-class settings, or those specific settings should be scoped out of round 1 mirroring.

## 2. Every real vanilla setting found, by tab

Only rows with an actual `dvar`/`dvarFloat` binding are listed (label text pulled from the nearest `text`/`exp text` line, some approximate since labels and control itemDefs aren't always adjacent 1:1 in the raw dump — verify exact label-to-control pairing with a live screenshot before wiring, same as this project's own established methodology for every other UI position value).

**Look** (`pc_options_look_ingame.menu`):
| Label | Dvar | Type | Range/step |
|---|---|---|---|
| Sensitivity | `sensitivity` | float | step 5, range 1–30 |
| Invert Mouse (Pitch) | `ui_mousePitch` | bool (UI-proxy dvar, not the real gameplay dvar directly) | — |
| Smooth Mouse | `m_filter` | bool | — |
| Free Look | `cl_freelook` | bool | — |
| Look Up / Look Down / Hold Mouse Look / Center View | `+lookup` / `+lookdown` / `+mlook` / `centerview` | **keybinds, not dvars** | — |

**Video** (`pc_options_video_ingame.menu`):
| Label | Dvar | Type |
|---|---|---|
| Resolution | `ui_r_mode` | enum/string (UI-proxy) |
| Brightness | `profileMenuOption_Gamma` | float, step 0.01, range 0.5–1.5 (opens via a separate `brightness_adjust.menu` popup, same dvar) |
| Color Blind Assist | **NOT a dvar** — `getprofiledata("colorBlind")` / `exec profile_toggleColorBlind;` (profile data, see §3) |
| Subtitles (found in Audio tab, see below) | | |

**Audio** (`pc_options_audio_ingame.menu`):
| Label | Dvar | Type |
|---|---|---|
| (Game/Master) Volume | `profileMenuOption_volume` | float, step 0.008, range 0–0.8 |
| Output Config | `ui_outputConfig` | enum (UI-proxy) |
| Subtitles | **NOT a dvar** — `getprofiledata("subtitles")` / `exec profile_toggleSubtitles;` (profile data, see §3) |

**Voice** (`pc_options_voice_ingame.menu`):
| Label | Dvar | Type |
|---|---|---|
| Mic Sensitivity/Level | `winvoice_mic_reclevel` | float, step 655, range 0–65535 |
| (a Yes/No toggle, label not confirmed) | `dvarFloatList { "@MENU_NO" 1 "@MENU_YES" 0 }` — inverted bool-as-float | |
| Push-to-Talk | `+talk` | keybind |
| Voice enable | `cl_voice` | bool |

**Advanced Video** (`pc_options_advanced_video_ingame.menu`) — real graphics settings, 6 `dvarFloatList` enum rows plus several plain bool/enum `dvar` rows:
`ui_r_aspectratio`, `ui_r_aasamples` (AA: Off/2x/4x), `ui_r_displayRefresh`, `ui_r_vsync`, `sm_enable` (shadow maps), `r_specular`, `r_dof_enable` (depth of field), `ui_r_ssao` (Off/Low/High), `r_zfeather`, `fx_marks`, `ui_r_picmip_manual` (Automatic/Manual texture quality), `ui_r_picmip`/`ui_r_picmip_bump`/`ui_r_picmip_spec` (Low/Normal/High/Extra texture detail tiers). All UI-proxy `ui_*` dvars here are staged, not applied live — see the restart-popup flow in §1.

**Controls** (`pc_options_controls_ingame.menu`): no `dvar`/`dvarFloat` bindings found at all — this screen is purely the tab-bar host plus a "Reset to Defaults" action (`options_control_defaults.menu`), not a settings list itself.

**Movement** (`pc_options_movement_ingame.menu`) and **Actions** (`pc_options_actions_ingame.menu`): **100% keybind lists** (`dvar` field holds a bind command like `+forward`, `+sprint`, `togglecrouch`, `+actionslot 1`, `weapnext`, `+scores`, etc.), zero real value-settings. These are keyboard rebinding screens — there's no meaningful "vanilla setting" to mirror here for a controller-focused UI (rebinding a keyboard key has no controller analog); recommend explicitly excluding these two tabs from the "mirror every vanilla setting" scope rather than trying to force-fit them.

## 3. Corrections to prior assumptions

- **Subtitles and Color Blind Assist are PROFILE DATA, not dvars.** Both use `getprofiledata("subtitles"/"colorBlind")` to read and `exec profile_toggleSubtitles;` / `exec profile_toggleColorBlind;` (a real console-command exec, via the already-confirmed `Cbuf_AddText`/`Cbuf_Execute`/`Cmd_ExecuteString` triplet) to write. This is a DIFFERENT real subsystem than the Dvar system this project already has getters/setters for — a `GetProfileData`-equivalent function has NOT been researched or found this pass. Toggling these from a custom UI is possible (queue the same `exec profile_toggle...` command string, which this project can already do), but READING the current value natively needs new research.
- **No PC in-game Margin/Safe-Area setting exists.** Searched all 8 `_ingame` tabs plus a broad `safearea`/`margin` grep across the entire extracted `ui.ff` dump — zero matches in any Options tab. This project's own CLAUDE.md carries a note (from earlier Xenia/console research) that the real console Options screen lists Horizontal/Vertical Margin — that's real for CONSOLE but does not carry over to this PC build's in-game Options menu at all. Recommend dropping Margin from the "mirror every vanilla setting" scope — there is nothing on PC to mirror.

## 4. Real Dvar SETTER functions — found via Ghidra, HIGH confidence, NOT live-verified

Method: `FUN_0062abe0` (the already-confirmed `Dvar_FindVar`-equivalent this project's own `GetDvarInt`/`GetDvarString` already call) was cross-referenced for ALL callers via `re_notes/ghidra_scripts/FindCallers.java` against `iw5sp.exe` (Ghidra project `D:\Tools\ghidra_projects`, program `MW3`). 20 real callers found; three follow the EXACT same shape as the known getters (find dvar by name, then call a type-specific accessor with the found pointer) but WRITE instead of read:

- **`FUN_0044d700(const char* name, bool value)`** → on success, calls `FUN_0042b500(dvarPtr, value, 0)`. Real bool setter candidate (`Dvar_SetBool`-equivalent). Decompiled body:
  ```c
  void FUN_0044d700(undefined4 param_1,undefined4 param_2) {
    int iVar1 = FUN_0062abe0();
    if (iVar1 != 0) { FUN_0042b500(iVar1, param_2, 0); return; }
    // dvar not found -> logs a warning via FUN_0062b610(7, ...)
  }
  ```
- **`FUN_005396b0(const char* name, const char* value)`** → on success, calls `FUN_0051a070(dvarPtr, value, 0)`. Real string setter candidate (`Dvar_SetString`-equivalent).
- **`FUN_005513c0(const char* name, float value)`** → on success, calls `FUN_0047e690(dvarPtr, value, 0)`. Real float setter candidate (`Dvar_SetFloat`-equivalent).
- (`FUN_00497de0(name, x, y, z)` is the same pattern for a 3-float/vector dvar via `FUN_0044a2f0` — lower priority, no vec3 settings identified in §2.)

All three share the third argument `0` (likely a `setSource`/flags enum — the existing `Dvar_FindVar`-based getters don't take this, so its exact meaning is unconfirmed; other callers in the same xref dump pass `1` or `param_3` here, e.g. `FUN_00522290`/`FUN_0047df30`, suggesting it's a real enum worth understanding before shipping, not just a magic 0). No int-specific setter of this exact shape was found in the 20 callers — the bool setter (`FUN_0044d700`, byte-sized value) is the closest candidate for int-typed dvars given how the existing `GetDvarInt` getter already reads the same `+0xc` union slot as a raw int regardless of declared type.

**Not done this pass**: no live call was actually made to any of these three functions to confirm they work end-to-end (e.g. hooking one in the proxy DLL and verifying a real in-game value changes) — confidence is from decompiled structural pattern-matching only, same bar as this project's other "found via signature, not yet live-tested" entries.

## 5. Most important open question (pre-expansion) — see §6 for the resolution

Whether `FUN_0044d700`'s 3rd argument (`0` in every read call site) is safe to pass as a constant `0` for this project's use, or whether it's a real enum (e.g. `DVAR_SOURCE_INTERNAL` vs `DVAR_SOURCE_EXTERNAL`/`_PROFILE`) that affects whether the change persists to the user's profile/config the same way the real menu's own slider-drag does — get this wrong and a mirrored setting could work visually in a live session but silently fail to persist across restarts. Needs either a decompile of `FUN_0042b500`/`FUN_0051a070`/`FUN_0047e690` themselves (not done this pass — only their callers were inspected) or a live test with save/restart, before this project relies on these setters for real config-changing behavior.

---

## 6. (2026-08-04, scope-expansion follow-up) Dvar setter 3rd argument — RESOLVED via decompile, real semantics found

Decompiled the three inner writer functions directly (`FUN_0042b500`, `FUN_0051a070`, `FUN_0047e690` — the functions `FUN_0044d700`/`FUN_005396b0`/`FUN_005513c0` from §4 call after finding the dvar). All three funnel into the SAME shared low-level commit function, **`FUN_0062a8c0`** — this is the real sink that actually consumes the mystery 3rd argument (renamed `param_5` in its own decompile), so it was decompiled too.

**Confirmed real behavior of `param_5`** (the value every setter this project found passes as `0`):

```c
if ((param_5 == 1) || (param_5 == 2)) {
    uVar2 = *(uint *)(in_EAX + 4);              // the dvar's own flags word
    if ((uVar2 & 0x2800) != 0) { return; }       // refuse the write entirely if these flag bits are set
    if ((param_5 == 1) && ((uVar2 & 4) != 0) && (DAT_0092e97c == '\0')) { return; } // param_5==1 additionally gated by a flag bit + a global byte (very likely a cheats-enabled/dev-mode flag)
    if ((uVar2 & 2) != 0) {                      // flag bit 0x2 -- almost certainly DVAR_LATCHED
        FUN_0062a7c0(param_1,param_2,param_3,param_4);  // diverted to a SEPARATE "staged/latched" set path, not the immediate write below
        return;
    }
}
// ...unconditional immediate write into the dvar's live value slot follows for any other param_5, including 0
```

**What this means, concretely: passing `param_5 = 0` (what every setter this project found actually does) SKIPS this entire permission/latch-diversion block and goes straight to the immediate, unconditional write.** This is almost certainly NOT the identical code path a real menu slider drag uses — the real UI very likely passes `1` or `2` (its own real "this came from the user-facing menu" source enum) specifically so that (a) read-only/protected dvars correctly refuse the change, and (b) `DVAR_LATCHED` dvars (flag bit `0x2`) get properly staged for the restart-popup flow this project already found in §1/§2 (Video/Audio/Advanced Video settings), rather than written live immediately.

**Practical conclusion for implementation**: for the mirrored settings that are NOT latched (most bools/floats — Sensitivity, mic level, mouse settings, etc.), `param_5 = 0` is very likely fine and behaves the same as the real menu, since the special-casing only activates for the specific flag bits checked. For the settings already identified in §2 as staged/restart-required (the `ui_r_*`/picmip/AA/shadow family under Advanced Video, gated behind `all_restart_popmenu` in the real `.menu` scripts), **`param_5 = 0` likely writes the value live/immediately instead of properly latching it for the real restart-apply flow** — this could be BETTER (instant apply, no restart needed) or WORSE (skips real side-effect/staging logic the restart flow depends on, e.g. actually reinitializing the renderer) — genuinely unconfirmed which, and this is exactly the kind of thing that needs a live test (toggle a latched setting via the setter with `param_5=0`, confirm the effect actually applies correctly and survives a restart), not further static analysis. `FUN_0062a7c0` (the latch-diversion path) was not itself decompiled this pass — doing so would likely fully resolve this without needing a live test, and is the cheapest next step if more certainty is wanted before wiring latched settings specifically.

---

## 7. (2026-08-04, scope-expansion follow-up) Real keybind GET and SET functions — CONFIRMED, both sides, decompile-verified

**The real keybind table**: a flat array at `DAT_00a98e4c`, one block per local-player config index, each block holding 256 key slots (`0x100`), each slot 3 `int`s (12 bytes) wide — slot 0 of each key holds the compiled numeric ID of whatever command string is bound to that key (0 = unbound). Total table size confirmed exactly: `FUN_00455450` (unbindall, below) loops until `puVar4 < 0xa99a4c`, and `0xa99a4c - 0xa98e4c = 0xC00 = 256 * 3 * 4` bytes — matches exactly.

- **GET** — `FUN_0057e770(configIndex, outBuf)`: the real function `analog_input_hooks.cpp`'s own "bind resolver" comment already pointed at (`FUN_0061f6f0`) calls into this. It calls `FUN_0057e640(configIndex)`, the actual core lookup: resolves the target command string to its numeric ID via `FUN_005330a0` (also used by GET's own caller chain), then linearly scans all 256 key slots in that config's block comparing each slot's stored ID against the target, returning a count (0/1/2, capped at 2 — this engine reports "key1 OR key2" if two keys share a bind, matching the `"KEY_OR"` string seen in `FUN_0061f6f0`) plus the found keynum(s) via output params. `FUN_0057e770` then formats the found keynum(s) into a display string via `FUN_004bea00` (keynum → key-name string).
- **SET** — `FUN_0044a900(configIndex, keynum, commandId)`. Found via the real `"bind"` console command handler (`FUN_00426c40`, registered by name `"bind"` in `FUN_00514d00` via the real `Cmd_AddCommand`-equivalent `FUN_00558820`) — decompiled directly: it resolves the key-name argument to a keynum via `FUN_00508e70` (a `Key_StringToKeynum`-equivalent) and the command-string argument to a numeric ID via the SAME `FUN_005330a0` the getter uses, then calls `FUN_0044a900(0, keynum, commandId)`. This is the real, direct `Key_SetBinding`-equivalent.
- **UNSET one key** — confirmed via the real `"unbind"` command handler (`FUN_00421040`): resolves the key name to a keynum the same way, then writes `0` directly into that key's table slot (`(&DAT_00a98e4c)[keynum * 3] = 0`) — equivalent to calling the setter above with `commandId = 0`.
- **UNSET all** — confirmed via the real `"unbindall"` command handler (`FUN_00455450`): the same direct-write-zero, looped across all 256 slots.

All four are cleanly decompiled with a consistent, mutually-corroborating table layout (getter's stride/bound exactly matches the two clear-handlers' stride/bound) — HIGH confidence, though like the dvar setters, NOT yet live-tested end-to-end (no live call made from this project's own proxy DLL to confirm `FUN_0044a900` actually rebinds a key when called from our own hook rather than from the real command parser).

## 8. (2026-08-04, scope-expansion follow-up) Real `GetProfileData`-equivalent — NOT resolved, same class of dead end this project already hit once

The menu-script builtin name `"getprofiledata"` is confirmed declared in a real native name array at `0092d9fc` (one of ~12 profile-related builtin names in that immediate table: `coopplayer`, `iscoop`, `getpartystatus`, `getsearchparams`, `gettimeplayed`, `isselectedplayerfriend`, `getcharbyindex`, `getprofiledata`, `getprofiledatasplitscreen`, `isprofilesignedin`, `getwaitpopupstatus`, `getnattype`) — but this table is a plain array of name pointers with **no interleaved function pointer** and **not in alphabetical order** (ruling out a simple binary-search-by-name pattern), meaning the matching function-pointer array (if one exists in this same parallel-array form) was not located this pass, and the dispatch mechanism connecting a menu-script builtin call to its real native implementation was not traced.

Directly searching for the two REAL profile-key strings this project actually needs (`"colorBlind"`, `"subtitles"`) came back with **zero code references to either** — both exist as plain string data with no instruction operand pointing at them that Ghidra's reference analysis catches. This is the same category of dead end this project already documented once before for the GSC method-name table's `coopready` entry (`re_notes/known_issues.md` issue #5): **the compiled script bytecode almost certainly references these strings/builtins by a compile-time numeric ID baked into the menu bytecode itself, not by a runtime string lookup a static string-xref search can trace.** Recommend NOT sinking more static-analysis time into this specific path — the write side (`exec profile_toggleSubtitles;`/`exec profile_toggleColorBlind;` via the already-working `Cbuf_AddText`/`Cbuf_Execute`/`Cmd_ExecuteString` triplet) is already usable standalone; for the READ side, a live technique (memory-diffing the profile-data blob before/after toggling each setting in the real menu, the same `xenia_probe`/`memdiff`-style technique this project has already used successfully elsewhere) is the recommended next approach over further static tracing.

## 9. Most important remaining open question (post-expansion) — RESOLVED, see §10

Whether `FUN_0062a7c0` (the LATCHED-dvar diversion path found in §6, not itself decompiled this pass) does something a naive `param_5=0` immediate write would miss for the restart-required settings under Advanced Video — this is the one piece of the three follow-up items that has a clear, cheap next step (one more decompile) rather than needing a live test. The keybind get/set functions (§7) and the profile-data read gap (§8) are otherwise fully characterized to the limit of what static analysis can answer here.

---

## 10. (2026-08-04, fourth follow-up round) `FUN_0062a7c0` decompiled — definitive answer on `param_5=0` for latched dvars

Decompiled directly:

```c
void FUN_0062a7c0(int param_1,undefined4 param_2,undefined4 param_3,undefined4 param_4)
{
  switch(*(undefined1 *)(in_EAX + 8)) {   // dvar type tag, same field the getters/setters already use
  case 2:
    *(int *)(in_EAX + 0x1c) = param_1;    // writes ONLY the +0x1c.. slot(s)
    *(undefined4 *)(in_EAX + 0x20) = param_2;
    return;
  case 3: case 9:
    *(in_EAX+0x1c)=param_1; *(in_EAX+0x20)=param_2; *(in_EAX+0x24)=param_3;
    return;
  case 4:
    *(in_EAX+0x1c)=param_1; *(in_EAX+0x20)=param_2; *(in_EAX+0x24)=param_3; *(in_EAX+0x28)=param_4;
    return;
  default:
    // same as case 4 -- writes only +0x1c/+0x20/+0x24/+0x28
  case 7:
    // string/enum type: only updates the SAME +0x1c "pending" slot via string-intern helpers, never +0xc
  }
}
```

Cross-referencing this against `FUN_0062a8c0`'s own immediate-write switch (already decompiled in §6/§4) makes the dvar struct layout unambiguous: **`+0xc` (and `+0x10/+0x14/+0x18` for multi-component types) is the LIVE/current value — the exact same field `GetDvarInt`/`GetDvarString`/every getter already reads — while `+0x1c` (and `+0x20/+0x24/+0x28`) is a SEPARATE "latched/pending" value.** This is the classic id-Tech `dvar_t { current; latchedValue; }` shape. `FUN_0062a8c0`'s own immediate-write path (cases 2/3/9/4) writes BOTH the live slot AND the latched-mirror slot together in the same call; `FUN_0062a7c0` (the diversion taken only when `param_5` is 1/2 AND the dvar's flag bit `0x2` — almost certainly `DVAR_LATCHED` — is set) writes ONLY the latched slot, deliberately leaving the live slot (and therefore what every getter reports, and what the renderer/audio subsystem actually reads at the moments it consults the dvar) untouched until some separate "commit latched values" step runs (real cvar systems in this engine lineage typically do this at a `vid_restart`/`snd_restart`/level-load boundary).

**Definitive conclusion**: passing `param_5=0` (what all three of this project's found setters do) is **NOT a shortcut that skips persistence — it is a MORE immediate/aggressive write than the real menu UI's own path.** It writes the live value directly, taking effect immediately, and also keeps the latched-mirror slot consistent (so nothing is left in a torn state). The real menu's own path (presumably `param_5=1` or `2`) deliberately does NOT touch the live slot for a `DVAR_LATCHED` dvar — it only stages the pending value, and relies on the SEPARATE real restart/apply flow (the `all_restart_popmenu`/`vid_restart`-family exec calls already found in §1's `onESC` handlers) to actually reinitialize the renderer/audio device and pick up the new value. **Practical implementation guidance**: using this project's found setters with `param_5=0` on a `DVAR_LATCHED` setting (the Advanced Video family: `ui_r_aasamples`, `ui_r_vsync`, `sm_enable`, `ui_r_picmip*`, etc.) will change the underlying dvar's value correctly and immediately, but will NOT by itself trigger whatever real device/subsystem reinitialization the restart flow performs — the visual/audible effect will likely not actually apply until something else (a real `vid_restart`/`snd_restart` exec, which this project already knows how to queue via the existing `Cbuf_AddText`/`Cbuf_Execute`/`Cmd_ExecuteString` triplet) runs afterward. **Recommendation for implementation**: for any mirrored setting already identified in §1/§2 as behind the real restart-popup flow, follow the setter call with the same real restart exec command the `.menu` scripts themselves queue, rather than assuming the dvar write alone is sufficient — this is now a concrete, actionable design note, not an open question.

## 11. (2026-08-04, fifth follow-up round) Native menu defocus mechanism — CONFIRMED as real per-frame mouse hit-testing (the "acceptable fallback" outcome)

No standalone, directly-callable "clear focus"/`Menu_ClearFocus`-style function was found. Instead, traced the real per-frame mechanism concretely:

- **`FUN_006239e0`** is the real per-frame per-menu mouse-processing loop (found via `FindCallers` on the same `GetTopmostActiveMenu`-equivalent, `0x00547980`, this project's own `TryGetRealFocusedGroupAndIndex` already calls). It reads a live mouse X/Y (`in_EAX[4]`/`in_EAX[5]`, from the same context struct `GetTopmostActiveMenu` operates on) and walks the SAME item-count/item-array pair (`+0xa8`/`+0xac`) `TryGetRealFocusedGroupAndIndex` already walks. Confirmed running TWICE per relevant pass (`local_8` loop bound `< 2`) — almost certainly once for each of two focus layers/players (splitscreen-shaped, matching every other per-player-indexed field already found in this struct, e.g. `+0x48 + *in_EAX*4` and `+0x4c + *in_EAX*4`).
- For each item, it calls **`FUN_0061a2d0(mouseX, mouseY)`** — a point-in-rect test (returns 0 for "inside"/hit, based on the surrounding `if (iVar5 == 0)` branching) — this is the real mouse-hit-test function. On a hit, it proceeds into hover-enter-style handling (`FUN_0061d4b0`, `FUN_004863e0`, `FUN_00620ec0`) that (per `FUN_00620ec0`'s own decompile, §-adjacent) reads and reacts to the SAME `+0x48 + playerIndex*4` flags field `TryGetRealFocusedGroupAndIndex` reads for the "is this item focused" check.
- **The exact single instruction that CLEARS a previously-focused item's bits once the mouse moves off it was not pinned** (would need either a live breakpoint/memory-write-watchpoint session — not attempted, this was kept to static analysis per the coordinator's own scoping — or considerably more decompile time tracing every branch of `FUN_006239e0`/`FUN_00620ec0`/`FUN_0061a2d0`'s callees). Given the loop structure (`local_c` tracks at most ONE "current hover candidate" per pass, reset to 0 at the top of every call), the most likely real mechanism is that a previously-focused item simply never gets its bits RE-set on a frame where the mouse no longer hits it, while something upstream of this loop (not identified) clears ALL items' transient hover bits at the start of each pass before this hit-test loop re-sets the (at most one) real hit for that frame — a common "clear-then-set" pattern for this class of per-frame UI state, but not directly observed in the decompiled code read this pass.

**Confirmed answer to the actual question asked**: this IS genuinely driven by real per-frame mouse-position hit-testing against live itemDef rects (`FUN_0061a2d0`), not some other trigger (not a click event, not a focus-change notification, not GSC-script-driven) — the **"acceptable fallback" implementation path is valid**: synthesizing a real `WM_MOUSEMOVE` to a known-blank screen coordinate via the game's own `WndProc` (the same technique already proven for `SendSyntheticActivationClick`/the Survival ready-up F5 synth) should reach this exact same native hit-test loop next frame, find no item under that coordinate, and should therefore result in no item being re-flagged as focused that frame — very likely achieving the desired "real menu shows nothing focused" state this project wants for the custom Options screen replacement (issue #66), though this specific end-to-end behavior (synthetic mouse move → confirmed-empty real focus) has NOT been live-tested yet.

## 13. (2026-08-04, sixth follow-up round) Suppressing the real Options menu's rendering entirely

Project owner chose FULL suppression (not an opaque panel drawn on top) for the custom Options replacement. Traced the real per-item and per-menu render call chain completely, from the known low-level quad-draw primitive (`0x004d48f0`, already used for the cursor-suppression precedent) upward, via a chain of single-caller functions (each found by taking the previous function's sole real caller):

```
FUN_0050b740(menuPtr)   -- layer 0 (background/decoration items), and
FUN_004a4150(menuPtr)   -- layer 1 (interactive/foreground items, only if menuPtr+0x20 flag was set by layer 0)
    -> FUN_00626ca0(menuPtr, layerBit)      -- REAL per-menu render loop: walks the menu's
                                                item array(s) (+0x38/+0xa38 for layer-0 items,
                                                +0xa3c/+0xa7c for the z-ordered layer-1 list),
                                                filtering each item's own layer bit
                                                (itemPtr+0x48, bit 0x400000) against layerBit
        -> FUN_00475550(playerIdx, itemPtr) -- REAL Item_Paint: gated at its very first line by
                                                FUN_0044c860(playerIdx, itemPtr) (the real
                                                Item_Visible-equivalent -- see below), then
                                                evaluates rect/color and draws the item's own
                                                background quad via FUN_004d48f0, then...
            -> FUN_006256a0(itemPtr)        -- type-specific paint dispatch (background focus-
                                                color selection reads the SAME itemPtr+0x48 bits
                                                0x4/0x2 TryGetRealFocusedGroupAndIndex reads)
                -> FUN_006235c0 / FUN_00622bb0 / etc. per item type
```

`FUN_0050b740` is confirmed called from exactly ONE real site (`FUN_00478540`, the same cursor-draw function this project already hooks/suppresses, calling `FUN_0050b740(&DAT_01c00458)` immediately before drawing the cursor) — i.e. it's the real "paint the current/topmost menu" entry point, run once per frame for whichever menu is active. `FUN_004a4150` likewise has one real caller (not traced further this pass, presumably a later point in the same frame for correct z-ordering of always-on-top items).

### Deliverable 1 (best case: menu-level visibility flag) — NOT FOUND

Neither `FUN_0050b740` nor `FUN_00626ca0` has any "is this whole menu hidden, bail before touching any item" check at their top — both unconditionally process every item in the given menu's arrays. No menu-level runtime "hidden" bit equivalent to the item-level focus bits was found.

A PER-ITEM equivalent DOES exist and was found and decompiled (`FUN_0044c860`, the real Item_Visible check `FUN_00475550`/Item_Paint calls first): a simple per-player-indexed byte flag at `itemPtr + 0x4C + playerIndex*4`, bit `0x4` — if clear, the item is skipped before any of the expensive compiled `visible when(...)` expression evaluation runs (that evaluation itself happens deeper in the same function, gated behind separate bits `0x10000000`/`0x20000000`/`0x40000000` in the `itemPtr+0x48` flags dword, calling out to real expression-evaluator functions `FUN_00543550`/`FUN_005315d0`/`FUN_00531ce0`). This is real and directly usable, but it is PER-ITEM, not per-menu — using it to hide the whole Options screen would mean writing this bit to EVERY itemDef in that menu's array (once, or every frame if something resets it), not a single clean toggle. Given deliverable 2 below is a single, clean, one-time hook with no such per-item bookkeeping, deliverable 1's per-item flag is NOT recommended as the primary mechanism, though it's worth keeping on file as a documented, real, alternate lever if the hook approach ever hits an unforeseen problem.

### Deliverable 2 (fallback: interceptable render call site) — FOUND, RECOMMENDED

**Recommendation: hook `FUN_0050b740` and `FUN_004a4150` directly** (plain MinHook function-entry detours, not return-address-gated primitive intercepts) with a check at the top of each: `if (param_1 == <the Options menuDef pointer>) return;` — otherwise call through to the real trampoline unchanged. This is simpler than the cursor-suppression precedent's return-address gate (`Hook_004d48f0`) because these two functions are not shared low-level primitives called from 30+ unrelated sites — each is already called from exactly one real site, once per frame, for whichever menu is currently being processed, so gating on the `menuDef*` argument's identity is sufficient and precise: only the Options menu's OWN two paint passes are skipped; every other menu (main menu, pause menu itself, popups, HUD) continues rendering through the exact same unmodified functions. The menu pointer to compare against is obtainable the same way this project's own `GetTopmostActiveMenu()` already gets it (confirmed this project's existing code already calls the real `0x00547980` function for this purpose) combined with the already-proven `TryGetRealFocusedGroupAndIndex`-style group-name check to confirm it's specifically the Options screen before caching/comparing its pointer.

### Deliverable 3 (must not break focus/input tracking) — SATISFIED BY CONSTRUCTION

This approach only touches the RENDER call chain (`FUN_0050b740`/`FUN_00626ca0`/`FUN_00475550`/`FUN_006256a0`, all confirmed draw-only). It does NOT touch the separately-confirmed INPUT/focus call chain from the earlier defocus research (`FUN_006239e0`/`FUN_00620ec0`/`FUN_0061a2d0`, the real per-frame mouse-hit-test loop) or the `itemPtr+0x48` focus-flag bits `TryGetRealFocusedGroupAndIndex` reads — none of those are called from inside the render functions being suppressed (`FUN_006256a0`'s own focus-flag READ, used only to pick a text-color scheme, still runs internally but its result is simply never drawn since the whole call is skipped one level up). The real menu's own focus tracking and input handling continue running completely unaffected — satisfies the "must not break" requirement directly, with no additional caveat needed.

## 12. Final open items before a confident full implementation round (post all research passes)

1. **Live-test the synthetic-mouse-move defocus approach** (§11) — the cheapest, highest-value remaining validation: send a synthetic `WM_MOUSEMOVE` to a blank coordinate while a real menu is open, and confirm via `TryGetRealFocusedGroupAndIndex` (already live-proven elsewhere in this project) that it reports no focus afterward.
2. **Live-test the dvar/keybind setters end-to-end** (§4/§7/§10) — none of `FUN_0044d700`/`FUN_005396b0`/`FUN_005513c0` (dvar) or `FUN_0044a900` (keybind) have been called from this project's own proxy DLL yet; all findings are static-decompile-only.
3. **Profile-data read (§8) needs a different technique** (live memory-diff, not static tracing) if Subtitles/Color Blind read-back is still wanted — flagged already, not resolved, not blocking the rest of the implementation.
4. **Live-test the render-suppression hook (§13)** — hooking `FUN_0050b740`/`FUN_004a4150` and confirming the Options menu's items genuinely stop drawing (while its focus/input tracking keeps working, and every OTHER menu still renders normally) has not been done; all of §13 is static-decompile-only, same caveat as everything else in this document.
5. Everything else this document set out to answer (menu flow/tab structure, every vanilla setting + dvar, the dvar setter persistence model, keybind get/set, the defocus mechanism, and render suppression) is now characterized to the limit of what static analysis alone can determine.
