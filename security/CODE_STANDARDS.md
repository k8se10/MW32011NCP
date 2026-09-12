# Code Standards

This document is the authoritative statement of the bar every change in this
repository is held to — human-written or AI-assisted, no distinction. It's
referenced from `CONTRIBUTING.md`; read this file in full before opening a PR.
Mirrors the sibling `MW32011NCP` project's own `CODE_STANDARDS.md`, extended
with this project's own responsible-disclosure requirements.

## AI-assisted contributions are permitted — held to the exact same bar

This project (like its sibling) is developed with heavy use of AI coding
assistance. That is explicitly fine. What is **not** fine, from any
contributor, human or AI:

- **No placeholder hooks.** No `// TODO: find real offset later`, no stub
  function committed as if it were the real thing, no "this should fix it"
  submitted without having actually reproduced the original bug and confirmed
  it no longer triggers.
- **No half-finished work presented as done.** A fix is either
  production-ready per the criteria below, or it isn't merged/committed as
  complete.
- **No unfinished work masquerading as finished.** A fix that only closes one
  of several ways to trigger the same underlying bug, or only works against
  one of the two target binaries, is not done, and must not be described as
  done in a commit message, PR description, or code comment.

## Production-Ready Only

A fix is only "done" once it's been verified to actually stop the specific
vulnerability it claims to close — reproduced against the ORIGINAL,
unpatched behavior first (confirm you can actually trigger the bug), then
confirmed the fix prevents it, not just "the code compiles and looks right."

## Production Readiness Criteria

A fix is **production ready** when:

1. ✅ The original vulnerability was actually reproduced first — a fix for a
   bug you never triggered yourself isn't verified, it's a guess
2. ✅ The fix is confirmed to prevent that exact reproduction, tested against
   the actual running game (both `iw5sp.exe` and `iw5mp.exe` where the bug
   applies to both — never assume parity, see below)
3. ✅ Normal, legitimate netcode traffic still works correctly — a fix that
   closes a vulnerability by breaking real gameplay traffic isn't acceptable;
   verify real multiplayer/co-op play still functions after the patch
4. ✅ No new crashes/instability introduced elsewhere — tested through real
   play, not just the specific repro case
5. ✅ Documented — the vulnerability and the fix are both explained in
   `re_notes/`, in enough detail for another contributor to independently
   verify (see "Documentation Standards" below and `SECURITY.md`'s
   disclosure-timing guidance for anything severe/actively-exploited)
6. ✅ Committed — changes are in this project's own git repo with a clear
   message

A fix that fails any one of these is not done, regardless of how confident
the description of it sounds.

## Documentation Standards

Same standard as the sibling project: document every last detail, including
dead ends. Additionally, for this project specifically:

- **Every fix documents the vulnerability it closes precisely enough to
  verify, and no more than that** — real, specific technical detail for a
  legitimate contributor to reproduce and confirm the fix, not a
  ready-to-run exploit tool bundled alongside the fix.
- **Cite real sources** for any vulnerability drawn from public research
  (a CVE ID, a GitHub PoC repo, a security advisory) rather than restating it
  without attribution — `re_notes/vulnerability_research.md` is the pattern
  to follow.
- **Distinguish "confirmed against our own binary" from "believed applicable
  from public research, not yet independently verified"** explicitly, every
  time. Four findings are now confirmed this way (three vulnerable, one
  confirmed safe) — but any NEW candidate starts as "believed applicable"
  until it's independently re-verified against this project's own binary
  copies. Don't let that distinction blur.
- **Redaction: document every last detail, except the exact address/offset of
  a still-unpatched vulnerable function, or (once fixed) the precise
  bypass-enabling implementation detail of the fix.** Full policy lives in
  `SECURITY.md`'s "Redaction policy" section — the short version: those two
  categories go only in a gitignored `re_notes/INTERNAL_<name>.md` counterpart
  to the public doc (never committed), and the public doc gets a one-line
  pointer in their place. Everything else — CVE citations, architecture,
  command tables, methodology, dead ends — stays fully public. When adding a
  new public `re_notes/` doc that will need this split, create its
  `INTERNAL_` counterpart as a real paired file from the start, not an
  afterthought, and keep the two in sync as research progresses. Once an
  item's fix ships and is verified, un-redact it in the public doc — the
  split protects what's unfixed, not what's already shipped and working.

## Native patch code (C/C++)

- Every hook target should be independently confirmed against **this
  project's own copies** of `iw5sp.exe`/`iw5mp.exe` via Ghidra — don't build a
  fix purely from a public writeup's description of a different game version
  or a related title (MW2, etc.) without confirming the same function/bug
  exists at whatever address it resolves to in this project's own binaries.
- **`iw5sp.exe` and `iw5mp.exe` are separate binaries, RE'd independently** —
  same principle as the sibling project. Don't assume a vulnerable function
  found in one exists at the same offset, or even in the same form, in the
  other.
- All hook callbacks must be safe to call from the game's own thread(s) — no
  blocking calls, no heavy work inline.
- Clean up hooks and hold no dangling trampolines on DLL unload.
- Log what the patch found and did (vulnerability check triggered, packet
  rejected/sanitized, hook install/uninstall) to a file a user or the
  developer can pull after an incident — silent failure is not acceptable for
  a security-relevant patch even more than it would be for a gameplay one.

## Input Validation & Security

- This entire project IS input validation — but the same rule about the
  project's OWN code applies recursively: any bounds/length check this
  project adds must itself be correct (an off-by-one in a "security fix"
  that turns a heap overflow into a stack overflow elsewhere is not a fix).
- Never write secrets, tokens, or account details into this project's own
  source or committed config.
- See `SECURITY.md` for what counts as a reportable security issue (in either
  the base game or this project's own patch code) and how to report one
  responsibly.

## Scope discipline

- Only make changes explicitly requested or clearly required by the phase
  currently in progress — don't bundle Phase 2 hardening work into a Phase 1
  targeted bug fix, and vice versa.
- **Hook targets are resolved via runtime signature scanning**
  (`proxy_d3d9/src/signature_scan.h`/`.cpp`) — a wildcarded byte-pattern
  scan against the game's own main module, resolved once at process
  startup and cached for the session, not a continuous re-scan loop. This
  is the current policy, matching the sibling `MW32011NCP` project's own
  (see that project's `CODE_STANDARDS.md` for the full history: a runtime
  scanner was framed as a real VAC-risk tradeoff at one point, but a
  hardcode-only policy cannot survive a binary update at all — MW3's own
  2026-09-03 recompile proved that concretely for the sibling project, and
  the same reasoning applies here). Do not hardcode a fixed address for a
  new hook target. Validate a signature actually resolved (non-null, sane
  surrounding bytes) before installing a hook on it — fail loudly rather
  than hooking garbage.
- **Scope a fix as narrowly as the real call sites, not the whole enclosing
  function or shared primitive.** A hook on a primitive shared by many
  unrelated, legitimate call sites must be scoped by exact return address
  (or another precise discriminator) to only the specific vulnerable call —
  see `proxy_d3d9/src/matchdatadone_memberjoin_fix.cpp`'s own header comment
  for a worked example (one shared copy primitive, two distinct vulnerable
  call sites, one unrelated call left deliberately untouched).
