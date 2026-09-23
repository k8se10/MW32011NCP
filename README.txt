MW32011NCP -- Native Community Patches for MW3 (2011)
v0.0.2-x64

WHAT THIS IS
------------
Native controller support (movement/look/every button hooked directly into
the game's own real engine functions, not a keyboard/mouse emulation
mapper), a visual/performance enhancement suite (render scale, motion blur,
FSR sharpening, frame pacing), and four fixes for real, exploitable
vulnerabilities in MW3's own base-game netcode -- all built in and enabled
by default.

Survival's own controller support is Gameplay Complete: every core control
is live-confirmed working, including Predator Missile guidance. Campaign
ships best-effort (never a release gate, same as the prior 32-bit line).
Multiplayer has no controller/menu-navigation support yet, but internal
render scale -- the visual-enhancement suite's first MP-enabled feature --
now works there.

IMPORTANT -- render scale above 100%: the safe ceiling is NOT one fixed
percentage. 200% is completely clean in Campaign/Survival but causes
constant stutter in Multiplayer -- the real safe headroom depends on how
demanding the specific mode/map/moment is, and this mod cannot detect that
automatically. Test any increase deliberately in the mode you actually
play; if you see stutter, lower it back toward 100% rather than trusting a
number that was safe somewhere else. Full detail in PATCHNOTES.md.

Motion blur now reacts to keyboard/mouse look, not just controller.

One known cosmetic bug ships with this release: the pause-menu Back glyph
flickers. Back itself still works correctly. The forced anisotropic
filtering/shadow/lighting-quality toggles are currently non-functional on
x64 (a silent no-op, not a crash).

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
if you want to remove your settings too). The base game files are never
modified.

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
