// renderdoc_capture_x64.cpp -- added 2026-09-26, real GPU-side frame capture via
// RenderDoc's official in-application API, direct user instruction after this
// project's own CPU-side sampling profiler (self_sampling_profiler_x64.cpp) hit its
// fundamental limit: no thread showed sustained CPU work in either a live or a
// paused capture -- consistent with real background/worker threads being idle
// either way, which cannot distinguish "genuinely idle" from "correctly blocked
// waiting on the GPU." "how bout we do something weve never done on this proj,
// dump gpu to disk" -- this is that: a real, per-frame Vulkan command capture
// (a .rdc file, RenderDoc's own native format) showing every draw call, every
// resource, and real per-event GPU timing for one specific captured frame, openable
// in RenderDoc's own UI for direct visual/timeline comparison between a live-
// gameplay frame and a paused-menu frame -- the class of data this project's CPU
// thread sampler structurally cannot produce.
//
// WHY RENDERDOC, NOT A CUSTOM GPU TIMER: RenderDoc is the real, standard, widely
// used open-source (MIT) GPU debugger for Vulkan/D3D/OpenGL, and it ships an
// official "in-application API" specifically designed for exactly this use case --
// a target application loads renderdoc.dll itself and calls a small, stable,
// versioned C function-pointer API to trigger captures programmatically, with no
// external process attach, no debugger, and no relation to this project's own
// x64dbg/cdb instability (see known_issues_x64.md issue #4's newest round and the
// project's own persistent feedback_no_autonomous_live_debugger memory) -- this is
// a completely different, purpose-built mechanism, not a workaround for that one.
// `renderdoc_app.h` (vendored under proxy_d3d9/third_party/renderdoc/, MIT license,
// fetched verbatim from the official github.com/baldurk/renderdoc repo so the real
// function-pointer struct layout is byte-exact -- this ABI is safety-critical to
// get right, since a wrong field order would call an arbitrary wrong function) is
// the ONLY dependency; RenderDoc itself must be installed separately by the user
// (renderdoc.org, free, real, official) -- this project does not vendor or bundle
// the actual renderdoc.dll runtime.
//
// TIMING, THE PART THAT ACTUALLY MATTERS FOR THIS TO WORK AT ALL: RenderDoc's own
// documentation is explicit that RENDERDOC_GetAPI() must be called BEFORE creating
// any graphics API device/context, or its Vulkan layer cannot properly hook
// vkCreateInstance and capture will not work. The correct call site is therefore
// the TOP of this DLL's own exported Direct3DCreate9 forwarder (dllmain.cpp),
// BEFORE calling through to the real/DXVK Direct3DCreate9 -- DXVK creates its real
// VkInstance lazily inside that very call, so this is the latest point that still
// counts as "before". Deliberately NOT called from DllMain's own DLL_PROCESS_ATTACH,
// even though that would be earlier still -- this project has a real, live-
// reproduced precedent (TryInitStreamlineX64, see that function's own comment)
// for a DLL's own initialization work hanging the game when run from inside
// DllMain's loader lock. Direct3DCreate9 runs on the game's own calling thread,
// well after DllMain has returned and the loader lock has long since closed --
// same safety profile as every other post-DllMain hook in this project, while
// still satisfying RenderDoc's own "before any device/context" requirement.

#include <windows.h>
#include <cstdio>
#include "../third_party/renderdoc/renderdoc_app.h"

extern void LogFromController(const char* msg); // dllmain.cpp

namespace {
RENDERDOC_API_1_6_0* g_rdocApi = nullptr;
bool g_rdocInitAttempted = false;
}

void InitRenderDocX64()
{
    if (g_rdocInitAttempted) return; // Direct3DCreate9 could in principle be called
                                       // more than once; only ever try this once.
    g_rdocInitAttempted = true;

    // Try a bare "renderdoc.dll" first -- resolves if it's already loaded into this
    // process (e.g. the game was launched THROUGH RenderDoc's own capture tool) or
    // sits on PATH. Falls back to the real default install location if not, since
    // a standalone RenderDoc install does NOT add itself to PATH by default.
    HMODULE rdoc = GetModuleHandleA("renderdoc.dll");
    if (!rdoc) rdoc = LoadLibraryA("renderdoc.dll");
    if (!rdoc) rdoc = LoadLibraryA("C:\\Program Files\\RenderDoc\\renderdoc.dll");
    if (!rdoc) {
        LogFromController("[gpu-capture] RenderDoc not found (checked PATH and the default "
            "C:\\Program Files\\RenderDoc\\ install location) -- F11 GPU capture unavailable "
            "this session. Install RenderDoc (free, renderdoc.org) and relaunch to enable it.");
        return;
    }

    pRENDERDOC_GetAPI RENDERDOC_GetAPI =
        reinterpret_cast<pRENDERDOC_GetAPI>(GetProcAddress(rdoc, "RENDERDOC_GetAPI"));
    if (!RENDERDOC_GetAPI) {
        LogFromController("[gpu-capture] renderdoc.dll loaded but is missing its own "
            "RENDERDOC_GetAPI export -- unexpected, possibly a corrupted or unrelated DLL "
            "with the same filename. F11 GPU capture unavailable this session.");
        return;
    }

    int ok = RENDERDOC_GetAPI(eRENDERDOC_API_Version_1_6_0, reinterpret_cast<void**>(&g_rdocApi));
    if (!ok || !g_rdocApi) {
        LogFromController("[gpu-capture] RENDERDOC_GetAPI failed to resolve the 1.6.0 API "
            "(installed RenderDoc version may be too old). F11 GPU capture unavailable this "
            "session.");
        g_rdocApi = nullptr;
        return;
    }

    // Files land beside the game exe, same convention as the F9 self-dump and F10
    // self-profile tools -- RenderDoc appends its own frame index/timestamp/.rdc
    // extension to this template automatically.
    g_rdocApi->SetCaptureFilePathTemplate("gpucapture");

    int major = 0, minor = 0, patch = 0;
    g_rdocApi->GetAPIVersion(&major, &minor, &patch);
    char buf[256];
    sprintf_s(buf, "[gpu-capture] RenderDoc in-application API initialized (requested 1.6.0, "
        "real runtime version=%d.%d.%d) BEFORE Direct3DCreate9 -- F11 now triggers a real GPU "
        "frame capture (.rdc, openable in RenderDoc's own UI).", major, minor, patch);
    LogFromController(buf);
}

void TriggerGpuCaptureX64()
{
    if (!g_rdocApi) {
        LogFromController("[gpu-capture] F11 pressed but RenderDoc was never successfully "
            "initialized this session (see the [gpu-capture] line logged at startup for why) "
            "-- no capture triggered.");
        return;
    }
    // TriggerCapture() captures the very next frame automatically (its own Start/
    // EndFrameCapture pair, no manual bracketing needed) -- exactly one real, full
    // GPU frame, whatever state the game happens to be in at the moment of the
    // NEXT frame boundary after this call.
    g_rdocApi->TriggerCapture();
    LogFromController("[gpu-capture] F11: triggered a RenderDoc capture of the next frame -- "
        "check for a new gpucapture_*.rdc file beside the game exe once it completes.");
}
