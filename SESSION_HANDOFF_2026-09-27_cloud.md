# Handoff — 2026-09-27 (local session out of tokens)

Continues the branch work from `claude/confident-mendel-hyoqkw` (DLSS ROUNDS 17-24, now fully
resolved/live-confirmed) plus this session's MP port work — all already merged to `main`
(commit `39beef2a2` and prior). PATCHNOTES.md Groundwork section numbering has just been fixed
(clean 1-59 sequential run, no dupes) and pushed.

## Remaining work (pick up directly, no need to re-derive)

1. **PATCHNOTES.md `## Unreleased` section**: the Summary paragraph at the top is stale — rewrite
   it to reflect the TRUE full v0.0.3-x64 scope: the complete DLSS/Streamline saga (black-viewport
   fix, ghosting fix, camera-matrix real-engine-view feed), the MP controller-pipeline foundation
   (menu nav, performance hooks: wait-coalescing/IWD-cache/shadow-activation/console-font-init
   skips), the real x64 dvar-write path (unlocks ForceAnisotropicFiltering/HighQualityShadows/
   HighQualityLighting + Custom Options screen), and any audio fixes noted elsewhere in the file.
2. Once the summary is accurate, rename `## Unreleased` → `## v0.0.3-x64 — <Stage> (2026-09-27)`
   and add a fresh empty `## Unreleased` section above it (standard release-process step, CLAUDE.md
   "PATCHNOTES.md structure").
3. **Update in-game modals** (`proxy_d3d9/src/mod_config.h`): bump `kModVersionString` to
   `0.0.3-x64`, update `kWelcomeFeatureList` to list the real new features from this release
   (stale list = false claim shown to every player, per CLAUDE.md).
4. Build-verify (`build_dlss.bat` in scratchpad, or plain vcxproj Release x64 rebuild), deploy,
   commit, push to `main` — no feature branches (standing rule).

## Notes
- Untracked files sitting in the working tree (NVIDIA license PDF, RenderDoc installer, a
  screenshot, a snapshot zip, streamline SDK zip, some x64_migration RE scratch .txt files,
  `tools/iw5oat/decompiled/`) — none of these are part of this handoff's scope, leave them alone
  unless asked.
- No cloud/background agent was reachable at handoff time (`ListAgents` returned none) — this file
  is the continuation point for whichever session picks this back up next.

Co-Authored-By: Claude Sonnet 5 <noreply@anthropic.com>
