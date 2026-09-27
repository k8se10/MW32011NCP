# Handoff — real hardware tessellation (2026-09-27)

Direct request: "we need tessellation. thats the final step to this game looking beyond
ridiculous" -- then, on being told this is a genuinely large undertaking, "give a handoff
for the full real thing to the cloud agent." **This is the full real thing, not the cheap
displacement/parallax fake** discussed and rejected as the scope for this handoff.

## Ownership -- this is OUR fork, not a dependency

Work happens in `MW32011NCP/dxvk/` (`github.com/k8se10/MW32011DXVK`), a real,
history-preserving `git subtree` fork of `doitsujin/dxvk` this project owns and
ships. **Standing rules for this fork, from `CLAUDE.md`'s own `MW32011DXVK`
section -- read that section in full before starting, it is not optional
background**:

- Independent versioning/releases -- never bundled into or blocked by an
  `MW32011NCP` `-x64` release.
- Must never depend on any `MW32011NCP`-specific code, header, or convention --
  it has to build and run standalone, with or without `MW32011NCP` installed.
- Any genuinely IW5/MW3-specific patch (which tessellation for this game's own
  content absolutely is) must be gated behind real, independent runtime
  detection of which game binary/build is actually running (comparable to
  `MW32011NCP`'s own `game_exe_detect.h`, but a separately-implemented check
  living entirely in this fork) -- never a blind, unconditional change to
  DXVK's general D3D9 behavior. A general, opt-in, off-by-default capability
  that isn't MW3-specific (the earlier `dxvk.enableNvCudaInteropNative`
  precedent) does NOT need this gate -- decide which shape this is and say so
  explicitly in your own commits/docs, per that section's own 2026-09-24
  clarification.
- Build system is Meson (`meson.build`/`meson_options.txt`), **not**
  `MW32011NCP`'s MSVC/vcxproj toolchain. **A working native-Windows x64 Meson
  build for this fork has never been stood up** (CLAUDE.md: "unset up as of
  this entry") -- this is real, unstarted groundwork and almost certainly
  your first concrete task, before any tessellation code is written.
- License stays zlib/libpng (DXVK's own, unmodified) for anything that isn't
  a genuinely MW3-specific patch.
- Commit format `[type]: [description]`, no session-identifying URLs in
  commit messages (this project's own standing, previously-violated-once
  rule, `CLAUDE.md` §9) -- `Co-Authored-By: Claude ...` with no URL is fine.

## Why this is hard, precisely

`iw5sp.exe`/`iw5mp.exe` speak **Direct3D 9** (Shader Model 3) to whatever
`d3d9.dll` they load. D3D9 has **no tessellation stage at all** -- no hull
shader, no domain shader, no tessellator, nothing. `MW32011NCP`'s existing
`[Video] GraphicsApi=Vulkan` mode already routes the game through this fork's
DXVK build (D3D9-to-Vulkan translation), and Vulkan itself DOES support real
tessellation control/evaluation shader stages -- but DXVK's job is to
faithfully translate the D3D9 calls the game actually makes, and the game
never makes a tessellation call, because it doesn't know the concept exists.
**There is nothing to "unlock" here, unlike a DXVK-side translation bug --
this requires genuinely new rendering behavior the original game never had,
synthesized on top of geometry that was never authored with displacement/
tessellation in mind.**

Two structurally different levels of ambition, in increasing order of real
difficulty -- pick a real starting point, don't try to reach the top in one
step:

1. **Smooth/PN-triangle tessellation (Phong tessellation).** Subdivides
   existing triangles and bends the new vertices toward the true underlying
   normal-interpolated surface -- rounds off faceted low-poly silhouettes
   (gun models, character models, some world geometry) without needing ANY
   new texture data (no heightmap/displacement map required, since it only
   uses geometry+normals the game already has). This is the real, honest
   "first milestone" -- it's still a genuinely new Vulkan pipeline stage
   (hull+domain shaders) injected for content that never had one, but it
   doesn't also require solving "where does displacement data come from for
   a 2011 game with none."
2. **True displacement-mapped tessellation.** Needs a real height/
   displacement source per surface. MW3 has no displacement maps at all --
   this would mean either (a) synthesizing a plausible heightmap from each
   material's existing normal map at load time (a real, nontrivial signal-
   processing problem, and a source of real visual risk if done crudely), or
   (b) hand-authoring/curating displacement data per asset (does not scale,
   likely out of scope for an automated mod). Do not start here. This is the
   real "beyond ridiculous" payoff the user is asking for, but it is
   downstream of #1 actually working, not a parallel first step.

## Where the real interception point has to be found (unstarted RE)

Nobody has looked at this yet. The real research task, in order:

1. **Map exactly where/how the game's real draw calls reach DXVK for actual
   world/model geometry** (not UI/HUD, which is a separate 2D pass this
   project's own `MW32011NCP` renderer map already documents in
   `re_notes/x64_migration/renderer_architecture_map.md` -- read that file
   first, it's real, current groundwork from the same investigation family,
   just on the `MW32011NCP` side). The real per-frame render-command flow
   (frontend queues typed draw commands, a backend thread submits them) is
   already partially mapped there and is directly relevant context even
   though tessellation work itself lives in the DXVK fork.
2. **Confirm whether DXVK's own D3D9 frontend (`src/d3d9/` in this fork) has
   a clean interception point** to recognize "this draw call is real 3D
   world/model geometry, not UI" and redirect it through a NEW Vulkan
   pipeline (with real tessellation control/evaluation shaders bound) instead
   of DXVK's normal D3D9-equivalent pipeline -- this is the single riskiest
   unknown in this whole feature. It may require a genuinely new draw path
   inside DXVK's D3D9 device implementation, gated (per the ownership rules
   above) to only activate for this specific game/build.
