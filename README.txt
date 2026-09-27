MW32011NCP -- Native Community Patches for MW3 (2011)
v0.0.3-x64

WHAT THIS IS
------------
Native controller support (movement/look/every button hooked directly into
the game's own real engine functions, not a keyboard/mouse emulation
mapper), a visual/performance enhancement suite (render scale, motion blur,
FSR sharpening, frame pacing, Vulkan rendering and optional NVIDIA
DLSS/DLAA), and four fixes for real, exploitable vulnerabilities in MW3's
own base-game netcode -- all built in and enabled by default, except DLSS,
which is opt-in.

Survival's own controller support is Gameplay Complete: every core control
is live-confirmed working, including Predator Missile guidance. Campaign
ships best-effort (never a release gate, same as the prior 32-bit line).
Multiplayer has controller menu navigation (D-pad + A/B, B back) but no
in-game controller movement/aiming yet; render scale and the performance
fixes work there too.

NEW IN v0.0.3-x64
-----------------
- Vulkan rendering is now the default in Campaign/Survival, through this
  project's own DXVK fork built into d3d9.dll. Set GraphicsApi=LegacyD3D9
  in mw3ncp_config.ini to go back to Direct3D 9. Multiplayer always uses
  Direct3D 9.
- NVIDIA DLSS/DLAA for RTX GPUs (Campaign/Survival, Vulkan only): set
  StreamlineEnabled=1. Above 100% render scale it switches to DLAA for
  that session automatically. Bloom and depth of field aren't applied to
  the DLSS image above 100% render scale yet.
- Major performance fixes, on by default: up to ~3x FPS at high render
  scale, and the pause menu no longer runs worse than gameplay.
- World/AI audio no longer sounds like it's in a small room.
- Controller menu navigation in Multiplayer.
- Fixed in code but not yet confirmed in live play: the pause-menu Back
  glyph flicker, and the forced anisotropic filtering/shadow/lighting
  toggles (previously a silent no-op on x64).

A one-week development break runs from release day (2026-09-27) to
2026-10-04 while this release settles as the LTS candidate for this line
-- please keep reporting bugs.

IMPORTANT -- render scale above 100%: this release fixed the main cause of
render scale's disproportionate cost, but the safe ceiling is still not one
fixed percentage -- it depends on the mode, the map and your hardware.
Before this fix, 200% was clean in Campaign/Survival but stuttered in
Multiplayer, and Multiplayer hasn't been re-measured since. Test any
increase deliberately in the mode you actually play; if you see stutter,
lower it back toward 100% rather than trusting a number that was safe
somewhere else. Full detail in PATCHNOTES.md.

INSTALL
-------
1. Requires a legitimate Steam copy of Call of Duty: Modern Warfare 3 (2011),
   current (post-2026-09-03) 64-bit version.
2. Copy d3d9.dll (and its companion .pdb, if present) into your MW3 install
   folder -- the same folder as iw5sp.exe.
3. Launch the game normally. No separate injector is needed; the game loads
   this DLL automatically the same way it loads the real d3d9.dll.
4. mw3ncp_config.ini is generated next to the DLL on first launch. See the
   GitHub wiki's Configuration page for every available key.

UNINSTALL
---------
Delete d3d9.dll (and mw3ncp_config.ini / mw3ncp_state.ini / proxy_d3d9*.log
if you want to remove your settings too). The Vulkan/DLSS runtime files the
DLL extracts on launch live in %LOCALAPPDATA%\MW32011NCP\runtime_x64\ and
can be deleted too. The base game files are never modified.

TROUBLESHOOTING
----------------
If anything looks wrong, check proxy_d3d9.log in the same folder -- it
records signature-scan results and hook install/uninstall events.

MORE INFO
---------
Full documentation, the complete patch history, and the source repository:
https://github.com/k8se10/MW32011NCP

Known issues and current status:
https://github.com/k8se10/MW32011NCP/blob/main/re_notes/known_issues_x64.md

LICENSE
-------
Free to use, modify, and fork. The one restriction: neither this project
nor any fork/derivative may ever be sold or charged for. See LICENSE for
the full text.

Not affiliated with, endorsed by, or sponsored by Activision, Infinity
Ward, or any of their affiliates.
