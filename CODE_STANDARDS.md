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
4. ✅ Vanilla keyboard/mouse control is unaffected — the project must be strictly
   additive for players not using a controller
5. ✅ Documented — non-obvious signatures/offsets/hook mechanics explained
   in-repo (`re_notes/iw5sp.md`)
6. ✅ Committed — changes are in the project's own git repo with a clear message

A change that fails any one of these is not done, regardless of how
confident the description of it sounds.

## Documentation Standards

Document every last detail. This project's `re_notes/` directory is the
reference record, not a polished highlight reel — treat it that way:

- **Dead ends get documented as thoroughly as successes.** A hypothesis that
  turned out wrong, a technique that crashed the game, an address that looked
  right but wasn't — all of it goes in `re_notes/`, with *why* it was wrong,
  not just "didn't work." Future work (including a future AI session with no
  memory of this one) depends on this to avoid re-treading the exact same dead
  end blind.
- **Every user-facing change gets a `PATCHNOTES.md` entry in the same pass as
  the change itself**, sorted into Added/Fixed/Changed/Investigated-not-
  resolved/Docs — not a separate cleanup task, not something to batch later.
  This includes corrections to previously-wrong documentation or config-comment
  text, not just code changes.
- **Cite concrete evidence, not conclusions.** A finding is "confirmed via
  disassembly at address `0x...`" or "live-tested, see screenshot/log," not
  "should be correct" or "this seems right." Distinguish confirmed-live, from
  static-analysis-only, from theorized-but-untested — explicitly, every time.
  Don't let confidence bleed across that line in the writing; a reader should
  never have to guess how solid a claim actually is.
- **Cross-reference, don't duplicate.** `re_notes/iw5sp.md` is the full RE
  trail, `re_notes/known_issues.md` is the actively-tracked issue list,
  `re_notes/compatibility_matrix.md` is the per-mission/per-mode live
  playtest status (Campaign by mission, Special Ops by mission, Survival as
  a single overall entry — see that file for why), `PATCHNOTES.md` is the
  curated player-facing changelog, `README.md` is the feature/status
  overview (with a condensed compatibility summary table pointing to the
  full matrix, not a duplicate of it). A given fact belongs in exactly the
  place that owns it, linked from everywhere else that needs it — not
  copy-pasted across several files that will inevitably drift out of sync.
  When a live playtest surfaces a compatibility finding, it goes in
  `compatibility_matrix.md` (status) AND `known_issues.md` (the technical
  bug/RE detail, if any) — not just one or the other.
- Undocumented work is not done, by the same standard as untested work — see
  **Production Readiness Criteria** above.

## Debugging Methodology

Added 2026-08-16 after issue #74's own postmortem (`known_issues.md`) — a "no
glyphs" report took **sixteen days and two investigation rounds**, including two
externally-sourced architectural theories, to trace back to a config flag this
project itself had hardcoded off from day one and was already printing on the
first line of every `proxy_d3d9.log` capture the whole time. Standing rules from
that failure:

- **Before investigating anything external (hardware, OS, other software, memory
  layout, third-party overlays), dump and personally read every gating flag this
  project's own config loader already logs.** If a `[config] loaded ...`-style
  line exists, read the WHOLE line, not just the field your current hypothesis
  predicts. Confirmation bias in what you grep for is as dangerous as a missing
  diagnostic — don't assume a diagnostic doesn't exist just because the specific
  tag you're used to searching for isn't matching.
- **A real fix found along the way is not evidence the remaining reports are
  something more exotic.** It's easy to let "we already found and fixed a real
  bug here" quietly close off re-checking the simplest original explanation for
  everyone still affected. Re-verify the mundane gate again after every fix,
  before escalating the theory.
- **Don't scope the search window to "what changed recently."** A silent,
  permanent, wrong-since-introduction default is invisible to any diff-shaped or
  changelog-shaped search — it only surfaces from a full, unfiltered read of
  CURRENT state, independent of when it was introduced.
- **A reporter saying "I already tried the config fix" is a claim about their
  actions, not a verified fact about the code.** Check what the fix they're
  describing actually does in the source before treating it as having ruled
  anything out — don't let a plausible-sounding user report substitute for
  reading the gating logic itself.
- **Direct, first-party reproduction beats remote diagnosis from partial reports.**
  When remote diagnosis stalls, install fresh yourself and read your own log
  start-to-finish rather than re-grepping reporters' partial logs for the same
  prior hypotheses again.

## Native project code (C/C++)

- **Aspirational goal, not current practice (corrected 2026-08-01 — this
  standard previously claimed as already-true something the codebase has
  never actually done): every hook target should ideally be found via
  byte-pattern/signature scanning at runtime, per binary, because game
  updates and the SP/MP binary split both shift offsets.** The ACTUAL current
  practice, honestly: every real engine-function hook in this codebase is a
  literal hardcoded address, found once via static Ghidra analysis and never
  re-resolved at runtime — see `CONTRIBUTING.md`'s own matching correction
  for the exact wording this project has settled on. A genuine runtime
  scanner would be strictly better and remains a real, open, project-wide
  idea — but until one exists, match the established pattern (static
  analysis, hardcode, document) rather than hardcoding one new hook while
  claiming this standard is met.
- Validate a scanned signature actually resolved (non-null, sane surrounding
  bytes) before installing a hook on it — fail loudly and refuse to hook
  rather than jumping to garbage. (Inapplicable today since no runtime
  scanning exists yet — kept as the standard for whenever it does.)
- All hook callbacks must be safe to call from the game's own thread(s) — no
  blocking calls, no heavy work inline; queue/defer anything expensive.
- **Not currently done (corrected 2026-08-01 — this standard also previously
  claimed as already-true something the codebase has never actually done):
  clean up hooks/hold no dangling trampolines on DLL unload.** `dllmain.cpp`'s
  `DLL_PROCESS_DETACH` currently only unloads fonts and closes the log file —
  no `MH_DisableHook`/`MH_RemoveHook`/`MH_Uninitialize` call exists anywhere
  in the codebase, so every installed hook's trampoline is left dangling on
  unload today. Kept as the standard to hold new work to; a real project-wide
  cleanup pass to actually satisfy it hasn't happened yet.
- Keep XInput polling, hook installation, and gameplay-input translation in
  clearly separate modules — don't let pattern-scan/hook plumbing and
  aim-assist/curve logic tangle together.

## Error Handling & Logging

- Log signature-scan results (found/not found, resolved address) and hook
  install/uninstall events to a file the user can pull after a crash —
  silent failure on a missing signature is not acceptable.
- Wrap injected code paths defensively; a bug in the project must never be
  allowed to corrupt or crash the base game silently without a log trail
  explaining why.

## Input Validation & Security

- Never write secrets, tokens, or account details into project source or
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
- No hardcoded addresses, no OS-level input emulation beyond the two
  documented, narrowly-scoped exceptions (`re_notes/known_issues.md` issues
  #5 and #14) — see `CONTRIBUTING.md` for the full ground rules.
