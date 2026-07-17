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
- **Hook targets are found via static Ghidra analysis (decompile/disassemble,
  confirm via disassembly, then hardcode the address) — not a runtime
  signature scan.** Read that as the honest current state, not the aspiration:
  a genuine runtime byte-pattern scanner would be strictly better (game
  updates and the `iw5sp.exe`/`iw5mp.exe` binary split both mean offsets
  aren't stable across versions or between the two executables), but every
  single hook in this codebase today — all ~50+ of them — is a literal address
  found once via static analysis, not a scan performed at runtime. Match the
  existing pattern (find it in Ghidra, verify the calling convention via raw
  disassembly, hardcode it, document it in `re_notes/iw5sp.md`) rather than
  introducing a scanner for just your one new hook while everything else stays
  hardcoded — that would make the codebase MORE inconsistent, not less. If
  you want to tackle a real runtime scanner as its own project-wide effort,
  open an issue to discuss it first, since it would touch every existing hook.
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
- **No OS-level input emulation**, with two existing, explicitly scoped
  exceptions: Survival's ready-up (synthesizes an `F5` keypress because the
  real native trigger could not be found after an extensive search — see
  `re_notes/known_issues.md` issue #5) and D-pad Left's AI-squadmate call-in
  (synthesizes a `'4'` keypress for that one slot only, after the same class
  of investigation pointed at a Survival-specific GSC script rather than a
  native trigger — see `re_notes/known_issues.md` issue #14). Don't add
  another synthetic-input shortcut without opening an issue to discuss it
  first; the bar for both exceptions was "every native avenue was actually
  exhausted and documented," not "convenient."

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
and if possible, the mod's log file from the session. Crashes matter a lot
more here than most projects — this mod hooks a live game process, so please
include as much repro detail as you can.
