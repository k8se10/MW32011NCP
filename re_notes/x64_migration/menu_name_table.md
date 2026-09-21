# Menu name table (static, 2026-09-21)

Source: the four shared SP/Survival zones (`common.ff`, `common_survival.ff`, `common_specialops.ff`, `code_post_gfx.ff`)
inflated as a single zlib stream (a plain deflate at file offset 0x17) and scanned for identifier strings; the Unlinker was
not needed (it is still unreliable on these zones).

Topmost-menu name is read from the first field of the menu struct (`menu+0x0`, the windowDef name pointer) by
`GetTopmostMenuNameX64` (analog_input_hooks_x64.cpp) and logged as `[x64-menuname]`.

| Screen | Menu name (lowercase = menu, UPPERCASE = itemDef group) | Back glyph |
|---|---|---|
| Pause menu | `pausedmenu` (live); itemDef list `PAUSE_LIST` | hardcoded corner, design (1634.2, 981.7) |
| Survival buy stations | `survival_armory_*` (live: `survival_armory_equipment`; weapon/airsupport variants share the prefix) | hardcoded white box, design ~(1208, 827) |
| Restart-mission modal | `restart_warning` (live) | none |
| Difficulty popups | `popmenu_difficulty*`, `popmenu_specialops_*difficulty*` | none |
| Autosave / level-select overwrite | `popmenu_autosave_warning`, `popmenu_levelselect_overwrite` | none |
| Errors | `error_popmenu`, `error_popmenu_lobby/party/submenu` | none |
| Other popups | `popup_*`, `offensive_skip_popup*`, `cac_popup`, ... | none |

Not a menu name: `armory_unlock_hint`, `armory_unlock_hint_ss` (HUD hints, excluded by the `hint` check).
Full candidate list: the scan output (2778 strings) is not committed; regenerate with the PowerShell inflate-and-regex
snippet described above if more names are needed.

**Correction (2026-09-22, from the live `[x64-menuname-raw]` dump):** the menu name pointer is the second qword of the
menu struct (`menu+0x8`), not `menu+0x0`. Names seen live: `pausedmenu`, `restart_warning`, `pc_options_video_ingame`,
`survival_armory_equipment`, `savegame_warning_arcade`. The static-scan guesses above (`all_restart_popmenu`,
`armory*`) were wrong for the running game -- treat the live names as authoritative.
