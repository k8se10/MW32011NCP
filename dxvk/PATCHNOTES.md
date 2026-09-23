# Patch Notes — MW32011DXVK

Real, notable changes made in this fork, per release, on top of the real
upstream [doitsujin/dxvk](https://github.com/doitsujin/dxvk) this project is
forked from. This fork's own versioning and release cadence is independent
of the sibling `MW32011NCP` project's own `-x64` releases — see `README.md`
for the real reasoning. See `re_notes/known_issues.md` for the full
investigation/reverse-engineering trail behind each entry.

---

## Unreleased

No patches to upstream DXVK source yet — this fork currently ships
byte-for-byte identical to upstream `v3.1.1`. Real, active investigation
into the first candidate fix (see `re_notes/known_issues.md` issue #1: a
motion-blur post-process pass, real in the sibling `MW32011NCP` project,
runs correctly by every external signal under this DXVK build but produces
no visible effect) is underway; nothing has been isolated to a specific
DXVK source change yet.
