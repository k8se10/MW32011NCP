# Security Policy

## What this project is

This is a defensive security project: it studies Call of Duty: Modern Warfare 3
(2011, IW5 engine)'s netcode for real, exploitable vulnerabilities, and ships
fixes for them, primarily to protect players joining public/untrusted servers
(applies to both `iw5sp.exe` — Campaign/Survival/Spec-Ops co-op — and
`iw5mp.exe` — Multiplayer). It exists because this engine family has a real,
documented history of network-reachable bugs, and Steam's VAC (confirmed active
on `iw5mp.exe`) is aimed at memory-cheat detection, not packet-level/protocol
attacks — a different threat model this project exists to actually cover.

**This creates a two-sided responsibility that doesn't exist for a typical
project's `SECURITY.md`**: this repository's own commit history and `re_notes/`
are, by necessity, a running record of real vulnerabilities in the base game —
not something to hide, since fixing them requires documenting them precisely
enough for a contributor to verify a fix actually works, but also not something
to turn into a how-to-exploit resource. See "Responsible disclosure" below for
how this project handles that tension.

## Supported Versions

Pre-alpha software under active development, same as the sibling `MW32011NCP`
project. Only the most recent release is supported.

| Version | Supported |
| ------- | --------- |
| Latest release (see [Releases](../../releases)) | ✅ |
| Older releases | ❌ |

## Two categories of security report

**1. A vulnerability in the BASE GAME's netcode this project hasn't found/fixed
yet.** This is the primary thing this project wants to hear about — report it
per "Responsible disclosure" below, not as a public issue.

**2. A bug in THIS PROJECT's OWN patch code** (memory-safety issues, a fix that
doesn't actually close the vulnerability it claims to, a supply-chain concern
with a release artifact). Same categories as any security-sensitive C/C++
project — report the same way, see below.

## Responsible disclosure

**Do not open a public GitHub issue for a netcode vulnerability, in this game
or any other.** Email **k8se10@gmail.com** instead. Include:

- What you found and why you believe it's exploitable (a crash is not
  automatically a security issue — see "Not in scope" below)
- Enough detail to reproduce and verify a fix, but please avoid attaching a
  ready-to-run exploit/PoC tool in the initial report — a written description
  and/or a minimal packet capture is preferred; if a working PoC is genuinely
  necessary to demonstrate impact, say so and we'll agree on how to share it
  safely
- Whether you're aware of this already being exploited in the wild (this
  changes urgency significantly)
- Which release/commit you tested against

You should get an acknowledgment within a few days. This is a solo,
from-scratch project worked on outside full-time hours — response and fix time
will vary, but security reports are prioritized over regular feature work.
Once a fix ships, the vulnerability and fix get documented in `re_notes/` and
`PATCHNOTES.md` in enough detail for the community to verify the fix, following
the same "document every last detail" standard as the sibling `MW32011NCP`
project's `CODE_STANDARDS.md` — but a genuinely severe, actively-exploited
finding may have its full technical detail held back for a short period after
the fix ships, to give the community running-server operators time to update
before the exact mechanism is public. This project does not follow a rigid
fixed-day embargo — timing is judged per finding, biased toward disclosure once
a fix is available and verified, not toward secrecy.

## Redaction policy (this project's own docs and commits)

**As open as possible, redacted only where it would expose the backbone of an
unfixed protection.** This project is source-available and public by default
(see `LICENSE`) — the goal is real transparency (anyone can audit that this
patch does what it claims and isn't a backdoor), not secrecy for its own sake.
The one narrow exception, decided 2026-07-17:

- **Exact addresses/offsets of confirmed-or-candidate vulnerable code that
  isn't fixed yet do NOT go in the public repo.** Everything else does:
  which CVEs are targeted, public PoC citations, the general architecture
  (e.g. "there's a real out-of-band packet dispatcher, here's its full
  command table"), the phased plan, methodology, dead ends — all fully public
  and detailed, same "document every last detail" standard as always.
- **Practically**: every public doc that has a redacted item keeps a matching,
  gitignored `re_notes/INTERNAL_<name>.md` counterpart with the full,
  unredacted detail (see `.gitignore`) — never committed, never pushed. That
  file is what real development work against a specific unfixed bug actually
  uses; the public doc gets a one-line pointer in its place ("exact address
  redacted, see internal notes"), not a placeholder that pretends nothing was
  found.
- **Once an item's fix ships and is verified working, un-redact it in the
  public doc too** — copy the real detail over from the internal file. The
  whole point of the split is protecting what's not yet fixed, not
  permanently hiding finished, already-protected work. A public doc that's
  still redacted for something long since fixed is a bug in this policy's
  own upkeep, not correct behavior — fix it when noticed.
- **The same logic applies, even after a fix ships, to the EXACT bypass-
  relevant implementation detail of the fix itself** (e.g. an exact numeric
  threshold or edge-case handling that, if published in full, would hand an
  attacker a roadmap to finding a gap in the fix) — describe what class of
  fix was applied and why (bounds-checking, rate limiting, etc.) at the same
  level of honesty as everything else in this project, without necessarily
  publishing the single line of code an attacker would most want to read
  first. Use judgment here the same way "Responsible disclosure" above
  already asks for it — biased toward disclosure, not toward secrecy, but
  aware that this project's own credibility depends on the fixes actually
  working, not just on maximal transparency about exactly how to defeat them.

## Not in scope

- Ordinary gameplay bugs, crashes from normal (non-adversarial) play, or
  anything specific to a single player's own session with no broader
  implication for other players/servers
- Memory/aimbot-style cheats — that's VAC's domain (already covers this to
  whatever extent it does), and a different threat model from what this
  project addresses
- Vulnerabilities in the base game unrelated to netcode (e.g. a local save-file
  parsing bug with no network reach) — still worth reporting, but lower
  priority for this specific project's scope

## Scope note: this is not the game itself

This project has no affiliation with Activision, Infinity Ward, or Call of
Duty: Modern Warfare 3 itself. It is an independent, unofficial security patch.
Activision/Infinity Ward are not this project's disclosure channel and
reporting a vulnerability here does not constitute reporting it to them.
