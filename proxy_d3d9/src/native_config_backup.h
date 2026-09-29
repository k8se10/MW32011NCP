#pragma once

// Real safety net for players2/config.cfg (2026-09-29). Real, live-confirmed
// incident this backs: a native engine crash (FAIL_FAST_INVALID_ARG,
// confirmed via a real WER crash dump -- iw5sp.exe's own "archive dvars ->
// config.cfg" write step, one frame past this project's own
// Hook_ComFrameBodyX64 dvar-write drain) left this file truncated mid-write.
// Every subsequent launch read the broken file back in, and the game kept
// crashing in a loop -- independent of anything else this project could
// find (confirmed live with RTSS closed, so not that either). Manually
// recovered once (backup + delete, letting the engine regenerate a fresh
// default) -- this makes that recovery automatic going forward.
//
// Call once, as early as possible in DLL_PROCESS_ATTACH (right after
// LoadModConfig -- see dllmain.cpp), before the native engine gets a real
// chance to read this file itself.
void BackupOrRestoreNativeConfigCfg();
