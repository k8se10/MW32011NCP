# Code Standards

This document is the authoritative statement of the bar every change in this
repository is held to — human-written or AI-assisted, no distinction. It's
referenced from `CONTRIBUTING.md`; read this file in full before opening a PR.

## AI-assisted contributions are permitted — held to the exact same bar

This project has been developed with heavy use of AI coding assistance. That
is explicitly fine. What is **not** fine, from any contributor, human or AI:

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
meets the bar below. Verify live, per **Production Readiness Criteria**.

## Production Readiness Criteria

A feature/change is **production ready** when:

1. ✅ All requirements met — every acceptance criterion satisfied
2. ✅ Verified live against the actual running game (Campaign, Survival, or
   Multiplayer as applicable) — not just "should work"
3. ✅ No crashes introduced — tested through normal play, not just a single
   happy-path pass
4. ✅ Vanilla keyboard/mouse control is unaffected — the project must be
   strictly additive for players not using a controller
5. ✅ Documented — non-obvious signatures/offsets/hook mechanics explained
   in-repo (`re_notes/known_issues_x64.md`)
6. ✅ Committed — changes are in the project's own git repo with a clear
   message

A change that fails any one of these is not done, regardless of how
confident the description of it sounds. A change that is build-verified but
not yet live-tested is a real, valid, honestly-labeled interim state — it is
simply not the same claim as "done."

## Documentation Standards

Document every last detail. `re_notes/` is the reference record, not a
polished highlight reel — treat it that way:

- **Dead ends get documented as thoroughly as successes.** A hypothesis that
  turned out wrong, a technique that crashed the game, an address that
  looked right but wasn't — all of it goes in `re_notes/`, with *why* it was
  wrong, not just "didn't work." Future work (including a future AI session
  with no memory of this one) depends on this to avoid re-treading the exact
  same dead end blind.
- **Every user-facing change gets a `PATCHNOTES.md` entry in the same pass as
  the change itself**, sorted into the current release's own **What's New /
  Fixed / Documentation / Groundwork / Investigated, Not Yet Resolved**
  sections — see `PATCHNOTES.md`'s own structure comment for the exact
  category definitions and the "exactly one heading of each kind per
  release" rule. Not a separate cleanup task, not something to batch later.
- **Every bug fix, non-trivial finding, or investigation gets its own
  numbered entry in `re_notes/known_issues_x64.md`, in the same pass as the
  fix** — not just a `PATCHNOTES.md` line. `PATCHNOTES.md` is the curated
  summary a player reads; `known_issues_x64.md` is this project's actual
  source of truth for *why* something is the way it is, and a fix that only
  exists in the patch notes leaves no trail for the next session (human or
  AI) to find when the same symptom resurfaces, or when a second, deeper
  root cause turns up under a fix that looked complete at the time. Each
  entry opens with a `**Status:**` line from a fixed vocabulary (Open /
  Investigating / Partially Resolved / Resolved / Deferred / Roadmap Idea)
  and gets a top-of-file Index line.
- **Cite concrete evidence, not conclusions.** A finding is "confirmed via
  disassembly at address `0x...`" or "live-tested, see log," not "should be
  correct" or "this seems right." Distinguish confirmed-live, from
  static-analysis-only, from theorized-but-untested — explicitly, every
  time. Don't let confidence bleed across that line in the writing; a reader
  should never have to guess how solid a claim actually is.
- **Cross-reference, don't duplicate.** `re_notes/known_issues_x64.md` is the
  actively-tracked issue list and RE trail, `re_notes/compatibility_matrix.md`
  is the per-mission/per-mode live playtest status, `PATCHNOTES.md` is the
  curated player-facing changelog, `README.md` is the feature/status
  overview. A given fact belongs in exactly the place that owns it, linked
  from everywhere else that needs it — not copy-pasted across several files
  that will inevitably drift out of sync.
- Undocumented work is not done, by the same standard as untested work.

## Debugging Methodology

Standing rules from a real 16-day, two-round investigation that traced a
"no controller glyphs" report back to a config flag this project itself had
shipped hardcoded off from day one — one already printing on the first line
of every log capture the whole time:

- **Before investigating anything external (hardware, OS, other software,
  memory layout, third-party overlays), dump and personally read every
  gating flag this project's own config loader already logs.** Read the
  WHOLE line, not just the field your current hypothesis predicts.
  Confirmation bias in what you grep for is as dangerous as a missing
  diagnostic.
