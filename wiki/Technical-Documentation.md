# Technical Documentation

## Overview

MW3 (2011) ships on PC with **zero working controller input path** — confirmed via the binary's own PE import table: no `xinput*.dll`, no `dinput8.dll`, no `DirectInput8Create`/`GetRawInputData` call anywhere. This isn't a hidden setting to unlock; the code to read a controller simply isn't there. This project supplies it, by hooking the game's own real internal engine functions directly and feeding them from XInput.

## Why Native, Not an Emulator

Every other "controller support" option for games like this works by faking keyboard/mouse input (synthetic key taps, injected mouse deltas) underneath a mapper tool — poll → convert to a key/mouse event → OS input queue → the game's own keyboard/mouse-delta processing. That's a real, measurable translation layer.

This project instead writes straight into the engine's real per-frame input path — the `usercmd_t` movement/button bytes, the raw pitch/yaw angle accumulators, and (where the engine requires it) the real internal button-state calls the game's own code reads — from inside the game's own process, on the game's own frame tick. No OS-level input event, no intermediate queue, no keyboard/mouse pipeline to pass through.

**A small number of narrow, deliberate exceptions** exist where an extensive search found no locatable native trigger for a specific input, and a real keypress is synthesized instead — everything else, including all of movement/look/combat, drives real internal engine state directly. See [`re_notes/known_issues_x64.md`](https://github.com/k8se10/MW32011NCP/blob/main/re_notes/known_issues_x64.md) for the specific reasoning behind each.

## Architecture

```
iw5sp.exe (unmodified game logic, x64)
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
    │      — movement, look, buttons, ADS, Sprint, Reload, weapon switch, stance
    │  a WndProc subclass + timer (keeps running even while paused)
    │      — the pause menu's own open/close
    ▼
Real internal engine calls: button-state (down/up) calls for Fire/ADS/Reload/
Sprint; real case dispatch for stance/D-pad actionslots/weapon switch
```

**Every hook target is resolved via runtime signature scanning** — a wildcarded byte-pattern scan against the game's own main module, resolved once at process startup and cached for the session. This is the current policy, adopted after the game's own 2026-09-03 recompile from 32-bit to 64-bit invalidated every hardcoded address the project had previously found — a hardcode-only approach cannot survive a binary update; a resolve-once, cache-for-the-session scanner does. See [`re_notes/known_issues_x64.md`](https://github.com/k8se10/MW32011NCP/blob/main/re_notes/known_issues_x64.md) for the complete reverse-engineering log: every function found, every dead end ruled out, and why.

## Architecture pieces not yet ported to this line

These existed on the prior 32-bit line and are real, working precedent for how the equivalent x64 work will likely be structured — but none of them are active on the current binaries yet:

- **Native DualSense backend** (`dualsense_input.cpp`) — a raw-HID backend bypassing Steam Input entirely.
- **Full-screen post-process pipeline** — hosted internal render scaling, FSR 1.0 RCAS sharpening, and camera-only motion blur.
- **Controller-glyph icon rendering and the custom cursor** — depends on menu-focus/item-position tracking that hardcodes 32-bit-only pointer/struct assumptions; needs its own x64 struct-layout RE pass.

The event-driven, one-thread-per-job background architecture (controller polling, vibration writes, config-hot-reload, log-flushing each on their own dedicated thread) lives in shared, cross-platform code and is already active on this line.

## Reverse Engineering Approach

- **Tools**: Ghidra for static analysis/decompilation, a debugger for live verification, and raw byte-pattern scanning for building the signatures the shipped mod resolves at runtime.
- **`iw5sp.exe`** (Campaign/Survival) and **`iw5mp.exe`** (Multiplayer) are separately-built binaries — a function or signature found in one is never assumed to carry over to the other. Multiplayer hasn't been started at all.
- **Verify live, always.** A hook isn't considered "done" until it's confirmed working during actual play — "build-verified" and "confirmed working" are treated as two different, separately-tracked states throughout this project's own documentation.

## Where to Go Deeper

- [`re_notes/known_issues_x64.md`](https://github.com/k8se10/MW32011NCP/blob/main/re_notes/known_issues_x64.md) — the full research trail behind every open issue, including raw signatures and disassembly notes.
- [[Development Notes]] — the project's own contribution/testing standards.
