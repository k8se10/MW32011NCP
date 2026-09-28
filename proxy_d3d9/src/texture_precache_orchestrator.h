#pragma once

// Bulk first-run texture pre-cache orchestrator (2026-09-28) --
// re_notes/x64_migration/texture_upscale_cache_research.md. Direct
// instruction: "we want it to cahce all assets from the files themselves on
// first run displaying the progress modal immediately serving them through
// the whole session + next sessions (unless update etc)". Closes the real
// gap the runtime capture path (Hook_ImageFileLoadX64, analog_input_hooks_x64.cpp)
// can't reach on its own: world/level geometry textures never route through
// FindOrLoadAsset/Hook_ImageFileLoadX64 during an ordinary play session (or
// only the small fraction of a level actually walked through would) --
// bulk-extracting directly from the game's own zone\*.ff files is the only
// way to reach those.
//
// Real extraction path: a shipped copy of tools/iw5oat's own Unlinker.exe
// (this project's own OpenAssetTools fork, already proven against real
// retail zones -- see known_issues_x64.md), invoked as a real subprocess,
// image-only mode (--include-assets image --image-format IWI), against
// every real zone\english\*.ff and zone\dlc\*.ff file. A real, deliberate
// policy exception: tools/iw5oat is otherwise dev-only, never shipped to
// players -- see CLAUDE.md's own 2026-09-28 record of this decision.
//
// Real, current limitation, not hidden: many real zones still fail to load
// via Unlinker today (open, unresolved bugs in this fork's own zone parser,
// tracked in known_issues_x64.md) -- this orchestrator skips and logs a
// failing zone rather than aborting the whole pass, so coverage today is
// partial and grows automatically as those bugs get fixed in future
// sessions, with zero code changes needed here.
namespace TexturePrecacheOrchestrator
{
    // Checks the on-disk completeness marker (keyed on the current mod
    // version + TextureRenderRes) and, if missing or stale, starts a
    // dedicated background thread that enumerates every real zone file,
    // runs Unlinker against each, walks the real dumped .iwi files, and
    // queues each one to TextureUpscaleWorker for background upscaling --
    // entirely off the game's own thread. A no-op if the marker is already
    // current, if TextureRenderRes == 1 (upscaling itself disabled), or if
    // BulkTexturePrecache is disabled in config. Safe to call once at
    // startup (SP-only, after device creation) -- idempotent, does nothing
    // on a second call this session.
    void EnsurePrecacheStarted();
}
