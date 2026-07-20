# Nexus page metadata (form fields, not free-text)

Reference for setting up/maintaining the actual Nexus page — these are separate
form fields/dropdowns on Nexus, not part of the BBCode description.

## Category
Utilities / Miscellaneous — closest fit; Nexus's MW3 (2011) category list doesn't
have a dedicated "controller support" bucket. Re-check at page-creation time in
case one's been added.

## Suggested tags
`Controller Support`, `Gamepad`, `XInput`, `Accessibility`, `QoL`, `Campaign`,
`Survival`, `Alpha` (or whatever Nexus's current tag taxonomy calls
work-in-progress status)

## Requirements
- Retail Steam copy of Call of Duty: Modern Warfare 3 (2011)
- Windows (built 32-bit/x86 specifically, matching the game's own binaries — not
  tested on anything else)
- A real Xbox/PS/generic XInput-compatible controller

No other mods required. Does not modify any base game file on disk — the DLL is
the entire install.

## Permissions / credits template
- **Uploaded by**: (Nexus account name)
- **Original author**: same
- **This mod bundles**: [MinHook](https://github.com/TsudaKageyu/minhook)
  (Copyright © 2009–2017 Tsuda Kageyu, BSD 2-Clause-style license) and the
  Hacker Disassembler Engine (HDE) 32/64 C it bundles, same license style.
- **Permissions**: source is free to use/modify/fork; the project's own custom
  license (see `LICENSE` in the source repo) forbids selling or charging for this
  project or any derivative — it must stay free. Full license text should be
  linked/quoted on the Nexus page's own permissions tab, not just implied.
- **This does NOT meet Nexus's usual "ask permission before reuploading" norms
  automatically** — the no-charge restriction is the one hard rule; otherwise
  forking/modifying is explicitly welcomed (see `CONTRIBUTING.md`).

## Install instructions (short form, mirrors `description.bbcode.txt`)
1. Extract `d3d9.dll` into the MW3 install folder (same folder as `iw5sp.exe`).
2. Launch the game, start Campaign or Survival.
3. Check `proxy_d3d9.log` (appears in the same folder) if anything goes wrong.

Uninstall: delete `d3d9.dll`. Nothing else was touched.

## Main file
Ship the same release zip already built for GitHub releases
(`MW3NCP-vX.Y.Z.zip` — `d3d9.dll` + `LICENSE` + `PATCHNOTES.md` + `README.txt`)
rather than maintaining a separate Nexus-only archive — one build artifact, two
distribution points, less to keep in sync.

## Things to double check before every Nexus update
- Nexus's own "Adult content"/"Contains mature content" flags — this project has
  no such content itself, but check current Nexus policy on injection-based/DLL
  mods generally in case that classification changes.
- Whether Nexus requires separate disclosure of the anti-cheat risk (Plutonium
  ban warning) in a dedicated warning field vs. just in the description body —
  check the current page-creation flow, this may have a dedicated UI element.
