# LTS Policy

This document explains which MW32011NCP releases carry long-term support (LTS),
how a release earns that status, and when support for an older one ends. It exists
so the policy is a stable, public commitment — not something reconstructed from a
GitHub Releases page or inferred from patch notes.

## Why an LTS line exists at all

Most releases in this project's `0.x` line are still active development —
new systems landing, existing ones being corrected against live testing, config
schema still shifting. That's appropriate for an alpha, but it means most releases
are not a good target for someone who just wants the mod to work reliably for a
while without tracking every change.

LTS releases are the answer to that: a small, deliberately-chosen subset of
releases that get *extended* support — continued availability and, where a real
regression is found, a backported fix — after the rest of that release's own
minor line has moved on.

## The rule

**Every `0.x` minor line designates exactly one LTS release.** Not the first
release in that line, and not automatically the last — whichever release in that
line is judged stable enough after real-world use, following the promotion
process below.

**Only one LTS is supported at a time, with a 4-week handover overlap.** When a
new LTS is promoted, the previous LTS doesn't disappear immediately — it keeps
receiving support for 4 more weeks alongside the new one, so anyone still on it
has a real window to move over deliberately rather than being cut off the moment
a new LTS is announced. At the end of that 4-week overlap, support for the
previous LTS ends and it's archived; from that point on, only the current LTS and
the active development line receive releases.

**A release doesn't become LTS on the day it ships.** A candidate has to hold up
under real play for **4 weeks post-release** with no confirmed major regression
before it's promoted. Shipping a version is not the same claim as "this is now
the stable one" — the promotion is a separate, later decision, made after enough
people have actually played it.

**`v0.2.2` is the oldest release that can ever hold LTS status.** Nothing before
it is eligible, under any future circumstance. This isn't a stability judgment —
`v0.2.1` and everything before it shipped with this project's original aim-assist
implementation, which read live gameplay-entity data out of the game's own
process memory to compute rotational friction/target magnetism. That class of
read is mechanically identical to a soft-aimbot regardless of intent, and this
project's own anti-cheat research (`re_notes/known_issues.md` issue #33) treats
it as a real, standing risk category — not a solved or disproven one. `v0.2.2`
(2026-07-20) is the release where aim assist was permanently removed, not just
disabled, specifically to close that risk off going forward. A version that
shipped with the removed feature can't retroactively stop having shipped with
it, so the floor is fixed at `v0.2.2` for good, independent of how this policy
or the project's own risk assessment evolves later.

## Promotion process

1. A release ships as a normal, current-development release — no different from
   any other `0.x` release at launch.
2. It's the **candidate** for its minor line's LTS.
3. The candidate needs **4 weeks of real-world use with no confirmed major
   regression** — reports get triaged and fixed the same as any other bug, but a
   confirmed regression severe enough to reset the clock delays promotion until
   a corrected build has its own clean 4-week window.
4. Once that window closes clean, the candidate is promoted — see **Keeping this
   document current** below for exactly what has to happen in that same pass.
5. If a minor line ends (development moves to the next `0.x`) before any release
   in it accumulated a clean 4-week window, that line simply has no LTS
   candidate promoted from it — the previous line's LTS keeps serving until a
   later line produces one.

This mirrors the process already used for the current LTS transition (see
Current Status below) — this document formalizes a policy that was already being
followed, rather than introducing a new one.

## Keeping this document current

**Every time a release is designated LTS, this file is updated in the same
pass — not as separate, deferred cleanup.** Concretely, promoting a release
means, in one pass:

1. Update the **Current status** table below: the newly-promoted release
   becomes Current LTS; the previous Current LTS becomes "In handover" with the
   date its 4-week overlap ends; the LTS-candidate row is cleared or refilled
   with whatever the next candidate is.
2. Once a previous LTS's handover window actually closes, remove it from the
   table entirely (it's covered by the normal "everything else, superseded"
   row from then on) — don't leave a stale "In handover" row sitting past its
   own end date.
3. Cross-check `re_notes/known_issues.md`'s own LTS-plan entry and
   `PATCHNOTES.md` for anything that now needs the same update (a promotion is
   a real, user-facing event — it belongs in the patch notes for whichever
   release triggered it, same as any other change).

A stale LTS table is worse than no table — a reader has no way to tell the
difference between "this hasn't changed" and "this was never updated," so
treat any promotion that lands without this file being touched the same way
this project treats any other doc left silently out of date: fix it as part of
whatever task surfaces that, not as a separately scheduled cleanup pass.

## Current status

*(Last updated: 2026-09-04.)*

