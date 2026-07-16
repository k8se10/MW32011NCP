# Security Policy

## Supported Versions

This project is pre-alpha software under active development. Only the most
recent release is supported — please update to the latest `d3d9.dll` before
reporting an issue.

| Version | Supported |
| ------- | --------- |
| Latest release (see [Releases](../../releases)) | ✅ |
| Older releases | ❌ |

## What counts as a security issue here

This mod ships as a proxy `d3d9.dll` that gets loaded by `iw5sp.exe` at launch
and hooks real engine functions in that process. Given that shape, the kinds
of issues that matter most are:

- **Memory-safety bugs in the mod's own code** (out-of-bounds reads/writes,
  use-after-free, etc.) that could be triggered by in-game state and lead to
  more than a crash — e.g. anything that looks like it could be turned into
  arbitrary code execution inside the game process.
- **Supply-chain concerns** — e.g. a release artifact that doesn't match its
  published source, or a way to trick a user into loading a malicious
  `d3d9.dll` believing it's this project's.
- Anything in the mod's hooking/injection mechanism itself that could be
  abused beyond its intended scope (this mod is input-only — it does not read
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
