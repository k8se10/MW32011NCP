# MW32011DXVK — a fork of DXVK for Call of Duty: Modern Warfare 3 (2011)

This is `k8se10/MW32011DXVK` (github.com/k8se10/MW32011DXVK), a fork of
[doitsujin/dxvk](https://github.com/doitsujin/dxvk), the real, independently-
maintained D3D9/10/11-to-Vulkan translation layer. Everything else in this
directory is upstream DXVK, unmodified except where this file's own scope
below says otherwise — see `README.md`/`LICENSE` for DXVK's own real
documentation and licensing (zlib/libpng), unchanged.

## Why this fork exists

Forked 2026-09-23, direct instruction, mid-investigation into a real,
reproducible bug: with `MW32011NCP`'s own `[Video] GraphicsApi=Vulkan` mode
selected (see that repo's `re_notes/x64_migration/vulkan_dlss_pipeline_research.md`
for the full architecture), the game's own `[Video] MotionBlurEnabled` full-
screen post-process pass runs every frame (confirmed via diagnostic logging —
gates pass, the pixel shader compiles, `DrawFullScreenPass` is reached) but
produces **no visible effect at all**. Two real, source-grounded hypotheses
(a vertex-declaration/`SetFVF` state issue; a `D3DRS_FOGENABLE`/W-fog edge
case DXVK's own source explicitly flags as jank-prone for pre-transformed
`XYZRHW` vertices) were tried on the *game-side* mod code and both
**live-tested and disproven** — the pass genuinely runs correctly by every
external signal, and still produces nothing visible. That result is real
evidence the bug lives inside DXVK's own D3D9-to-Vulkan translation itself,
for this specific game's specific usage pattern — not something fixable by
changing how the game-side mod calls the D3D9 API.

**Direct instruction on scope, verbatim**: "the dxvk fork is the right call,
were not remaking dxvk, were fitting it into iw5s engine." This fork's job is
narrow and specific: find and patch the real, concrete quirk in DXVK's own
D3D9-to-Vulkan translation that causes this game's engine (IW5, Call of Duty:
Modern Warfare 3, 2011 — both the Campaign/Survival binary `iw5sp.exe` and,
eventually, Multiplayer's `iw5mp.exe`) to render incorrectly under DXVK. It
is explicitly **not** a general-purpose DXVK improvement effort, a rewrite,
or an attempt to outpace upstream — patches here should be as small and
targeted as the real root cause turns out to be, and anything genuinely
general-purpose should go upstream to `doitsujin/dxvk` instead, not live here
permanently.

## Standalone release model — real, direct requirement

**Direct instruction, verbatim**: "this repo should also be treat as
standalone from ncp as it will get it own releases despite being nested,
this means it must not be ncp reliant or native reliant. it must be smart
and know what version its being run on so it doesnt mess up the hooks."

Concretely, this means:

- **Independent versioning and releases.** This fork does not share
  `MW32011NCP`'s own `-x64` version numbers or release cadence. It ships its
  own tagged releases from this repo, on its own schedule, whenever a real
  patch here is ready — not bundled into or gated by an `MW32011NCP` release.
- **No dependency on `MW32011NCP`'s own code.** Nothing in this fork may
  `#include` or otherwise depend on any `MW32011NCP`-specific header, type,
  or convention. It must build and function as a real, standalone DXVK
  build usable by anyone, with or without `MW32011NCP` installed alongside
  it — the nested `git subtree` location in that repo is a convenience for
  this project's own development workflow, not a statement that this fork
  requires that repo to exist.
- **Must not assume it's the only thing touching this game's process.**
  `MW32011NCP` itself installs its own, separate set of real hooks
  (signature-scanned engine functions, `EndScene`/`Reset`/`CreateDevice`
  vtable hooks) into the exact same process this DXVK build would also be
  loaded into (see that repo's `TryLoadVendoredDxvk()`, `dllmain.cpp`, for
  how). **Any MW3/IW5-specific patch this fork ever adds must be gated
  behind a real, runtime detection of which game binary and build it is
  actually running against** — never a blind, unconditional change to
  DXVK's own general behavior. This protects two things at once: a
  MW3-specific quirk-workaround can never silently misfire against an
  unrelated game (or an unrelated build of this same game) this DXVK build
  might also end up loaded into, and any patch here can never interfere
  with `MW32011NCP`'s own hooks by assuming a process/game context that
  isn't actually true at runtime. The real, concrete mechanism for this is
  not yet built — `MW32011NCP`'s own `game_exe_detect.h`
  (`GetModuleFileNameA` against the process's own main module, compared
  case-insensitively against known binary names) is the closest existing
  precedent and real reference implementation for the same underlying
  problem, though this fork needs its own, independent version of that
  logic per the "must not be NCP-reliant" requirement above — real,
  scoped, not-yet-started work.

## Real status, 2026-09-23

- Forked and merged into `MW32011NCP` as a real, history-preserving
  `git subtree` at `dxvk/` (see that repo's `CLAUDE.md`/`AGENTS.md` "Nested
  components" entry) — full upstream commit history preserved, not squashed.
- **Kept deliberately separate from** the already-vendored, prebuilt,
  official DXVK v3.1.1 binary `MW32011NCP`'s own `[Video] GraphicsApi=Vulkan`
  feature actually loads today
  (`MW32011NCP/proxy_d3d9/third_party/dxvk/x64/d3d9.dll`, an unmodified
  upstream release binary). This fork is the real, buildable *source*, for
  once a genuine patch exists to build and ship; it does not replace that
  prebuilt binary until then.
- **Not yet built at all from this fork.** DXVK's own real build system is
  Meson (`meson.build`, `meson_options.txt`), not `MW32011NCP`'s MSVC/
  `.vcxproj`-based one — setting up a real, working build toolchain for this
  fork (Meson + a compatible compiler/linker toolchain for a native-Windows
  x64 D3D9-to-Vulkan build) is real, separate, not-yet-started groundwork,
  needed before any actual custom patch here can be tested.
- **The real root cause of the invisible-motion-blur bug is not yet found**
  inside DXVK's own source. Real investigation so far (all from the
  `MW32011NCP` side, read directly against this exact source): DXVK's
  `DrawPrimitiveUP` requires a non-null bound vertex declaration to succeed
  at all; `SetFVF` genuinely creates and binds a real, correctly-typed one
  (confirmed via source read, not assumed); `UseProgrammableVS()`/
  `UseProgrammablePS()` correctly route a fixed-function-vertex +
  programmable-pixel-shader combination (exactly what this pass uses)
  through DXVK's own supported fixed-function-vertex emulation path — this
  combination is a real, designed-for case, not inherently broken. The next
  real, unstarted step is a genuine live/debug session against this DXVK
  fork's own build (once a build exists) to observe the actual Vulkan calls
  DXVK emits for this specific pass, rather than continuing to reason about
  it from source alone.