**Further escalation, 2026-09-04: every `-x86` release archived/unpublished
on both Nexus and GitHub, not just support-discontinued.** Direct
instruction: "ive actually archived all releases on nexus and we should do
the same on github. no release works and as such i refuse to serve it."
This goes further than the 2026-09-03 support-discontinuation below (which
left the files themselves downloadable, just unsupported) — since the x64
recompile means literally every `-x86` build fails to load into the live
game at all (not degraded, not partially working — genuinely non-functional
against the only game binary that exists now), the user's own judgment is
that serving a build that cannot work at all is worse than serving nothing.
All 15 GitHub Releases (`v0.1.0-prealpha` through `v0.3.5`) were converted
to **Draft** (`gh release edit <tag> --draft`) — GitHub's own mechanism for
removing a release from the public Releases page and download counts while
keeping the tag, release notes, and uploaded assets intact and restorable,
the same mechanism this project already used for the v0.2.5–v0.3.1.h1
stretch back on 2026-08-18. Nothing was deleted on either platform. The
"remains published as a historical artifact" framing below is now
superseded — see the Current Status table.

**Architecture note (2026-09-03):** MW3 (2011) received its first-ever binary
update, recompiling the live game from x86 to x64 — see `re_notes/x64_migration/README.md`.
Releases now carry a `-x86`/`-x64` suffix (versioning reset to `v0.0.1-x64` for
the new architecture line; the table below relabels the existing x86-line
releases accordingly, it does not rename anything that shipped).

**Emergency escalation, same day: ALL support for the entire `-x86` line is
discontinued, effective immediately — not a frozen clock, a full stop.**
Direct instruction: update the docs "to state the immediate discontinuation
of all support (emergency case) for the existing 32-bit versions." This
supersedes the "frozen candidate" framing this section carried earlier today
— the practical trigger is the same fact (the live game can no longer run a
32-bit build at all, so there is nothing left to verify or support against),
but the policy response is stronger: **every `-x86` release loses its
support status outright**, not just the LTS candidate. Concretely:
- `v0.2.2-x86`, the **Current LTS**, is no longer under active LTS support as
  of 2026-09-03 — no further availability commitment beyond what's already
  published, no backported fixes, no scheduled handover to a successor.
- `v0.3.5-x86`'s LTS candidacy is **withdrawn**, not paused — its 4-week
  clean window will never resume or complete under this policy.
- This is an emergency policy action taken outside the normal promotion/
  demotion process in this document (no successor is being promoted, no
  4-week handover overlap applies) — the process above still governs the
  eventual `0.x-x64` line, this is a one-time break in continuity forced by
  the architecture change, not a precedent for how this policy normally
  operates.
- Existing `-x86` release files remain intact on GitHub/Nexus, not deleted
  — but as of 2026-09-04 (see the further escalation note above) they are
  archived/unpublished (GitHub: every release converted to Draft; Nexus:
  archived by the user directly) rather than left downloadable as
  historical artifacts. Restorable at any time, currently not being served.

| Release | Role | Notes |
|---|---|---|
| `v0.2.2-x86` | **Support discontinued, archived (emergency)** | Formerly Current LTS (promoted 2026-08-18 archival pass). Support discontinued 2026-09-03; the release itself archived/unpublished (Draft on GitHub, archived on Nexus) 2026-09-04 — no longer downloadable through either platform's normal listing. |
| `v0.3.5-x86` | **LTS candidacy withdrawn, archived (emergency)** | Shipped and confirmed live 2026-08-29; its 4-week LTS clean window (started 2026-08-29) was withdrawn 2026-09-03, and the release itself archived/unpublished 2026-09-04 — no longer downloadable through either platform's normal listing. |
| Everything else in `0.1.x`–`0.3.x` (all now `-x86`) | Superseded, no support, archived | Was never LTS-eligible (predates the floor, or superseded by a later release in the same or a later line); already Draft on GitHub since 2026-08-18, now additionally covered by the blanket `-x86` discontinuation and archival above. |
| `0.x-x64` line | **Not yet started** | No x64 release exists yet — versioning resets to `v0.0.1-x64` once one ships. This policy's floor (`v0.2.2` and later only) is an x86-line-specific historical fact; whether/how it carries over to the x64 line is an open question for whenever that line matures enough to need its own LTS discussion, not decided here. |

## What LTS support actually means here

- **Availability** — an LTS release stays published (GitHub Releases, Nexus)
  for the duration of its support window, including the 4-week overlap after a
  successor is promoted.
- **Backported fixes** — a confirmed, severe regression found in the LTS itself
  (not "missing a feature the current dev line has") gets a targeted fix
  backported to it, rather than telling an LTS user to move to active
  development to get it.
- **No feature backports** — new features land in the active development line
  only. LTS is a stability commitment, not a parallel release track.
- **What ends after the support window** — availability and fix backports.
  The release itself isn't deleted from history; it's archived (unpublished
  from the active Releases list, kept accessible on request) the same way prior
  intermediate releases already are.
