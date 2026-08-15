# Custom Options Screen Coverage Audit — 2026-08-06

## Scope and confidence

Static code audit of the **actively edited working tree** only. This is **not**
a live-game test and does not claim that the existing menu paths are crash-free
or visually correct. It answers one implementation question: does the custom
Options screen currently expose every vanilla option and every persisted
non-meta setting in `mw3ncp_config.ini`?

**Answer: no.** The custom screen has broad vanilla coverage, but five vanilla
controls still need work and most mod configuration fields are not surfaced.

## Source-of-truth paths inspected

- `proxy_d3d9/src/vanilla_settings_table.h` — vanilla rows the custom UI can
  enumerate and edit.
- `proxy_d3d9/src/overlay_hud.cpp` — custom row model, tab dispatch, rendering,
  and input handling.
- `proxy_d3d9/src/mod_config.h` / `mod_config.cpp` — persisted ModConfig fields
  and the generated INI schema.

## Vanilla option coverage

`kVanillaSettings` models 59 rows across Look, Video, Audio, Voice, Advanced
Video, Movement, and Actions. The UI's `RebuildTabRowCache()` enumerates all
of those rows, so they are all visible in their corresponding tabs.

### Interactive now: 57 of 59 modeled rows

All modeled float/bool/known-enum rows are editable, and all 35 modeled keybind
rows route through the real keybind-capture path. Staged Video, Audio, and
Advanced Video changes use the existing pending-value/apply-confirm path.

### Modeled but currently read-only

| Row | Reason in code | Required completion work |
|---|---|---|
| Resolution (`ui_r_mode`) | Runtime-populated display-mode enum; no static `stringEnumValues` is supplied. `AdjustCurrentTabRow()` intentionally does nothing for a plain `DvarString`. | Read the game/display's runtime enum list and use it for selection. |
| Display Refresh (`ui_r_displayRefresh`) | Same runtime-enum limitation. | Read the game/display's runtime enum list and use it for selection. |

### Native options absent from `kVanillaSettings`

| Native row | Current code status | Required completion work |
|---|---|---|
| Color Blind Assist | Explicitly excluded as profile-backed data. | Locate/read the profile value and add a real get/set implementation. |
| Subtitles | Explicitly excluded as profile-backed data. | Locate/read the profile value and add a real get/set implementation. |
| Third Voice option | Explicitly omitted because the dvar/label pairing is not confirmed. | Re-read the actual menu/dvar wiring and add only after the exact backing value is confirmed. |

The table comments describe these omissions directly. The stated full target is
therefore 62 vanilla controls: 59 modeled now, two read-only, and three absent.

## Mod INI coverage

Excluding the schema-only `Meta.ConfigVersion`, the INI persists 45 mod
settings/fields. `g_optRows` currently exposes six directly usable controller
settings:

1. Sensitivity Horizontal
2. Sensitivity Vertical
3. Invert Look
4. Vibration Enabled
5. Stick Layout
6. Button Layout

The intended Custom Binds entry is currently non-functional (see the blocking
finding below), so its twelve persisted bindings are not currently accessible
from the UI either.

### Missing mod settings by INI section

| Section | Settings absent from the current UI |
|---|---|
| Look | `AdsSlowdownStrength`, `AdsSlowdownBaseline`, `AdsCloseRangeSlowdownStrength`, `AccelerationRampMs` |
| Stance | `ProneHoldThresholdMs` |
| Interact | `HoldThresholdMs` |
| Survival | `ReadyUpHoldThresholdMs` |
| Movement | `AutoMantleEnabled`, `AutoMantleForwardConeDegrees`, `AutoMantleMinStickMagnitude` |
| Bindings | `FlipTriggers`, `GlyphStyle` |
| CustomBinds | `Fire`, `Ads`, `Lethal`, `Tactical`, `ReloadUse`, `WeaponSwitch`, `Jump`, `CrouchProne`, `Sprint`, `Melee`, `Pause`, `Scoreboard` — editor code exists but is unreachable |
| Options | `UseCustomOptionsScreen` |
| Vibration | `FireIntensity`, `FireDurationMs`, `DamagePerPoint`, `DamageMaxIntensity`, `DamageDurationMs` |
| Overlay | `FontItalic`, `TestCycleAllVariants` |
| Experimental | `FireNotifyQueueKick`, `BindResolverHookLogging`, `BindResolverGlyphSubstitution`, `HudFontIdLogging`, `HudGlyphPositionLogging`, ~~`GlyphIconOverlay`~~ (removed, see note below), `ArmorFieldScanLogging` |

