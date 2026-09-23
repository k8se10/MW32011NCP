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
byte-for-byte identical to upstream `v3.1.1`. The first candidate
investigation (see `re_notes/known_issues.md` issue #1: a motion-blur
post-process pass in the sibling `MW32011NCP` project produced no visible
effect under this DXVK build) is resolved — the real bug was in
`MW32011NCP`'s own game-logic code, not this fork's own DXVK source, so no
patch landed here. A real, working native-Windows DXVK build toolchain
(MSYS2/MinGW-w64/Meson/Ninja/glslang) was set up in the course of that
investigation and stays in place as real groundwork for a future,
genuinely IW5-specific DXVK quirk.
