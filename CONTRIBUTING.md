# Contributing to MW3NCP

Thanks for taking an interest in this project. It's a from-scratch, native
reverse-engineering effort to bring real controller support to Call of Duty:
Modern Warfare 3 (2011, IW5 engine) — not a keyboard/mouse-emulation mapper.
Before opening a PR, please read this file in full.

> **Read [`CODE_STANDARDS.md`](CODE_STANDARDS.md) before writing any code.**
> It is the authoritative statement of the bar every change is held to —
> production-ready only, no placeholder hooks, no half-finished work
> presented as done. This applies identically whether the code was written
> by hand or with AI assistance (this project itself is developed with heavy
> AI-assistant use, which is explicitly fine — confidently-described AI
> output that hasn't actually been verified live is not).

## Ground rules

- **This is native RE, not config tweaking.** The base game ships with zero
  working controller input path (confirmed: no `xinput*.dll`/`dinput8.dll`
  import, no `DirectInput8Create`/`GetRawInputData` call anywhere in either
  binary). Every feature here is implemented by hooking real engine functions
  found via signature scanning, or in a couple of narrowly-scoped cases, our
  own additive timer/state layer on top of them. See `re_notes/iw5sp.md` for
  the full trail of what's been found so far.
- **Hook targets are found via runtime signature scanning (a byte-pattern
  scan resolved ONCE at process startup and cached for the session, never a
  repeated/continuous re-scan loop) — this is the CURRENT policy, and has
  been since 2026-09-03.** This project's addressing policy has genuinely
  reversed twice, and the full record matters for understanding why:
  1. **Originally (through 2026-08-24), signature scanning was the honest
     current-state approach**, not a chosen ideal.
  2. **REVERSED 2026-08-25** to hardcoded addresses (found once via static
     Ghidra analysis, no live process attached, then called at a known fixed
     location) on VAC-safety grounds: a runtime scanner has to walk arbitrary
     regions of the game's own process memory searching for a byte sequence
     every time it resolves — exactly the class of behavior VAC's own
     signature-based heuristics are built to notice — while a hardcoded
     address has no such runtime search surface at all.
  3. **REVERSED AGAIN 2026-09-03**, back to signature scanning, superseding
     #2 — not because the VAC-risk reasoning above was wrong (it wasn't, and
     still isn't resolved), but because a genuine MW3 (2011) binary update
     recompiled both `iw5sp.exe`/`iw5mp.exe` from x86 to x64 in one step,
     invalidating every one of this project's ~100+ hardcoded addresses at
     once — precisely the failure mode a hardcode-only policy cannot survive,
     and a real-world cost the 2026-08-25 policy never weighed against its
     own VAC reasoning. Signature scanning is the only approach that
     survives a binary update at all; the VAC-risk tradeoff is accepted, not
     eliminated, as the reasonable cost of that resilience.
  
  **What this means in practice for the CURRENT (`-x64`) line**: every
  engine-function hook target is resolved via `signature_scan.h`/`.cpp`
  (parses `"?? "`-wildcarded hex patterns, scans the game's own main module
  via a real PE-header walk, fails loudly and refuses to hook on a zero or
  ambiguous match) — once per process start, cached for the rest of the
  session. **Do not hardcode a fixed address for a new `-x64` engine-function
  hook** — that's the rejected, superseded approach, and it's specifically
  what a real binary update already proved fragile. Match the existing
  pattern instead (find the target in Ghidra against the x64 binary, build a
  wildcarded byte signature around it, resolve via `signature_scan`, validate
  before hooking, document it in `re_notes/known_issues_x64.md`/
  `re_notes/x64_migration/`). The frozen, unsupported `-x86` line's own hook
  targets remain literal hardcoded addresses, found via static Ghidra
  analysis against `iw5sp.exe`/`iw5mp.exe`'s x86 builds — that's accurate
  history for that now-discontinued line, not a live policy to extend.
  **Exception, unchanged by either reversal**: the three D3D9 hooks
  (`IDirect3DDevice9::EndScene`/`Reset` in `overlay_hud.cpp`,
  `IDirect3D9::CreateDevice` in `d3d9_hook.cpp`) resolve their real target
  address live from the actual COM vtable at runtime — the standard, correct
  way to hook a COM interface method regardless of this project's own
  engine-function convention, and unrelated to either the scan-vs-hardcode
  question or the VAC-risk tradeoff either way.
- **`iw5sp.exe` (Campaign/Survival) and `iw5mp.exe` (Multiplayer) are
  separate efforts.** Don't assume a function or offset found in one binary
  carries over to the other — each needs its own independently-found
  signatures. Multiplayer work has not started yet; there's an open,
  unresolved question about anti-cheat exposure from code injection on
  `iw5mp.exe` that needs discussion before that work begins — please raise an
  issue first rather than opening a PR for MP injection.
- **Verify live before calling anything done.** A hook that "should work" but
  hasn't been run against the actual game isn't done. PRs touching
  movement, look/aim, sprint, menu navigation, or any other gameplay-facing
  behavior should describe what was actually tested in-game (which mode:
  Campaign/Survival, what you did, what you observed), not just that it
  compiles.
- **Stay strictly additive.** Vanilla keyboard/mouse play must be unaffected
  by any change. If you're not sure whether a change could regress
  keyboard/mouse play, test that too before opening the PR.
- **No OS-level input emulation**, with four existing, explicitly scoped
  exceptions (corrected 2026-08-01 — a fourth was added and this list wasn't
  updated to match): Survival's ready-up (synthesizes an `F5` keypress because
  the real native trigger could not be found after an extensive search — see
  `re_notes/known_issues.md` issue #5), D-pad Left's AI-squadmate call-in
  (synthesizes a `'4'` keypress for that one slot only, after the same class
  of investigation pointed at a Survival-specific GSC script rather than a
  native trigger — see `re_notes/known_issues.md` issue #14), Back's real
  `+scores` scoreboard (synthesizes a `TAB` keypress, since `+scores` turned
  out not to be a native kbutton at all — see `re_notes/known_issues.md`
  issue #28), and Y opening the Friends list from a menu (synthesizes an `F`
  keypress, since Friends is a real keyboard bind the game's key-event
  handler listens for directly, not one of the menu system's own generic
  navigation keycodes — see `re_notes/known_issues.md` issue #50). Don't add
  another synthetic-input shortcut without opening an issue to discuss it
  first; the bar for all four exceptions was "every native avenue was
  actually exhausted and documented," not "convenient."

## Code style

- Keep XInput polling, hook installation/signature-scanning plumbing, and
  gameplay-input translation (curves, aim assist, stamina, etc.) in clearly
  separate modules — don't let pattern-scan/hook code and gameplay logic
  tangle together.
- Hook callbacks must be safe to call from the game's own thread(s) — no
  blocking calls, no heavy work inline.
- Clean up hooks and hold no dangling trampolines on DLL unload.
- Log signature-scan results (found/not found, resolved address) and hook
  install/uninstall events — silent failure on a missing signature is not
  acceptable.
- When you find a non-obvious function's real purpose (via decompile,
  memdiff, or live testing), document it in `re_notes/iw5sp.md` with enough
  detail for someone else to independently verify it, not just the
  conclusion.

## Building

- Windows only. Requires MSVC (Visual Studio Build Tools or Community, with
  the Windows 10 SDK) and MSBuild.
- Both target binaries (`iw5sp.exe`, `iw5mp.exe`) are 32-bit (x86) — build
  the proxy DLL as Win32, not x64.
- Build via the MSBuild project files under `proxy_d3d9/` from a Developer
  Command Prompt (or after running `vcvarsall.bat`).
- For live debugging, use a 32-bit debugger (e.g. `x32dbg`, not `x64dbg`).
- **`proxy_d3d9.vcxproj`'s `OutDir` is `..\..\`, relative to `proxy_d3d9/`** —
  building from the real checkout (this repo, sitting inside the game install
  root) resolves that straight to the game root and deploys `d3d9.dll` where
  the game actually loads it. Building from a **git worktree** (e.g. an
  isolated agent/fork checkout under `.claude/worktrees/agent-*/`) resolves
  the same relative path to the *worktree's own* two-levels-up directory
  instead — for this project's worktree layout that lands at the shared
  `.claude/worktrees/` folder itself (not even a per-worktree path, so
  concurrent worktree builds can clobber each other's output there). Confirmed
  2026-07-22: a fork's clean rebuild (0 warnings/errors) left the real deployed
  DLL untouched, so a live playtest afterward was silently still running the
  *previous* build — looked exactly like "the fix didn't work" until the
  timestamp mismatch was caught. **A worktree build only proves the change
  compiles — it never deploys.** Always do one final rebuild from the real
  checkout before asking for a live playtest of anything a worktree/fork
  built.

## Submitting a PR

1. Open an issue first for anything that isn't a small, obvious fix —
   especially new hook targets, anything touching `iw5mp.exe`, or new
   input-emulation exceptions — so the approach can be discussed before you
   sink time into it.
2. Meets every criterion in [`CODE_STANDARDS.md`](CODE_STANDARDS.md) —
   production-ready, live-verified, no placeholder/half-finished work. This
   is checked, not assumed, regardless of how the code was written.
3. Commit messages follow `[type]: [description]` (`feat:`, `fix:`, `docs:`,
   `chore:`, `refactor:`, `test:`).
4. Describe your live-testing in the PR description: which binary/mode, what
   you did, what you observed. Include re-tested vanilla keyboard/mouse play
   if your change touches a shared code path.
5. If you use a new third-party library, note its license in the PR — it'll
   need a credit added to `README.md`'s Credits section before merging (see
   `LICENSE`'s "Third-party components" section for the existing pattern).
6. By submitting a PR, you agree your contribution is licensed under this
   project's `LICENSE`.

## Reporting bugs

Open an issue with: which binary (`iw5sp.exe`/`iw5mp.exe`), which mode
(Campaign/Survival/Multiplayer), what you expected, what happened instead,
and if possible, the project's log file from the session. Crashes matter a lot
more here than most projects — this project hooks a live game process, so please
include as much repro detail as you can.

**Before reporting a Campaign or Special Ops mission-specific issue**, check
`re_notes/compatibility_matrix.md` first — it tracks per-mission live
playtest status and may already have your finding logged (or explain why a
given moment needs keyboard/mouse fallback). If you're playtesting a
mission/mode that shows as "not yet tested" there, reports are especially
useful — that file is actively maintained as testing continues, and PRs
that add or correct a compatibility entry (with the same precision as a bug
report: exact mission, exact moment, what worked vs. didn't) are welcome
alongside code contributions.
