# Technical Documentation

## Overview

MW3 (2011) ships on PC with **zero working controller input path** — confirmed via the binary's own PE import table: no `xinput*.dll`, no `dinput8.dll`, no `DirectInput8Create`/`GetRawInputData` call anywhere. This isn't a hidden setting to unlock; the code to read a controller simply isn't there. This project supplies it, by hooking the game's own real internal engine functions directly and feeding them from XInput.

## Why Native, Not an Emulator

Every other "controller support" option for games like this works by faking keyboard/mouse input (synthetic key taps, injected mouse deltas) underneath a mapper tool — poll → convert to a key/mouse event → OS input queue → the game's own keyboard/mouse-delta processing. That's a real, measurable translation layer.

This project instead writes straight into the engine's real per-frame input path — the `usercmd_t` movement/button bytes, the raw pitch/yaw angle accumulators, and (where the engine requires it) the real internal button-state (`kbutton_t`) calls the game's own code reads — from inside the game's own process, on the game's own frame tick. No OS-level input event, no intermediate queue, no keyboard/mouse pipeline to pass through. That removes a full layer of translation and buffering, which is the core reason input feel and latency matches native console analog input rather than approximating it through an emulation layer.

**Three narrow, deliberate exceptions** exist where an extensive search found no locatable native trigger for a specific input, and a real keypress is synthesized instead (Survival's ready-up, an AI-squadmate call-in, and the scoreboard/objectives button) — everything else, including all of movement/look/combat, drives real internal engine state directly. See [`re_notes/known_issues.md`](https://github.com/k8se10/MW32011NCP/blob/main/re_notes/known_issues.md) for the specific reasoning behind each.

## Newer Architecture Pieces (v0.3.2+)

- **Native DualSense backend** (`dualsense_input.cpp`) — a raw-HID backend that enumerates and reads a real DualSense's USB/Bluetooth HID reports directly (`SetupDiGetClassDevs`/`CreateFileW`/`ReadFile`), translating them into the same `XINPUT_GAMEPAD_*` values the rest of the codebase already consumes. Bypasses Steam Input entirely, by design. Bluetooth's stick-garbling bug (a real signed-16-bit integer overflow at full-forward deflection) is fixed and confirmed live as of v0.3.4; USB still hasn't been independently confirmed by a separate tester — see [[Known Issues]].
- **Event-driven, one-thread-per-job background architecture** (v0.3.5) — replaced the original single free-running 250Hz poll thread with four separate dedicated threads (controller polling, vibration writes, config-hot-reload, log-flushing), each either woken by an event from its real caller or on its own plain periodic loop. Fixed a recurring 2-5 second freeze root-caused to five independent causes across the old single-thread design. See [[Known Issues]].
- **Locked-source controller polling** (v0.3.3) — the background poll thread determines the active controller/backend once at boot and locks onto it for the session, instead of rescanning every possible device every tick. A real fix for a genuine extended-session performance regression — see [[Known Issues]].
- **Full-screen post-process pipeline** (v0.3.5) — captures the complete, final composed frame and re-draws it through an arbitrary pixel shader, generalizing the existing Options-screen blur's capture/composite technique from a small sub-region to the whole screen. Hosts `InternalRenderScalePercent`, FSR 1.0 RCAS sharpening, and camera-only motion blur, all off by default.
- **Runtime asset capture and a `.menu` file renderer** (`tools/ui_harness/`) — an internal research tool that parses and renders the game's real `.menu` UI definition files outside the running game, used to investigate menu layout/coordinate-transform behavior without needing to be live in-game to inspect it. Not a player-facing feature.
- **Per-frame benchmark tool** (`frame_benchmark.h/cpp`, `[Experimental] FrametimeBenchmarkLogging`) — writes real per-frame timing data (via `QueryPerformanceCounter`) broken down by this project's own hook costs, specifically so a felt stutter can be checked against real evidence instead of guessed. This is how both the v0.3.3 and v0.3.5 stutter fixes were actually found, not guessed.

## Architecture

```
iw5sp.exe (unmodified game logic)
    │  loads d3d9.dll from its own directory first (standard Windows DLL search order)
    ▼
our proxy d3d9.dll                    ← real injection point, ships beside the exe
    │  forwards all real d3d9 exports to the genuine system d3d9.dll
    │  hooks IDirect3D9::CreateDevice to subclass the real device's window
    ▼
XInput poll (linked by us — the game has none) → deadzone + response curve
    ▼
Two per-frame injection points, since they run at different times:
    │  the gameplay-simulation tick (halts while paused)
    │      — movement, look, buttons, ADS, Sprint, Hold Breath, Reload, weapon switch
    │  a WndProc subclass + timer (keeps running even while paused)
    │      — the pause menu's own open/close
    ▼
Real internal engine calls: button-state (down/up) calls for ADS/Reload/Sprint/
Hold Breath; real dispatch tables for weapon switch and D-pad actionslots;
real menu-forwarding calls for D-pad/A menu navigation
```

**Every hook target is found via static analysis (Ghidra, no live process attached) and then hardcoded — this project's own deliberate, permanent policy, not a stopgap.** This was reversed from an earlier "runtime scanner is the goal" stance (2026-08-25): a runtime signature scanner has to search the game's own process memory every time it resolves, which is closer to what anti-cheat heuristics are built to watch for than a fixed address resolved once, offline, ever is. Every address is re-found and re-verified per binary version, and documented with how it was located. See [`re_notes/iw5sp.md`](https://github.com/k8se10/MW32011NCP/blob/main/re_notes/iw5sp.md) for the complete reverse-engineering log: every function found, every dead end ruled out, and why.

## Reverse Engineering Approach

- **Tools**: Ghidra for static analysis/decompilation, a debugger for live verification, and memory-diffing techniques to find dynamically-allocated state that doesn't have a fixed static address.
- **`iw5sp.exe`** (Campaign/Survival) and **`iw5mp.exe`** (Multiplayer) are separately-built binaries — a function or offset found in one is never assumed to carry over to the other. Multiplayer hasn't been started at all.
- **Verify live, always.** A hook isn't considered "done" until it's confirmed working during actual play — "builds clean" and "confirmed working" are treated as two different, separately-tracked states throughout this project's own documentation.

## Where to Go Deeper

- [`re_notes/iw5sp.md`](https://github.com/k8se10/MW32011NCP/blob/main/re_notes/iw5sp.md) — the complete, function-by-function reverse-engineering log.
- [`re_notes/known_issues.md`](https://github.com/k8se10/MW32011NCP/blob/main/re_notes/known_issues.md) — the full research trail behind every open issue, including raw addresses and disassembly notes.
- [[Development Notes]] — the project's own contribution/testing standards.
