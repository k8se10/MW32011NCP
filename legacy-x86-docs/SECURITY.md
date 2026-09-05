# Security Policy

*Last updated 2026-08-01.*

## Supported Versions

This project is pre-alpha software under active development. **v0.2.2 and
every release since are supported** — please update to at least v0.2.2
before reporting an issue if you're on something older. Every release before
v0.2.2 has been unpublished from GitHub Releases as a VAC-risk mitigation
step (aim assist, present in those builds, was permanently removed in
v0.2.2 — see `re_notes/known_issues.md` issue #15); their git tags/commits
remain fully intact in this repository, just not offered as downloads.

| Version | Supported |
| ------- | --------- |
| v0.2.2 and later (see [Releases](../../releases)) | ✅ |
| Pre-v0.2.2 | ❌ (unpublished, VAC-risk mitigation — see issue #15) |

## A note on VAC (Valve Anti-Cheat)

This is a proxy-DLL/code-injection project, and Multiplayer's `iw5mp.exe`
does have VAC active. That's a real, non-zero risk-to-the-user question, not
a code vulnerability in the sense described below — full research and
reasoning is in `re_notes/known_issues.md` issue #33. If you're specifically
concerned about anti-cheat exposure, read that first; this document covers
vulnerabilities in the project's own code, not that broader risk
disclosure.

## What counts as a security issue here

This project ships as a proxy `d3d9.dll` that gets loaded by `iw5sp.exe` at launch
and hooks real engine functions in that process. Given that shape, the kinds
of issues that matter most are:

- **Memory-safety bugs in the project's own code** (out-of-bounds reads/writes,
  use-after-free, etc.) that could be triggered by in-game state and lead to
  more than a crash — e.g. anything that looks like it could be turned into
  arbitrary code execution inside the game process.
- **Supply-chain concerns** — e.g. a release artifact that doesn't match its
  published source, or a way to trick a user into loading a malicious
  `d3d9.dll` believing it's this project's.
- Anything in the project's hooking/injection mechanism itself that could be
  abused beyond its intended scope (this project is input-only — it does not read
  or write anything beyond what's needed for controller input and the
  documented sprint-stamina/menu-state logic; a report that it does more than
  that is a security report, not just a bug).

**Not in scope** (please still file these as regular issues, not security
reports): ordinary crashes from an incomplete/unimplemented feature, gameplay
bugs, or anything that only affects the reporter's own single-player session
with no broader implication.

## Reporting a Vulnerability

Please email **k8se10@gmail.com** rather than opening a public issue.
Include:

- What you found and why you believe it's a security issue (not just a bug)
- Steps to reproduce, if possible
- Which release/commit you tested against

You should get an acknowledgment within a few days. This is a solo,
from-scratch reverse-engineering project worked on outside of full-time hours,
so response and fix time will vary — but security reports will be
prioritized over regular feature/bug work.

## Scope note: this is not the game itself

This project has no affiliation with Activision, Infinity Ward, or Call of
Duty: Modern Warfare 3 itself. Vulnerabilities in the base game are out of
scope for this repository — please report those through the game
publisher's own channels.
