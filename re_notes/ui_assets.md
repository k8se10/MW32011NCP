# Controller UI assets — scope note (2026-07-14)

## Locked scope clarification (user, 2026-07-14)
"Native" support means more than analog movement/look/aim-assist — it also means:
1. **In-game/menu button-prompt icons swap to controller glyphs** (Xbox-style
   A/B/X/Y/LB/RB/LT/RT/D-pad, not just relabeled keyboard hints) when a controller is
   the active input device, matching the console build's behavior.
2. **A real controller options menu/screen** (sensitivity, invert Y, deadzone, stick
   layout preset, vibration) — not just "it works," a proper settings UI for it.
This is folded into task #6 (controller menu/UI navigation) scope, not a separate
later add-on.

## Asset investigation (2026-07-14)
Checked whether the shipped PC game files already contain unused Xbox 360/PS3 button
icon art (would save having to author new icon assets from scratch). Searched every
`.iwd` archive (readable directly with `7z`, since `.iwd` is plain zip) for
controller-icon-sounding image names.

**Found (confirmed real, in `main/iw_04.iwd` and `main/iw_05.iwd`):**
- `images/ps3_lstick.iwi`, `images/ps3_rstick.iwi` — PS3 analog stick icons.
- `images/ui_button_xenon_dpad_64x64.iwi` — Xbox 360 ("Xenon" was its internal
  codename) D-pad button icon, `ui_` prefix confirms it's a real UI element, not a
  world texture.
- Also many `dpad_*`/`iw5_dpad_*` images — but these are the **killstreak-selection
  radial-wheel icons** (ac130/uav/predator missile/etc.), not controller-button
  prompts — same feature exists on PC via a keybind, unrelated to this investigation
  despite the "dpad" naming.

**NOT found in `.iwd`:** no full Xbox A/B/X/Y or LB/RB/LT/RT set, no PS3
Cross/Circle/Square/Triangle set, no generic "controller button" HUD prompt icons
(the kind that would replace "Press [E]" with a button glyph in-game).

**Not yet checked: `.ff` fast-file zone archives** (`zone/english/*.ff`,
`zone/dlc/*.ff`) — these are IW's proprietary compressed format, not plain zip, so
`7z` can't list them directly. UI textures actually used by the live menu system are
often baked into a fastfile zone (`code_pre_gfx.ff`, `ui_mp.ff`, `common.ff` are
likely candidates by name) rather than left loose in `.iwd`. **This is where the rest
of a full button-icon set most likely lives, if it exists at all** — needs a
CoD/IW5-specific fastfile unpacker tool (community tools like Wraith/Greyhound exist
for later IW-engine titles; IW5/MW3-specific compatibility not yet confirmed) before
this can be checked. Not yet done.

## Open question / next step for task #6
- Get a working `.ff` unpacker for this exact game version and check for a fuller
  console button-icon set inside the zone files before deciding whether new icon art
  needs to be authored from scratch.
- If a full set genuinely doesn't exist anywhere in the shipped files, icon art will
  need to be created/sourced separately — flag this back to the user before doing
  so, since it's an art-asset task different in kind from the RE/hooking work.
