# Contributing to MW3NSP

Thanks for taking an interest in this project. It's a defensive security patch for
Call of Duty: Modern Warfare 3 (2011, IW5 engine) — finding and fixing real,
exploitable netcode vulnerabilities, primarily to protect players joining
public/untrusted servers. Before opening a PR, please read this file in full.

> **Read [`CODE_STANDARDS.md`](CODE_STANDARDS.md) before writing any code.** It is
> the authoritative statement of the bar every change is held to — a fix is only
> "done" once the ORIGINAL vulnerability has actually been reproduced and the fix
> confirmed to stop it, not just "the code looks right." This applies identically
> whether the code was written by hand or with AI assistance.

> **Read [`SECURITY.md`](SECURITY.md) before reporting or discussing ANY new
> vulnerability**, in this game or elsewhere. Do not open a public GitHub issue for
> a netcode vulnerability — email the address in `SECURITY.md` instead.

## Ground rules

- **This is a defensive project, not an offensive one.** Vulnerabilities get
  documented precisely enough to verify a fix works — not turned into
  ready-to-run exploit tooling. If you're not sure whether something you want to
  add crosses that line, ask first (see `SECURITY.md`).
- **`iw5sp.exe` (Campaign/Survival/Spec-Ops) and `iw5mp.exe` (Multiplayer) are
  separate binaries.** Don't assume a vulnerable function or its fix found in one
  carries over to the other — each needs independent confirmation. Both are in
  scope for this project (the co-op networking path in `iw5sp.exe` shares the same
  vulnerability surface as MP, not just a Campaign-only concern).
- **Verify a fix by actually reproducing the original bug first.** A "fix" for a
  vulnerability you never triggered yourself isn't verified — it's a guess. PRs
  should describe exactly how the original bug was reproduced, and how the fix was
  confirmed to stop it, not just that the patch compiles.
- **Stay strictly additive to normal play.** Legitimate multiplayer/co-op netcode
  traffic must keep working correctly after any fix — a patch that "closes" a
  vulnerability by breaking real gameplay traffic isn't acceptable.
- **Follow the phased plan** (see `CLAUDE.md` at the game install root, or this
  project's own `README.md` for the summary) — Phase 1 (patch the specific known
  bugs) before Phase 2 (general hardening) before Phase 3 (GSC VM research). Open
  an issue first if you want to work out of order.
- **Cite real sources.** If a fix is based on public vulnerability research (a CVE,
  a GitHub PoC repo, a security advisory), cite it in `re_notes/` the same way
  `re_notes/vulnerability_research.md` already does — don't restate a finding
  without attribution.

## Code style

- Same general discipline as the sibling `MW32011NCP` project: keep hook
  installation/signature-scanning plumbing separate from the actual
  validation/fix logic.
- Hook callbacks must be safe to call from the game's own thread(s) — no blocking
  calls, no heavy work inline.
- Clean up hooks and hold no dangling trampolines on DLL unload.
- Log what the patch found and did — silent failure is not acceptable, especially
  for a security-relevant patch.
- When you find/confirm a real vulnerability or its fix, document it in
  `re_notes/` with enough detail for someone else to independently verify — not
  just the conclusion. See `CODE_STANDARDS.md`'s "Documentation Standards"
  section for the full bar.

## Building

- Windows only, same target binaries as the sibling project. Requires MSVC
  (Visual Studio Build Tools or Community, with the Windows 10 SDK) and MSBuild.
- Both target binaries are 64-bit — build as x64. (MW3 received its
  first-ever binary update in 2026-09-03, recompiling from 32-bit; the
  32-bit line can no longer run at all, so there is no Win32 target worth
  building.)
- For live debugging, use a 64-bit debugger (e.g. `x64dbg`).
- Hook targets are resolved via runtime signature scanning
  (`proxy_d3d9/src/signature_scan.h`/`.cpp`), a wildcarded byte-pattern scan
  against the game's own main module, resolved once at process startup and
  cached for the session — same policy and reasoning as the sibling
  `MW32011NCP` project. Do not hardcode a fixed address for a new hook
  target.

## Submitting a PR

1. **For anything involving a NEW vulnerability finding**: report it per
   `SECURITY.md` first, privately — do not describe a new, unpatched
   vulnerability in a public PR or issue before a fix is ready.
2. Open an issue first for anything that isn't a small, obvious fix to an
   already-documented vulnerability — especially anything touching `iw5mp.exe` or
   introducing a new hook target.
3. Meets every criterion in [`CODE_STANDARDS.md`](CODE_STANDARDS.md) —
   production-ready, live-verified against a real reproduction of the original
   bug, no placeholder/half-finished work.
4. Commit messages follow `[type]: [description]` (`feat:`, `fix:`, `docs:`,
   `chore:`, `refactor:`, `test:`) — same convention as the sibling project.
5. Describe your verification in the PR description: which binary, how you
   reproduced the original bug, how you confirmed the fix stops it, and that
   normal play still works.
6. By submitting a PR, you agree your contribution is licensed under this
   project's `LICENSE`.

## Reporting bugs (non-security)

For ordinary bugs in this project's own patch code (not a new base-game
vulnerability — see `SECURITY.md` for that), open an issue with: which binary,
what you expected, what happened instead, and a log file from the session if
available.