3. **Determine what geometry classes are even safe to tessellate at all.**
   Character/weapon models, static world geometry, and any skinned/animated
   geometry (see `MW32011NCP`'s own renderer map -- IW5 skins models on the
   CPU via an SSE path, `r_sse_skinning`) are three structurally different
   cases with different real risk profiles; skinned geometry in particular
   may need special handling since a naive tessellation pass could visibly
   break animation-driven deformation if control points are computed before
   or after skinning incorrectly.
4. **A real, minimal, throwaway prototype**: get ONE simple, static,
   non-animated prop rendering with a trivial tessellation pass (even just
   flat subdivision with no displacement, to prove the pipeline works end to
   end) before attempting anything visually meaningful. This is the actual
   "hello world" for this feature -- do not skip to a broad rollout.

## Standing project conventions that still apply here

- **No hardcoded/unverified addresses** -- if this work ever needs to read
  real MW3-side state (dvars, geometry buffers) rather than staying purely
  inside DXVK's own D3D9-translation layer, it follows `MW32011NCP`'s own
  signature-scanning policy (`CLAUDE.md` §5/§10.3), not a hardcoded offset.
- **Opt-in, off by default**, same as every other structurally-significant
  feature this project ships (`[Video] GraphicsApi=Vulkan` itself, DLSS,
  motion blur, FSR, etc.) -- a new toggle, not an always-on behavior change.
- **LegacyD3D9 must be completely unaffected.** This is a Vulkan-path-only
  feature by construction (D3D9 has no tessellation stage, full stop) -- make
  sure no shared code path outside the Vulkan-specific draw redirection is
  touched.
- **SP-only to start**, matching every other Vulkan/DXVK/DLSS feature's own
  existing gate (`IsGraphicsApiVulkanModeAllowed()`, `MW32011NCP` side) --
  don't attempt MP until SP is proven.
- **Document real dead ends as thoroughly as real progress** -- this
  project's own `known_issues.md`-per-repo convention (status-line-first,
  dated rounds after) applies to this fork's own `re_notes/known_issues.md`
  too. If the interception-point research in step 2 above comes back
  negative, that is a real, valuable, completely acceptable outcome to
  report -- do not force a fragile hack to claim partial success.
- **Fresh Perspective threshold (`CLAUDE.md` §10.9)**: this is exactly the
  kind of investigation where five or six genuinely different attempts at
  the SAME interception-point angle failing is the signal to stop and report
  back for a real decision on how to proceed next, not to keep pushing a
  worn angle or to quietly downscope to something smaller without saying so.

## Explicitly out of scope for this handoff

- Displacement-mapped tessellation (level 2 above) -- flag it as a real
  follow-on goal in your own docs, don't attempt it this pass.
- Multiplayer (`iw5mp.exe`).
- Any change to `LegacyD3D9` rendering.
- Upstreaming anything to `doitsujin/dxvk` -- only relevant once/if a patch
  here turns out to be genuinely general-purpose rather than MW3-specific,
  per this fork's own charter; not a goal for this pass.

## Suggested first real commit

Stand up the Meson/MinGW-or-MSVC-compatible native-Windows x64 build for this
fork from a clean checkout (no tessellation code yet) -- confirm it produces
a real, working `d3d9.dll`-equivalent DXVK build byte-comparable in behavior
to the currently-vendored prebuilt v3.1.1 binary `MW32011NCP` actually loads
today. Nothing downstream of this is real until this fork can be built at
all.
