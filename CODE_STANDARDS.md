# Code Standards

This document is the authoritative statement of the bar every change in this
repository is held to — human-written or AI-assisted, no distinction. It's
referenced from `CONTRIBUTING.md`; read this file in full before opening a PR.

## AI-assisted contributions are permitted — held to the exact same bar

This project has been developed with heavy use of AI coding assistance
(Claude Code). That is explicitly fine. What is **not** fine, from any
contributor, human or AI:

- **No placeholder hooks.** No `// TODO: find real offset later`, no stub
  function committed as if it were the real thing, no "this should work"
  submitted without having actually run it.
- **No half-finished work presented as done.** A feature is either
  production-ready per the criteria below, or it isn't merged/committed as
  complete. Partial implementations belong in a draft PR clearly marked as
  such, not silently blended in with finished work.
- **No unfinished work masquerading as finished.** If a change only handles
  the happy path, or only works for one of two binaries, or only works with
  a controller and silently breaks keyboard/mouse — that is not done, and
  must not be described as done in a commit message, PR description, or code
  comment.

If you're using an AI assistant to help write a change for this repo, hold
its output to this document before committing — an assistant confidently
describing something as "working" or "implemented" is not evidence that it
meets the bar below. Verify live, per **Production-Ready Only** and
**Production Readiness Criteria**.

## Production-Ready Only

No placeholder hooks, no "TODO: find real offset later" committed as done. A
hook is only "done" once it's verified live against the running game.

## Production Readiness Criteria

A feature/change is **production ready** when:

1. ✅ All requirements met — every acceptance criterion satisfied
2. ✅ Verified live against the actual running game (Campaign, Survival, or
   Multiplayer as applicable) — not just "should work"
3. ✅ No crashes introduced — tested through normal play, not just a single
   happy-path pass
4. ✅ Vanilla keyboard/mouse control is unaffected — the mod must be strictly
   additive for players not using a controller
5. ✅ Documented — non-obvious signatures/offsets/hook mechanics explained
   in-repo (`re_notes/iw5sp.md`)
6. ✅ Committed — changes are in the mod's own git repo with a clear message

A change that fails any one of these is not done, regardless of how
confident the description of it sounds.

## Native mod code (C/C++)

- **Never hardcode a raw address.** Every hook target is found via
  byte-pattern/signature scanning at runtime, per binary, because game
  updates and the SP/MP binary split both shift offsets.
- Validate a scanned signature actually resolved (non-null, sane surrounding
  bytes) before installing a hook on it — fail loudly and refuse to hook
  rather than jumping to garbage.
- All hook callbacks must be safe to call from the game's own thread(s) — no
  blocking calls, no heavy work inline; queue/defer anything expensive.
- Clean up hooks/hold no dangling trampolines on DLL unload — a proxy DLL
  that crashes the host game on exit is not acceptable.
- Keep XInput polling, hook installation, and gameplay-input translation in
  clearly separate modules — don't let pattern-scan/hook plumbing and
  aim-assist/curve logic tangle together.

## Error Handling & Logging

- Log signature-scan results (found/not found, resolved address) and hook
  install/uninstall events to a file the user can pull after a crash —
  silent failure on a missing signature is not acceptable.
- Wrap injected code paths defensively; a bug in the mod must never be
  allowed to corrupt or crash the base game silently without a log trail
  explaining why.

## Input Validation & Security

- Never write secrets, tokens, or account details into mod source or
  committed config.
- Treat any data read out of the game's process memory (e.g. for aim-assist
  work) as untrusted/variable between binary versions — validate before
  dereferencing.
- See `SECURITY.md` for what counts as a reportable security issue and how
  to report one.

## Scope discipline

- Only make changes that are explicitly requested or clearly required by the
  task at hand — don't bundle unrelated fixes or refactors into the same
  change.
- No hardcoded addresses, no OS-level input emulation beyond the one
  documented, narrowly-scoped exception (`re_notes/known_issues.md` issue
  #5) — see `CONTRIBUTING.md` for the full ground rules.