- **A real fix found along the way is not evidence the remaining reports are
  something more exotic.** It's easy to let "we already found and fixed a
  real bug here" quietly close off re-checking the simplest original
  explanation for everyone still affected. Re-verify the mundane gate again
  after every fix, before escalating the theory.
- **Don't scope the search window to "what changed recently."** A silent,
  permanent, wrong-since-introduction default is invisible to any diff- or
  changelog-shaped search — it only surfaces from a full, unfiltered read of
  current state, independent of when it was introduced.
- **A reporter saying "I already tried the config fix" is a claim about
  their actions, not a verified fact about the code.** Check what the fix
  they're describing actually does in the source before treating it as
  having ruled anything out.
- **Direct, first-party reproduction beats remote diagnosis from partial
  reports.** When remote diagnosis stalls, install fresh yourself and read
  your own log start-to-finish rather than re-grepping reporters' partial
  logs for the same prior hypotheses.

## Performance Investigation Discipline

Standing rules from a stutter/freeze investigation that found five separate
real root causes (plus one self-inflicted regression caught mid-investigation)
before landing on a sixth, native/non-fixable explanation:

- **Once you find one instance of a bug SHAPE, systematically audit the
  whole codebase for the same shape — don't assume it's isolated.** A
  synchronous Windows syscall (a file stat, a log flush, a GDI font call)
  running unconditionally on the game's own main thread is exactly this
  kind of pattern — once recognized once, grep the whole `proxy_d3d9/src/`
  tree for the same shape rather than waiting for the next live report to
  point at each instance individually.
- **A clean profile from an instrumented tool proves nothing about code
  paths the tool never measures — confirm the suspected code is actually
  instrumented before trusting "it shows nothing."**
- **A fix that touches an ALREADY-DOCUMENTED hot/flood-prone code path must
  be checked against that path specifically, not just reviewed in
  isolation.** When adding a call to any function, grep that function's own
  history/comments for "fires N times per frame"-shaped prior findings
  before assuming a cheap-looking addition is safe.
- **When a symptom correlates with screen resolution, use aspect ratio as
  the discriminator between "our own scaling math is buggy" and "native
  rendering cost scales with resolution."** A bug in this project's own
  scaling code only ever shows up on a non-16:9 resolution change
  (`scaleX != scaleY`); a symptom that persists across same-aspect-ratio
  resolution changes points at genuine GPU/pixel-count-scaled cost instead —
  real, but not something a proxy-DLL's own math can fix.
- **When asked to make threading "safer" or add a "fallback" against an
  overloaded thread, prefer splitting logically DISTINCT jobs onto separate
  threads (division of labor) over duplicating the SAME job onto a second
  thread (redundancy).** A literal redundant fallback poller reading the
  same hardware source a second time risks reintroducing "two things
  reading the same source fight each other" bugs this project has hit more
  than once.

## Investigation & Persistence Discipline

- **Fresh perspective breaks real stalemates — but this is NOT "switch
  approach after any failed attempt."** Keep pushing the current, reasoned
  angle through ordinary setbacks. Only once a bug has survived an extended
  run of genuine, well-reasoned attempts at the SAME angle — call it 5-6+ —
  and investigation is demonstrably going in circles, is that the signal to
  STOP and ASK the user whether to keep pushing or shift to something
  genuinely different, not to silently decide that on your own. Hitting the
  threshold is never itself authorization to pivot autonomously — the user
  explicitly asking for a different angle is a separate, valid trigger at
  any point, without needing the threshold at all. The bar is deliberately
  high specifically because AI tooling is optimized for token usage in a way
  that makes it too easy to bail on a reasoned approach early.
- **An asymmetric symptom points at overflow, not convention.** When a bug
  affects one specific extreme/direction/value while everything else works
  correctly, that shape is the signature of an integer overflow/wraparound
  at that exact extreme, not a sign-convention or byte-offset bug (which
  would normally affect a whole class of inputs symmetrically). Check the
  arithmetic range and the target type's own limits before re-litigating
  parsing/sign theories again.
- **Checking is far cheaper than digging — verify an assumption directly
  before building on it.** When a fact is checkable in the time it takes to
  grep or build, check it — don't reason about it from memory or a partial
  re-read and treat that as ground truth.
- **Before dispatching new RE work (a Ghidra pass, a fresh string/xref
  search, a binary decompile), search this project's own `re_notes/` for an
  existing file by name/topic first** — a topic-relevant filename search
  costs seconds and can make an entire fresh investigation unnecessary.
