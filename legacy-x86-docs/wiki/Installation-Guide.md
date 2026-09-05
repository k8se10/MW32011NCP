# Installation Guide

> **Experimental, alpha-stage software.** This hooks directly into a live game process. Expect bugs and the occasional need to fall back to keyboard/mouse. Not affiliated with, endorsed by, or sponsored by Activision, Infinity Ward, or any of their affiliates.

> **⚠️ Only v0.2.2 (GitHub-only) and v0.3.2+ are currently distributed.** Grab the latest release unless you have a specific reason not to — see [[Changelogs]] for what's changed and the [README security notice](https://github.com/k8se10/MW32011NCP#readme) for why older builds were archived.

## Requirements

- **Windows** (built for 32-bit/x86 targets specifically, matching the game's own binaries) — this project does not run on macOS/Linux directly, though multiple players have reported it working through **Proton on Steam Deck/Linux**; see [[Compatibility]] for the (community-confirmed, project-untested) detail.
- **Retail Steam copy of Call of Duty: Modern Warfare 3 (2011)** — this project has only been analyzed and tested against retail Steam's `iw5sp.exe` (Campaign/Survival). See [[Compatibility]] for what is and isn't supported.
- An Xbox-layout (or XInput-compatible) controller.

## Installation Steps

1. Download the latest release zip from the [GitHub Releases page](https://github.com/k8se10/MW32011NCP/releases).
2. Copy **`d3d9.dll`** from the zip into your MW3 install folder — the same folder that contains `iw5sp.exe` (e.g. `...\SteamLibrary\steamapps\common\Call of Duty Modern Warfare 3\`).
3. Launch the game and start a Campaign or Survival session as normal — no extra launch options or injector needed, the DLL loads automatically the same way the game's own `d3d9.dll` normally would.
4. A log file, **`proxy_d3d9.log`**, will appear in that same folder once the project's DLL is running. If something goes wrong, this is the first thing to check, and the most useful thing to attach to a bug report.

## First Launch

On first run, the project writes a fully-commented **`mw3ncp_config.ini`** next to the DLL, pre-filled with sane defaults — nothing needs to be configured by hand to get started. See [[Configuration]] for the full key reference if you want to tune sensitivity, button layout, or anything else.

## Updating

To update, just replace `d3d9.dll` with the new version from the latest release. Your `mw3ncp_config.ini` carries forward automatically (v0.2.5+) — any setting you already tuned is kept, and renamed/restructured keys are migrated to their new equivalent, so there's nothing to redo by hand.

## Uninstalling

Delete `d3d9.dll` from the install folder. Nothing else is modified — the base game files are never touched, so uninstalling is always a single-file removal.

## A Gotcha to Know About

Steam's **"Verify integrity of game files"** (or an automatic game update) may silently remove a dropped-in `d3d9.dll`, since it isn't part of the base game's file manifest. If controller input suddenly stops working after an update or a file-verification pass, this is the likely reason — just reinstall by repeating step 2 above.

## Next Steps

- [[Controller Setup]] — the full control map
- [[Configuration]] — tuning sensitivity, ADS slowdown, button/stick layout
- [[Troubleshooting]] — if something isn't working as expected
