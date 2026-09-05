# LTS Policy

This document explains which MW32011NCP releases carry long-term support
(LTS), how a release earns that status, and when support for an older one
ends. It exists so the policy is a stable, public commitment — not something
reconstructed from a GitHub Releases page or inferred from patch notes.

## Current status

**No release exists yet on the `-x64` line, so no LTS designation is active.**
The prior `-x86` line's own LTS history (its Current LTS and LTS candidate)
was withdrawn outright when that line was discontinued following MW3's
x86→x64 recompile — see [`README.md`](README.md) and
[`legacy-x86-docs/LTS_POLICY.md`](legacy-x86-docs/LTS_POLICY.md) for that
record. The process below governs the `-x64` line going forward, starting
fresh from its first shipped release.

## Why an LTS line exists at all

Most releases in this project's `0.x` line are still active development —
new systems landing, existing ones being corrected against live testing,
config schema still shifting. That's appropriate for an alpha, but it means
most releases are not a good target for someone who just wants the mod to
work reliably for a while without tracking every change.

LTS releases are the answer to that: a small, deliberately-chosen subset of
releases that get *extended* support — continued availability and, where a
real regression is found, a backported fix — after the rest of that
release's own minor line has moved on.

## The rule

**Every `0.x` minor line designates exactly one LTS release.** Not the first
release in that line, and not automatically the last — whichever release in
that line is judged stable enough after real-world use, following the
promotion process below.

**Only one LTS is supported at a time, with a 4-week handover overlap.** When
a new LTS is promoted, the previous LTS doesn't disappear immediately — it
keeps receiving support for 4 more weeks alongside the new one, so anyone
still on it has a real window to move over deliberately rather than being cut
off the moment a new LTS is announced. At the end of that 4-week overlap,
support for the previous LTS ends and it's archived; from that point on, only
the current LTS and the active development line receive releases.

**A release doesn't become LTS on the day it ships.** A candidate has to hold
up under real play for **4 weeks post-release** with no confirmed major
regression before it's promoted. Shipping a version is not the same claim as
"this is now the stable one" — the promotion is a separate, later decision,
made after enough people have actually played it.

**A release that ships with a feature violating this project's own hard-line
policies (see `CODE_STANDARDS.md`'s "Input Validation & Security" section —
most notably, never reading live gameplay-entity memory) is permanently
ineligible for LTS status, retroactively and for good, regardless of when
that's discovered.** A version that shipped with a disqualifying feature
can't retroactively stop having shipped with it. This isn't a stability
judgment — it's a standing floor independent of how any other part of this
policy evolves.

## Promotion process

1. A release ships as a normal, current-development release — no different
   from any other `0.x` release at launch.
2. It's the **candidate** for its minor line's LTS.
3. The candidate needs **4 weeks of real-world use with no confirmed major
   regression** — reports get triaged and fixed the same as any other bug,
   but a confirmed regression severe enough to reset the clock delays
   promotion until a corrected build has its own clean 4-week window.
4. Once that window closes clean, the candidate is promoted — see **Keeping
   this document current** below for exactly what has to happen in that same
   pass.
5. If a minor line ends (development moves to the next `0.x`) before any
   release in it accumulated a clean 4-week window, that line simply has no
   LTS candidate promoted from it — the previous line's LTS keeps serving
   until a later line produces one.

## Keeping this document current

**Every time a release is designated LTS, this file is updated in the same
pass — not as separate, deferred cleanup.** Concretely, promoting a release
means, in one pass:

1. Update the **Current status** section above: the newly-promoted release
   becomes Current LTS; the previous Current LTS becomes "In handover" with
   the date its 4-week overlap ends; the LTS-candidate note is cleared or
   refilled with whatever the next candidate is.
2. Once a previous LTS's handover window actually closes, remove it from the
   status section entirely — don't leave a stale "In handover" note sitting
   past its own end date.
3. Cross-check `re_notes/known_issues_x64.md` and `PATCHNOTES.md` for
   anything that now needs the same update — a promotion is a real,
   user-facing event and belongs in the patch notes for whichever release
   triggered it.

A stale LTS status is worse than no status — a reader has no way to tell the
difference between "this hasn't changed" and "this was never updated," so
treat any promotion that lands without this file being touched the same way
this project treats any other doc left silently out of date.

## What LTS support actually means here

- **Availability** — an LTS release stays published (GitHub Releases, Nexus)
  for the duration of its support window, including the 4-week overlap after
  a successor is promoted.
- **Backported fixes** — a confirmed, severe regression found in the LTS
  itself (not "missing a feature the current dev line has") gets a targeted
  fix backported to it, rather than telling an LTS user to move to active
  development to get it.
- **No feature backports** — new features land in the active development
  line only. LTS is a stability commitment, not a parallel release track.
- **What ends after the support window** — availability and fix backports.
  The release itself isn't deleted from history; it's archived (unpublished
  from the active Releases list, kept accessible on request).
