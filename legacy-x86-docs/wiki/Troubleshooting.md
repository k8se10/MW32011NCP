# Troubleshooting

## First Step, Always

Check **`proxy_d3d9.log`** in your MW3 install folder (created next to `d3d9.dll` once the project's DLL is running). It logs hook installation results, config values loaded at startup, and diagnostic output — it's the single most useful thing to check yourself, and the most useful thing to attach to any bug report.

## Common Issues

### Controller input isn't working at all

- Confirm `d3d9.dll` is actually present in the same folder as `iw5sp.exe`, not a subfolder.
- Check whether `proxy_d3d9.log` exists at all. If it doesn't, the DLL likely isn't loading — Steam's **file-integrity verification** or an automatic game update can silently remove a dropped-in `d3d9.dll` (see [[Installation Guide]]) since it isn't part of the base game's manifest. Reinstall by copying it back in.
- Confirm you're playing **Campaign or Survival** (`iw5sp.exe`). Multiplayer (`iw5mp.exe`) is a separate binary this project has not started work on at all — controller input will not work there.

### A specific button/feature doesn't work

Check [[Known Issues]] first — a real, honest breakdown of exactly what's confirmed working, partial, or not implemented exists there and in the project's own [README "Status at a glance"](https://github.com/k8se10/MW32011NCP#status-at-a-glance) table. If something's listed as 🟡 or ⬜, that's expected behavior for this alpha, not a bug you need to report.

### Keyboard/mouse feels affected after installing

Keyboard/mouse is meant to stay strictly additive and unaffected — if you notice a real regression (a real, reproducible case did happen once during development and was fixed), please report it with your `proxy_d3d9.log` attached; this is treated seriously since it's not supposed to happen.

### The game feels laggy or stutters during a long session

**Make sure you're on v0.3.5 or later.** Two separate stuttering investigations have shipped real fixes: v0.3.3 fixed an unbounded log file, a controller-poll thread that never stopped over-scanning, and a diagnostic log with broken dedup. v0.3.5 went much further — a recurring freeze every 2-5 seconds was root-caused to five independent causes (a fixed-rate poll thread, synchronous vibration writes, an uninstrumented config-hot-reload file check, a synchronous log flush, and uncached per-frame text measurement) and fixed by moving each onto its own dedicated background thread. If you're on an older build, updating is the fix, not a config change. One narrower residual dip remains open above 1080p resolution specifically (confirmed fine at 1080p and 4:3) and is believed GPU-side, not something this project's own hooks are causing — see [[Known Issues]].

### The game becomes unstable, freezes, or crashes hours after playing with a visual-enhancement setting enabled

**If you ever enabled `ForceD3D9On12`, disable it and do not re-enable it.** That setting was **removed entirely in v0.3.5** after real-world evidence showed enabling it even once can leave the system in a bad driver-level state that outlives turning it back off, causing crashes in later sessions with the setting already disabled. If you're on an older build that still has it, update and leave it off. This is unrelated to the other, safe visual-enhancement toggles (`InternalRenderScalePercent`, `FsrSharpenEnabled`, `MotionBlurEnabled`, `ForceAnisotropicFiltering`, `ForceHighQualityShadows`/`Lighting`) — running all of those together is genuinely GPU-intensive but not unsafe, see [[Known Issues]].

### The game crashes or gets stuck

- Grab `proxy_d3d9.log` immediately — for a crash, the log often shows hooks installing successfully followed by an abrupt stop, which is a real, useful signature for diagnosing the cause.
- Note what you were doing right before it happened (which mission/mode, which button, ADS or not, etc.) — as specific as possible.
- If you enabled a PREVIEW/WIP or EXPERIMENTAL feature (the custom Options screen, the v0.3.5 visual-enhancement suite — render scale, FSR sharpening, motion blur) and hit a crash, mention that explicitly — those are the least-tested parts of the current build. See [[Known Issues]].

### Reporting a Bug

The best place for player-facing bug reports and feedback is the project's **Nexus Mods forum** (Bug Reports topic) — that's the channel actively watched for community reports. You can also open an issue on the [GitHub repository](https://github.com/k8se10/MW32011NCP/issues) if you prefer. Either way, please include:
- What you were doing, what you expected, and what happened instead.
- Your `proxy_d3d9.log` file, especially if the game crashed or got stuck.
- Which mission/mode (Campaign mission name, or Survival) if relevant.
- Your controller type and connection (Xbox/XInput, or DualSense USB/Bluetooth) — DualSense USB reports in particular are still needed, since USB has never been independently confirmed by a separate tester, see [[Known Issues]].

See [`CONTRIBUTING.md`](https://github.com/k8se10/MW32011NCP/blob/main/CONTRIBUTING.md) on GitHub if you'd like to help fix something yourself.

## See Also

- [[Known Issues]]
- [[FAQs]]
- [[Compatibility]]
