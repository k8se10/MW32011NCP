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
  the change itself**, sorted into the current release's own **What's New /
  Fixed / Documentation / Groundwork / Investigated, Not Yet Resolved**
  sections (this schema replaced an older, looser Added/Fixed/Changed/Docs
  convention on 2026-08-18 after duplicate-heading drift became a real, live
  problem — see `PATCHNOTES.md`'s own structure comment for the exact
  category definitions and the "exactly one heading of each kind per release"
  rule). Not a separate cleanup task, not something to batch later. This
  includes corrections to previously-wrong documentation or config-comment
  text, not just code changes.
- **Every bug fix, non-trivial finding, or investigation gets its own numbered
  entry in `re_notes/known_issues.md`, in the same pass as the fix** — not
  just a `PATCHNOTES.md` line. `PATCHNOTES.md` is the curated summary a player
  reads; `known_issues.md` is this project's actual source of truth for *why*
  something is the way it is, and a fix that only exists in the patch notes
  leaves no trail for the next session (human or AI) to find when the same
  symptom resurfaces, or when a second, deeper root cause turns up under a
  fix that looked complete at the time (a real, repeated pattern in this
  project's own history — see the Mantle drag-handle entry, issue #81, for a
  worked example of documenting a wrong-but-real first fix honestly alongside
  the actual one). Each entry opens with a `**Status:**` line from the fixed
  small vocabulary (Open / Investigating / Partially Resolved / Resolved /
  Deferred / Roadmap Idea) and gets a top-of-file Index line — see
  `known_issues.md`'s own Documentation Standards note for the full
  formatting convention.
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

## Performance Investigation Discipline

Added 2026-08-25 after issue #87 (`known_issues.md`) — a recurring stutter/freeze
investigation that found FIVE real, separate root causes (plus one self-inflicted
regression caught mid-investigation) before landing on a sixth, native/non-fixable
explanation. Worth its own section rather than folding into Debugging Methodology
above, since these lessons are specific to hunting performance regressions, not bugs
generally.

