# Troubleshooting

## First Step, Always

Check **`proxy_d3d9.log`** in your MW3 install folder (created next to `d3d9.dll` once the project's DLL is running). It logs hook installation results, config values loaded at startup, and diagnostic output — it's the single most useful thing to check yourself, and the most useful thing to attach to any bug report.

## Common Issues

### Controller input isn't working at all

- Confirm `d3d9.dll` is actually present in the same folder as `iw5sp.exe`, not a subfolder.
- Check whether `proxy_d3d9.log` exists at all. If it doesn't, the DLL likely isn't loading — Steam's **file-integrity verification** or an automatic game update can silently remove a dropped-in `d3d9.dll` (see [[Installation Guide]]) since it isn't part of the base game's manifest. Reinstall by copying it back in.
- Confirm you're playing **Campaign or Survival** (`iw5sp.exe`). Multiplayer (`iw5mp.exe`) is a separate binary this project has not started work on at all — controller input will not work there.
- Confirm your MW3 install is on the current, post-recompile version — a build of this mod for the current line cannot load into anything else.

### A specific button/feature doesn't work

Check [[Known Issues]] first — a real, honest breakdown of exactly what's confirmed working, build-verified-but-unconfirmed, or not yet implemented on the current line. If something's listed there as not yet done, that's expected for this alpha, not a bug you need to report.

### Keyboard/mouse feels affected after installing

Keyboard/mouse is meant to stay strictly additive and unaffected — if you notice a real regression, please report it with your `proxy_d3d9.log` attached; this is treated seriously since it's not supposed to happen.

### The game crashes or gets stuck

- Grab `proxy_d3d9.log` immediately — for a crash, the log often shows hooks installing successfully followed by an abrupt stop, which is a real, useful signature for diagnosing the cause.
- Note what you were doing right before it happened (which mission/mode, which button, ADS or not, etc.) — as specific as possible.
- If you enabled a feature explicitly marked build-verified-only in [[Known Issues]] and hit a crash, mention that explicitly — those are the least-tested parts of the current build.

### Reporting a Bug

The best place for player-facing bug reports and feedback is the project's **Nexus Mods forum** (Bug Reports topic) — that's the channel actively watched for community reports. You can also open an issue on the [GitHub repository](https://github.com/k8se10/MW32011NCP/issues) if you prefer. Either way, please include:
- What you were doing, what you expected, and what happened instead.
- Your `proxy_d3d9.log` file, especially if the game crashed or got stuck.
- Which mission/mode (Campaign mission name, or Survival) if relevant.
- Your controller type and connection.

See [`CONTRIBUTING.md`](https://github.com/k8se10/MW32011NCP/blob/main/CONTRIBUTING.md) on GitHub if you'd like to help fix something yourself.

## See Also

- [[Known Issues]]
- [[FAQs]]
- [[Compatibility]]