- **A wrong-but-real first fix is not a failure to hide — document it
  honestly, alongside the fix that actually worked.** Keep the first fix's
  own entry/commit if it was real and worth keeping (don't revert working
  code just because it didn't solve the reported symptom) — write up BOTH
  fixes in the same `known_issues_x64.md` entry, in the order they actually
  happened, including what evidence ruled the first one out as the real
  blocker.

## Native project code (C/C++)

- **Hook targets are resolved via runtime signature scanning** —
  `signature_scan.h`/`.cpp`, a wildcarded byte-pattern scan against the
  game's own main module, resolved once at process startup and cached for
  the session. Do not hardcode a fixed address for a new engine-function
  hook. Validate a signature actually resolved (non-null, sane surrounding
  bytes) before installing a hook on it — fail loudly and refuse to hook
  rather than jumping to garbage.
- All hook callbacks must be safe to call from the game's own thread(s) — no
  blocking calls, no heavy work inline; queue/defer anything expensive.
- **Standard, not yet met**: clean up hooks and hold no dangling trampolines
  on DLL unload. `dllmain.cpp`'s `DLL_PROCESS_DETACH` currently only unloads
  plugins and fonts — no `MH_DisableHook`/`MH_RemoveHook`/`MH_Uninitialize`
  call exists anywhere in the codebase, so every installed hook's trampoline
  is left dangling on unload today. Kept as the standard to hold new work
  to; a real project-wide cleanup pass to actually satisfy it hasn't
  happened yet.
- Keep XInput polling, hook installation, and gameplay-input translation in
  clearly separate modules — don't let scan/hook plumbing and gameplay logic
  tangle together.
- **When adding a new "must be distinct from every other one" constant**
  (kbutton bind indices, struct-identity offsets, etc.), **grep the WHOLE
  file for existing values first** — don't just check the nearby comment's
  own "distinct from X/Y/Z" list. A comment can only enumerate what its
  author remembered was nearby; it can't substitute for an actual
  whole-file search.
- **A function defined inside ANY anonymous namespace — nested or not — has
  internal linkage and cannot be called from another translation unit,
  UNLESS declared `extern "C"`.** MSVC gives `extern "C"` functions inside
  an anonymous namespace genuine external (C) linkage as a real, documented
  exception — plain C++ functions get no such exception. A function sitting
  right next to a working `extern "C"` one is NOT proof it shares the same
  linkage. When linkage is in doubt, trust the compiler/linker's own
  verdict — build and check the real error (or `dumpbin /symbols` on the
  built `.obj`) — never reason about brace-nesting by eye and treat that as
  settled.
- **A `static` local inside a function that's called once PER-ITEM in a loop**
  (once per slot, once per list entry, once per frame-visible hint, etc.)
  **is scoped to the function, not to the item** — it silently assumes "this
  function is called at most once per frame." If a system that used to draw
  one thing at a time grows a second simultaneous instance, the shared
  static starts corrupting state between items with no compile error and no
  crash. Per-item edge-detection/debounce state needs to be a small array
  indexed by the item's own identity, not a bare function-scoped `static`.
- **When extending an existing multi-part tool/system to a new context,
  audit EVERY control it depends on for that context — not just the
  newly-built interactive part that's easy to eyeball-test.** A feature can
  look complete because its visible/interactive half works while a
  persistence or trigger path silently never fires in the new context. Trace
  where EACH of a feature's triggers is polled when porting it, not just
  whether the visible parts respond.

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
- **This project has a hard-line, permanent policy against reading live
  gameplay-entity memory.** Not a feature to build carefully; a closed door.
  If any future work genuinely needs to read game-process memory for a
  non-gameplay purpose, treat it as untrusted/variable between binary
  versions and validate before dereferencing — but the decision to read
  entity/world memory AT ALL is a separate, explicit go/no-go conversation
  with the user every time, not something this standard pre-authorizes.
- See `SECURITY.md` for what counts as a reportable security issue and how
  to report one.

## Scope discipline

- Only make changes that are explicitly requested or clearly required by the
  task at hand — don't bundle unrelated fixes or refactors into the same
  change.
- No hardcoded addresses (signature-scanned only — see "Native project code"
  above), no OS-level input emulation beyond the documented, narrowly-scoped
  exceptions — see `CONTRIBUTING.md` for the full ground rules.
