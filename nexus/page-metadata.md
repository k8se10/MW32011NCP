# Nexus page metadata (form fields, not free-text)

Reference for setting up/maintaining the actual Nexus page — these are
separate form fields/dropdowns on Nexus, not part of the page's own
description body.

## Category
Utilities / Miscellaneous — closest fit; Nexus's MW3 (2011) category list
doesn't have a dedicated "controller support" bucket. Re-check at
page-creation time in case one's been added.

## Suggested tags
`Controller Support`, `Gamepad`, `XInput`, `Accessibility`, `QoL`,
`Campaign`, `Survival`, `Alpha`

## Requirements
- Retail Steam copy of Call of Duty: Modern Warfare 3 (2011), current version
  (post the 2026-09-03 64-bit recompile)
- Windows, x64
- A real Xbox/PS/generic XInput-compatible controller

No other mods required. Does not modify any base game file on disk — the DLL
is the entire install.

## Permissions / credits
Full ready-to-paste text is in `credits.md` (converted to `credits.bbcode.txt`
for the actual Nexus "Credits" field) — don't duplicate it here, update that
file instead. Summary of the permissions logic behind it: source is free to
use/modify/fork; the project's own custom license (see `LICENSE` in the
source repo) forbids selling or charging for this project or any
derivative — it must stay free, otherwise forking/modifying is explicitly
welcomed (see `CONTRIBUTING.md`).

## Install instructions (short form, mirrors `description.md`)
1. Extract `d3d9.dll` into the MW3 install folder (same folder as
   `iw5sp.exe`).
2. Launch the game, start Campaign or Survival.
3. Check `proxy_d3d9.log` (appears in the same folder) if anything goes
   wrong.

Uninstall: delete `d3d9.dll`. Nothing else was touched.

## Main file

**No main file is currently live.** No `-x64` release has shipped yet — see
`README.md`'s release gate. Once one does, ship the same release zip already
built for GitHub releases (`d3d9.dll` + `LICENSE` + `PATCHNOTES.md` +
`README.txt`) rather than maintaining a separate Nexus-only archive.

## Things to double check before every Nexus update
- Nexus's own "Adult content"/"Contains mature content" flags — this project
  has no such content itself, but check current Nexus policy on
  injection-based/DLL mods generally in case that classification changes.
- Whether Nexus requires separate disclosure of the anti-cheat risk
  (Plutonium ban warning) in a dedicated warning field vs. just in the
  description body — check the current page-creation flow.
