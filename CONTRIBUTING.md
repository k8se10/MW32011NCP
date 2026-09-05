# Contributing to MW32011NCP

Thanks for taking an interest in this project. It's a from-scratch, native
reverse-engineering effort to bring real controller support and a growing
suite of visual/QoL enhancements to Call of Duty: Modern Warfare 3 (2011, IW5
engine) — not a keyboard/mouse-emulation mapper. Before opening a PR, please
read this file in full.

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
  binary). Every feature here is implemented by hooking real engine functions,
  or in a couple of narrowly-scoped cases, our own additive timer/state layer
  on top of them.
- **Hook targets are resolved via runtime signature scanning**: a
  wildcarded byte-pattern scan against the game's own main module
  (`signature_scan.h`/`.cpp`), resolved once at process startup and cached
  for the rest of the session — never a repeated or continuous re-scan loop.
  To add a new engine-function hook: find the target in Ghidra against the
  current x64 binary, build a wildcarded byte signature around it, resolve it
  through `signature_scan`, validate the result (non-null, sane surrounding
  bytes) before installing anything, and document the trail in
  `re_notes/known_issues_x64.md`. **Exception**: the three D3D9 hooks
  (`IDirect3DDevice9::EndScene`/`Reset` in `overlay_hud.cpp`,
  `IDirect3D9::CreateDevice` in `d3d9_hook.cpp`) resolve their real target
  live from the actual COM vtable at runtime instead — the standard, correct
  way to hook a COM interface method.
- **`iw5sp.exe` (Campaign/Survival) and `iw5mp.exe` (Multiplayer) are
  separate efforts.** Don't assume a function or offset found in one binary
  carries over to the other — each needs its own independently-found
  signature. Multiplayer work has not started; there's an open, unresolved
  question about anti-cheat exposure from code injection on `iw5mp.exe` that
  needs discussion before that work begins — raise an issue first rather
  than opening a PR for MP injection.
- **Verify live before calling anything done.** A hook that "should work" but
  hasn't been run against the actual game isn't done. PRs touching movement,
  look/aim, buttons, sprint, or menu navigation should describe what was
  actually tested in-game (which mode, what you did, what you observed) —
  build-verified-only is a real, valid, honestly-labeled interim state, but
  it is not the same claim as live-confirmed.
- **Stay strictly additive.** Vanilla keyboard/mouse play must be unaffected
  by any change. If you're not sure whether a change could regress
  keyboard/mouse play, test that too before opening the PR.
- **No OS-level input emulation** except a small number of explicitly scoped,
  documented exceptions where an exhaustive search found no native trigger to
  hook instead (see `re_notes/known_issues_x64.md` and, for exceptions
  carried over from before the x64 recompile, `legacy-x86-docs/CONTRIBUTING.md`
  for the original investigation trail). Don't add another synthetic-input
  shortcut without opening an issue first — the bar is "every native avenue
  was actually exhausted and documented," not "convenient."

## Code style

- Keep XInput polling, hook installation/signature-scanning plumbing, and
  gameplay-input translation (curves, stamina, etc.) in clearly separate
  modules — don't let scan/hook plumbing and gameplay logic tangle together.
- Hook callbacks must be safe to call from the game's own thread(s) — no
  blocking calls, no heavy work inline.
- Clean up hooks and hold no dangling trampolines on DLL unload.
- Log signature-scan results (found/not found, resolved address) and hook
  install/uninstall events — silent failure on a missing signature is not
  acceptable.
- When you find a non-obvious function's real purpose (via decompile,
  memdiff, or live testing), document it in `re_notes/known_issues_x64.md`
  with enough detail for someone else to independently verify it, not just
  the conclusion.

## Building

- Windows only. Requires MSVC (Visual Studio Build Tools or Community, with
  the Windows 10 SDK) and MSBuild.
- Both target binaries (`iw5sp.exe`, `iw5mp.exe`) are 64-bit — build the
  proxy DLL as **x64**.
- Build via the MSBuild project files under `proxy_d3d9/` from a Developer
  Command Prompt (or after running `vcvarsall.bat`).
- For live debugging, use a 64-bit debugger (e.g. `x64dbg`).
- Do a final rebuild from your real checkout (not an isolated worktree copy)
  before asking anyone to live-test a change — an isolated build proves the
  code compiles, it doesn't confirm it was actually deployed to the running
  game.

## Submitting a PR

1. Open an issue first for anything that isn't a small, obvious fix —
   especially a new hook target, anything touching `iw5mp.exe`, or a new
   input-emulation exception — so the approach can be discussed before you
   sink time into it.
2. Meets every criterion in [`CODE_STANDARDS.md`](CODE_STANDARDS.md) —
   production-ready, live-verified (or honestly labeled as build-verified
   only), no placeholder/half-finished work. This is checked, not assumed,
   regardless of how the code was written.
3. Commit messages follow `[type]: [description]` (`feat:`, `fix:`, `docs:`,
   `chore:`, `refactor:`, `test:`).
4. Describe your testing in the PR description: which binary/mode, what you
   did, what you observed. Include re-tested vanilla keyboard/mouse play if
   your change touches a shared code path.
5. If you use a new third-party library, note its license in the PR — it'll
   need a credit added to `README.md`'s Credits section before merging.
6. By submitting a PR, you agree your contribution is licensed under this
   project's `LICENSE`.

## Reporting bugs

Open an issue with: which binary (`iw5sp.exe`/`iw5mp.exe`), which mode
(Campaign/Survival/Multiplayer), what you expected, what happened instead,
and if possible, the project's log file from the session. Crashes matter a
lot more here than most projects — this project hooks a live game process,
so please include as much repro detail as you can.

Before reporting a Campaign or Special Ops mission-specific issue, check
`re_notes/compatibility_matrix.md` first — it tracks per-mission live
playtest status and may already have your finding logged.