That leaves **39 of 45** non-meta persisted fields inaccessible in the current
UI (counts as originally audited 2026-08-06; see stale-field note below). Some
are deliberately diagnostic/experimental controls, but they are still part of
the requested "every config item" scope and need an explicit developer or
advanced-settings surface if that scope is retained.

> **Stale field note, added 2026-08-16**: `[Experimental] GlyphIconOverlay`
> (`glyphIconOverlayEnabled` in `mod_config.h`) was removed from the config
> schema entirely on 2026-08-16 — it is no longer a persisted field at all, not
> just re-defaulted. It was the master on/off switch for the controller-glyph
> overlay, shipped hardcoded `false` since issue #48 and never flipped on for
> release; this was the real root cause of the long-running "no glyphs"
> community reports (`known_issues.md` issue #74). Its removal means this
> audit's **45** total mod INI fields and **39 of 45** inaccessible count are
> both one lower as of that date (44 and 38 respectively) — left as originally
> written above since this document is a dated point-in-time snapshot, not
> living state; treat the 2026-08-06 counts as historical and this note as the
> correction.

## In-progress implementation: Custom Binds drill-down is not yet reachable

The Controller tab includes a `CustomBindsEntry` row that displays `EDIT >`.
The current in-progress refactor declares `g_optBindsDrilldownOpen` and
`g_optBindsDrilldownReturnRow`, moves Binds out of `kTabOrder`, and adds an
`EffectiveTab()` helper. The row dispatchers now use `EffectiveTab()`, and the
renderer correctly hides the tab bar and changes the title while the drill-down
is active. The mouse-click entry path and Back-to-Controller path are now also
wired. Those are real improvements over the prior snapshot.

However:

- The **controller** Select branch handles Stick Layout, Button Layout, bool
  rows, and vanilla keybind rows, but not `CustomBindsEntry`. Controller users
  therefore cannot open Custom Binds with A/Select; only the mouse path can.
- `CustomOptionsMenu_ResetOnMenuClose()` clears the ordinary layout drill-down
  but not `g_optBindsDrilldownOpen`. If the parent menu closes while Custom
  Binds is open, the next menu open can inherit the stale Binds state instead
  of reopening on Controller.

Therefore the new bind editor is mouse-reachable but not controller-reachable,
and its state is not safely reset on an external menu close. This is an
in-progress integration gap, not evidence that the new `EffectiveTab()` work
is wrong.

## Other in-progress UI improvements observed

- The top-level Binds tab was intentionally removed, leaving eight visible tabs
  and moving Custom Binds under Controller.
- Tab rendering is hidden when the intended Custom Binds drill-down is active.
- The main row list now computes a visible-row window and renders `MORE` hints,
  addressing tabs with more rows than fit vertically (notably Movement's 16 rows).

These changes improve the intended final navigation model, but require a live
test once the missing enter/exit transitions are wired.

## Completion plan

1. Add the missing controller Select case for `CustomBindsEntry`; the mouse
   entry and Back-return paths are already implemented. Also clear
   `g_optBindsDrilldownOpen` in `CustomOptionsMenu_ResetOnMenuClose()` so a
   forced parent-menu close cannot leak drill-down state into the next open.
   `EffectiveTab()` is already used by the row dispatchers in the current tree.
2. Extend the mod row schema beyond `FloatValue`/`BoolToggle`: unsigned-duration
   values, `GlyphStyle` enum, and a clear Advanced/Developer section for diagnostic
   controls. Reuse the same clamp/range constraints already enforced by
   `LoadModConfig()`.
3. Add the missing mod fields in user-facing groups: Look, Timing/Interaction,
   Movement, Bindings, Vibration, Overlay, and Advanced/Experimental.
4. Implement runtime enumeration for Resolution and Display Refresh.
5. Complete the three omitted vanilla controls only after their real profile/dvar
   backing values are confirmed.
6. Live-test every changed control: persistence through `SaveModConfig()`,
   reload/restart behavior for staged values, controller navigation, mouse input,
   and untouched keyboard/mouse behavior.

## Key code locations

- `overlay_hud.cpp`: `g_optRows` (~1630), `EffectiveTab()` (~2111), current
  row dispatch (~2218), `AdjustCurrentTabRow()` (~2436), and menu input
  (~3805).
- `vanilla_settings_table.h`: table declaration (~112) and deliberate exclusions
  (~10-14, ~123, ~130, ~138-141).
- `mod_config.cpp`: persisted-field loading (~712-796) and generated INI schema
  (~320-548).
