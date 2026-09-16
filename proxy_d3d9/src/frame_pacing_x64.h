#pragma once

// Frame-pacing limiter, x64-only, 2026-09-16 -- issue #99's THIRD attempt at an
// in-mod FPS cap, ported (with credit, see frame_pacing_x64.cpp's own header
// comment) from a real external reference implementation this project
// independently evaluated the same session: legoliamneeson/MW3_Standalone_D3D9_Project
// (github.com/legoliamneeson/MW3_Standalone_D3D9_Project), its own
// src/frame_pacing.hpp and src/frame_deadline.hpp. That project's own hook
// technique (a Detours-based Present intercept keyed to one exact EXE build's
// hardcoded RVAs) is NOT reused here -- this project's own signature-scanning
// policy and existing EndScene hook cover the same need more safely (see the
// .cpp for the full comparison). What IS reused, credited, is the actual
// frame-pacing ALGORITHM: a high-resolution waitable-timer spin-wait with an
// adaptive wake-error correction term, reading the game's own real com_maxfps
// dvar (never writing it -- the second attempt's own confirmed failure mode).

// Called once per real frame, from Hook_EndScene (overlay_hud.cpp), right
// before the real EndScene/Present call-through -- a safe no-op when
// g_modConfig.framePacingEnabled is off or com_maxfps is 0 (uncapped).
void OnEndSceneFramePacingX64();