- **Once you find one instance of a bug SHAPE, systematically audit the whole
  codebase for the same shape — don't assume it's isolated.** Every one of issue
  #87's first four causes was the exact same pattern: a synchronous Windows
  syscall (a file stat, a log flush, a GDI font call) running unconditionally on
  the game's own main thread, invisible to this project's own
  `FrametimeBenchmarkLogging` because no instrumentation column existed for that
  specific call. Once that shape was recognized after finding cause 3
  (`CheckConfigHotReload`'s `GetFileAttributesExA`), a systematic fork audit
  grepping for the SAME shape across the whole `proxy_d3d9/src/` tree found causes
  4 and 5 directly — faster and more reliable than waiting for the next live
  report to point at each one individually.
- **A clean profile from an instrumented tool proves nothing about code paths the
  tool never measures — confirm the suspected code is actually instrumented
  before trusting "it shows nothing."** `frame_benchmark.h` only ever timed
  rumble, asset-capture, and real `CreateTexture` calls; it had zero coverage for
  disk I/O (log flush, config-hot-reload stat) or GDI/font work
  (`MeasureTextWidthPx`). Five real, measurable-cost bugs existed for the entire
  session while the tool's own numbers stayed near-zero throughout, because none
  of the five were ever inside what it actually measured.
- **A fix that touches an ALREADY-DOCUMENTED hot/flood-prone code path must be
  checked against that path specifically, not just reviewed in isolation.** Issue
  #87's own event-driven-polling fix (`Controller_RequestPoll`) was itself a real,
  if smaller, regression: it added a `SetEvent` call, unconditionally, to
  `InjectMenuInputTick` — a function this project's own 2026-08-08 history already
  documents as firing "dozens of times per rendered frame" during mouse movement,
  the exact flood pattern that caused the original 4fps regression the whole
  background-poll-thread architecture exists to prevent. `SetEvent` is far
  cheaper than the `XInputGetState` call that caused THAT regression, but "cheaper
  per call" is not "free when flooded" — caught only because the user directly
  asked "what if threading is what's causing the issue," prompting a self-audit
  that should have happened automatically once the new call landed inside a path
  this project's own history already flags as flood-prone. When adding a call to
  any function, grep that function's own history/comments for "fires N times per
  frame"-shaped prior findings before assuming a cheap-looking addition is safe.
- **When a symptom correlates with screen resolution, use aspect ratio as the
  discriminator between "our own scaling math is buggy" and "native rendering
  cost scales with resolution."** This project's own scaling code
  (`GetResolutionScale`) only ever produces a real BUG (`scaleX != scaleY`,
  distorting fixed-aspect assets) on a non-16:9 resolution change (e.g. 4:3). A
  uniform-aspect-ratio change (1080p -> 1440p, both 16:9, `scaleX == scaleY` at
  both) ruling out that whole bug class while the symptom still gets WORSE at the
  higher resolution points at genuine GPU/pixel-count-scaled cost instead — real,
  but not something a proxy-DLL's own math can fix. Confirmed this way for issue
  #87's own residual (fine at 1080p/4:3, breaks above 1080p) without needing a
  live GPU capture to rule out the mod-side theory first.
- **When asked to make threading "safer" or add a "fallback" against an
  overloaded thread, prefer splitting logically DISTINCT jobs onto separate
  threads (division of labor) over duplicating the SAME job onto a second thread
  (redundancy).** A literal redundant fallback poller reading the same hardware
  source a second time risks reintroducing the exact "two things reading the same
  source fight each other" bug class this project has already hit more than once
  (DualSense-vs-XInput poll-priority contention; the Steam Input theory checked
  and ruled out during this same investigation). Issue #87's own real
  thread-safety improvement was giving vibration writes their own dedicated
  thread, separate from input polling — two different jobs that used to share one
  thread and could stall each other — not a second poller.

## Investigation & Persistence Discipline

Added 2026-08-25, formalizing two standing rules from `CLAUDE.md`'s own Key
Principles (`§10`, items 9-10) — repeated here because they're as much a
code-quality standard as a debugging-methodology one.

- **Fresh perspective breaks real stalemates — but this is NOT "switch
  approach after any failed attempt."** Keep pushing the current, reasoned
  angle through ordinary setbacks. Only once a bug has survived an extended
  run of genuine, well-reasoned attempts at the SAME angle — call it 5-6+ —
  and investigation is demonstrably going in circles, is that the signal to
  STOP and ASK the user whether to keep pushing or shift to something
  genuinely different (a fresh reference implementation to diff against,
  outside domain knowledge, etc.), not to silently decide that on your own.
  Hitting the threshold is never itself authorization to pivot
  autonomously — the user explicitly asking for a different angle is a
  separate, valid trigger at any point, without needing the threshold at all.
  The bar is deliberately high (not "one or two setbacks") specifically
  because AI tooling is optimized for token usage in a way that makes it too
  easy to bail on a reasoned approach early — a high, explicit numeric bar is
  what keeps genuine persistence the default rather than shallow thrashing.
  See `CLAUDE.md` §10 item 9 for the full worked example (issue #77, ~9 real
  rounds before the actual fix).
- **An asymmetric symptom points at overflow, not convention.** When a bug
  affects one specific extreme/direction/value while everything else works
  correctly (e.g. "diagonals fine, backward fine, only full-forward broken"),
  that shape is the signature of an integer overflow/wraparound at that exact
  extreme, not a sign-convention or byte-offset bug (which would normally
  affect a whole class of inputs symmetrically). Check the arithmetic range
  and the target type's own limits before re-litigating parsing/sign theories
  again — issue #77's real fix was exactly this: `+128*256=32768` silently
  overflowing a 16-bit `SHORT` (max 32767) into `-32768`, affecting only the
  single input that could ever reach exactly that extreme.
- **Checking is far cheaper than digging — verify an assumption directly
  before building on it.** Direct precedent (2026-08-25): a linkage bug was
  correctly diagnosed, "fixed," then WRONGLY reverted based on a flawed manual
  re-read of the surrounding code, reintroducing the bug — only a real MSVC
  build (a real `LNK2019`) caught the mistake. A quick grep for the enclosing
  namespace, or better, an actual `dumpbin /symbols` check on the built
  `.obj`, would have caught it in seconds instead of an expensive round-trip
  through a wrong assumption. When a fact is checkable in the time it takes
  to grep or build, check it — don't reason about it from memory or a partial
  re-read and treat that as ground truth. See the linkage-specific version of
  this lesson under **Native project code (C/C++)** below.
- **Before dispatching new RE work (a Ghidra pass, a fresh string/xref search, a
  binary decompile), search this project's own `re_notes/` for an existing file
  by name/topic first — a prior session's real output may already answer the
  question.** Direct precedent (2026-08-25, `known_issues.md` issue #89): two
  separate RE passes on "what's Survival's real scoreboard stat data source"
  both came back inconclusive from scratch — a binary string search, then a
  native entity-field-dispatcher trace — neither one checked whether this
  project had already investigated the same question. It had:
  `re_notes/survival_mode_overview.md` (a session from over a month earlier)
  already named the real scriptfile and summarized its scoring system, and the
  actual decompiled GSC source (`xensik/gsc-tool`'s own output) was still sitting
  on disk, unread, the whole time. A direct user question — "didnt we already
  decomp it for inspection" — is what actually found it, not a research pass.
  `re_notes/` is this project's own accumulated RE memory, spanning many past
  sessions; a topic-relevant filename search (`ls`/`find` by keyword, not just
  `grep` inside files you already know about) costs seconds and can make an
  entire fresh investigation unnecessary — do it before spending real Ghidra/
  decompile time re-deriving something already on disk.
- **A wrong-but-real first fix is not a failure to hide — document it
  honestly, alongside the fix that actually worked.** This project's own
  history has multiple real cases (`known_issues.md` issues #62, #81) where
  an initial fix addressed a genuine bug that turned out not to be the actual
  blocker for the reported symptom, and a second, different root cause was
  the real answer. Keep the first fix's own entry/commit if it was real and
  worth keeping (don't revert working code just because it didn't solve the
  reported symptom) — write up BOTH fixes in the same `known_issues.md`
  entry, in the order they actually happened, including what evidence ruled
  the first one out as the real blocker. A reader should be able to follow
  the actual investigation, not just see the final answer with no trace of
  the (real, useful) wrong turn along the way.

## Native project code (C/C++)

- **Hardcoded, statically-resolved addresses are this project's deliberate,
  permanent policy — REVERSED 2026-08-25 (direct correction: "no hardcoded
  addresses isnt a claim we can make anymore, its safer for vac than pattern
  scanning which could touch protected regions of memory").** This standard
  used to frame a runtime byte-pattern/signature scanner as the aspirational
  goal, with static-hardcode-per-binary as an honest-but-temporary current
  state. That framing is wrong and is not coming back: a runtime scanner has
  to walk arbitrary regions of the game's own process memory searching for a
  byte sequence every time it resolves — exactly the class of behavior VAC's
  own signature-based heuristics are built to notice, and a real way to
  touch a protected/guarded memory region this project never intended to
  read. A hardcoded address found offline (static Ghidra analysis, no live
  process attached) and simply called at a known, fixed location has no such
  runtime search surface at all — narrower, safer, and more predictable.
  Every real engine-function hook in this codebase is a literal hardcoded
  address, found once via static analysis and never re-resolved at runtime —
  see `CONTRIBUTING.md`'s own matching correction for the exact wording.
  **Do not propose a runtime scanner for engine-function hooks** — it is a
  rejected idea on VAC-safety grounds, not a deferred project-wide effort
  waiting to happen; match the established pattern (static analysis,
  hardcode, document per binary) for any new hook instead.
- Validate a signature actually resolved (non-null, sane surrounding bytes)
  before installing a hook on it — fail loudly and refuse to hook rather than
  jumping to garbage. Applies to the static, offline Ghidra-analysis pass
  used to find every hardcoded address above, not to a runtime scan (see
  that bullet for why this project deliberately doesn't do runtime
  scanning).
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
- **When adding a new "must be distinct from every other one" constant
  (kbutton bind indices, struct-identity offsets, etc.), grep the WHOLE file
  for existing values first — don't just check the nearby comment's own
  "distinct from X/Y/Z" list.** Lesson from a real, shipped critical
  regression (2026-07-31, issue #46): Hold Breath's bind index was set to
  `17`, silently identical to Fire's own bind index (also `17`) — both
  constants were genuine and each had its own "distinct from ADS/Reload/
  Sprint" comment, but neither was defined near the other (different sections
  of the same file, added on different days) so the collision was never
  caught until a live playtest found "can't fire while holding breath." A
  comment can only enumerate what its author remembered was nearby — it
  can't substitute for an actual whole-file search.
- **A function defined inside ANY anonymous namespace — nested or not — has
  internal linkage and cannot be called from another translation unit,
  UNLESS declared `extern "C"`.** MSVC gives `extern "C"` functions inside an
  anonymous namespace genuine external (C) linkage as a real, documented
  exception — plain C++ functions get no such exception. A function sitting
  right next to a working `extern "C"` one is NOT proof it shares the same
  linkage; a naive brace-counter (or a manual "does this look like it's
  inside braces" read) cannot see this distinction reliably. Real, repeated
  cost: a correct fix (closing/reopening the anonymous namespace around one
  function) was WRONGLY reverted based on a flawed manual re-read, only
  caught by a real `LNK2019` from an actual MSVC build. When linkage is in
  doubt, trust the compiler/linker's own verdict — build and check the real
  error (or `dumpbin /symbols` on the built `.obj`) — never reason about
  brace-nesting by eye and treat that as settled.
- **A `static` local inside a function that's called once PER-ITEM in a loop
  (once per slot, once per list entry, once per frame-visible hint, etc.) is
  scoped to the function, not to the item — it silently assumes "this
  function is called at most once per frame."** If that assumption stops
  being true (a system that used to draw one thing at a time grows a second
  simultaneous instance), the shared static starts corrupting state between
  items with no compile error and no crash. Real case (2026-08-25, issue
  #81): `DrawOneGameplayHintSlot`'s own `static bool s_lastMouseHeld` (click-
  edge detection for a drag handle) was shared across all 4 gameplay-hint
  slots; whenever two hints were visible in the same frame, the earlier-
  processed slot silently ate the later slot's click-edge detection for that
  frame, and the LAST slot in the fixed iteration order could never register
  a click whenever anything else shared its frame. Any per-item edge-
  detection/debounce state inside a function that can run multiple times per
  frame needs to be a small array indexed by the item's own identity (slot
  ID, list index), not a bare function-scoped `static` — the same way this
  project already keeps `g_gameplayHintEditNudges` per-slot rather than
  sharing one.
- **When extending an existing multi-part tool/system to a new context, audit
  EVERY control it depends on for that context — not just the newly-built
  interactive part that's easy to eyeball-test.** Lesson from a real bug
  (2026-08-25): the in-game glyph-position editor was extended from menu
  items to gameplay hints — the drag handles were built and worked fine, so
  the feature LOOKED complete, but the F3 export hotkey was only ever polled
  inside the menu-item editor's own per-frame function, which never runs
  during actual gameplay — so F3 silently never fired while calibrating a
  real gameplay prompt. The interactive half worked; the persistence half was
  dead the whole time, and nothing about watching the drag handles move would
  ever reveal that. When porting a feature to a new context, trace where
  EACH of its triggers is polled, not just whether the visible/interactive
  parts respond.

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
  gameplay-entity memory (corrected 2026-08-25 — this line previously cited
  "aim-assist work" as a live example; aim assist was permanently REMOVED
  2026-07-20, not paused, specifically because that class of read is
  mechanically identical to a soft-aimbot regardless of intent — see
  `CLAUDE.md`'s own "Aim-assist target" section and `known_issues.md` issue
  #33).** Not a feature to build carefully; a closed door. If any future work
  genuinely needs to read game-process memory for a non-gameplay purpose
  (e.g. save-state serialization, `known_issues.md` issue #80, paused
  pending its own explicit risk discussion), treat it as untrusted/variable
  between binary versions and validate before dereferencing — but the
  decision to read entity/world memory AT ALL is a separate, explicit
  go/no-go conversation with the user every time, not something this
  standard pre-authorizes.
- See `SECURITY.md` for what counts as a reportable security issue and how
  to report one.

## Scope discipline

- Only make changes that are explicitly requested or clearly required by the
  task at hand — don't bundle unrelated fixes or refactors into the same
  change.
- No hardcoded addresses, no OS-level input emulation beyond the three
  documented, narrowly-scoped exceptions (`re_notes/known_issues.md` issues
  #5, #13/#14, and #28 — corrected 2026-08-25, this line previously said
  "two" and named only #5/#14, missing the Back/`+scores` synthetic-TAB
  exception added later) — see `CONTRIBUTING.md` for the full ground rules.
