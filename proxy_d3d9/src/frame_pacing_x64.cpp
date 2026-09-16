// frame_pacing_x64.cpp -- issue #99's third in-mod FPS-limiter attempt, x64-only.
//
// ATTRIBUTION: the spin-wait/adaptive-correction algorithm below (QpcNow/
// QpcFrequency/LimitVisibleFrame and the deadline-advance formula) is ported,
// with real credit, from legoliamneeson/MW3_Standalone_D3D9_Project
// (github.com/legoliamneeson/MW3_Standalone_D3D9_Project, src/frame_pacing.hpp
// and src/frame_deadline.hpp) -- a real external reference implementation this
// project independently evaluated this same session (the "D3D9 optimizer repo
// evaluation" round). That project's own README states no redistribution
// license is asserted for this specific runtime source ("no new ownership or
// redistribution license is asserted for that supplied source"); credited
// here, in this project's own Credits section, and via a Co-Authored-By line
// on the commit that ports it, per direct instruction.
//
// WHAT IS AND ISN'T REUSED, AND WHY: the source project's own hook technique
// (Microsoft Detours, intercepting IDirect3DDevice9::Present/PresentEx/
// swapchain-Present on the live vtable, gated by an exact EXE build's
// hardcoded timestamp/image-size/RVA triplet) is NOT reused here -- this
// project already has a working, signature-scan-resolved per-frame hook
// (Hook_EndScene, overlay_hud.cpp; this project's own Present hook has been
// confirmed dead since 2026-07-15, most likely Steam Overlay silently taking
// that vtable slot) and a locked policy against hardcoded per-build addresses
// (CLAUDE.md SS5/SS10.3) -- reusing this project's own existing hook point and
// signature-scanned dvar accessor (GetDvarFloatX64, analog_input_hooks_x64.cpp)
// is both safer and less code than porting Detours + a build-identity check.
// What genuinely IS reused is the pacing algorithm itself: a high-resolution
// CreateWaitableTimerEx spin-wait with an adaptive wake-error correction term,
// reading (never writing) the game's own real com_maxfps dvar.
//
// WHY THIS IS A DIFFERENT ATTEMPT, NOT A RETRY (see mod_config.h's own
// framePacingEnabled comment for the short version): the FIRST attempt (issue
// #99) failed live testing for added input latency -- a blind fixed-interval
// wait with no adaptive correction. The SECOND attempt wrote the game's own
// com_maxfps dvar (SetDvarFloat) and was confirmed via a real FPS counter to
// only cap MENU framerate, not gameplay -- the engine evidently treats
// com_maxfps differently once connected/simulating than this project's own
// decompile assumed. This attempt does neither: it never writes com_maxfps
// (matches the source project's own README: "This project does not overwrite
// it"), and its wait uses the same adaptive wake-error correction the source
// project's own real implementation uses, not a blind Sleep().
//
// NOT YET LIVE-TESTED. Off by default (g_modConfig.framePacingEnabled).

#include <windows.h>
#include <cstdint>

#include "mod_config.h"

// Plain manual clamp/max, not std::clamp/std::max -- windows.h's own min/max
// macros (NOMINMAX not defined project-wide) would otherwise corrupt those
// calls, same fix already applied in dualsense_input.cpp's own DualSense_Poll.
namespace {
double ClampD(double v, double lo, double hi) { return v < lo ? lo : (v > hi ? hi : v); }
double MaxD(double a, double b) { return a > b ? a : b; }
}

extern "C" float GetDvarFloatX64_Exported(const char* name);

namespace {

// Ported verbatim from frame_deadline.hpp's own mw3_frame_deadline::Advance --
// late frames are released immediately and a new pacing phase starts there,
// rather than stacking up a full extra period of latency after a late frame.
double AdvanceDeadline(double previous, double now, double period, bool reset)
{
    if (reset || period <= 0.0) return now;
    const double next = previous + period;
    if (now >= next || next - now > period) return now;
    return next;
}

struct SmoothLimiterState {
    uint64_t qpcFrequency = 0;
    double nextDeadline = 0.0;
    int targetFps = 0;
    HANDLE timer = nullptr;
    double wakeErrorUs = 120.0;
    bool initialized = false;
};
SmoothLimiterState g_limiter;

uint64_t QpcNow()
{
    LARGE_INTEGER value{};
    QueryPerformanceCounter(&value);
    return static_cast<uint64_t>(value.QuadPart);
}

uint64_t QpcFrequency()
{
    if (g_limiter.qpcFrequency != 0) return g_limiter.qpcFrequency;
    LARGE_INTEGER value{};
    if (!QueryPerformanceFrequency(&value) || value.QuadPart <= 0) return 0;
    g_limiter.qpcFrequency = static_cast<uint64_t>(value.QuadPart);
    return g_limiter.qpcFrequency;
}

void ResetLimiter()
{
    g_limiter.nextDeadline = 0.0;
    g_limiter.targetFps = 0;
    g_limiter.initialized = false;
    g_limiter.wakeErrorUs = 120.0;
}

HANDLE GetLimiterTimer()
{
    if (g_limiter.timer != nullptr) return g_limiter.timer;
#ifndef CREATE_WAITABLE_TIMER_HIGH_RESOLUTION
#define CREATE_WAITABLE_TIMER_HIGH_RESOLUTION 0x00000002
#endif
    HANDLE timer = CreateWaitableTimerExW(nullptr, nullptr,
        CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_MODIFY_STATE | SYNCHRONIZE);
    if (timer == nullptr) timer = CreateWaitableTimerW(nullptr, FALSE, nullptr);
    g_limiter.timer = timer;
    return timer;
}

// Spin-wait with an adaptive correction term for how long SetWaitableTimer's
// own wake actually overshoots by on this machine, so the timer wait target
// shrinks over the session to land closer to the real deadline -- ported
// verbatim from LimitVisibleFrame (frame_pacing.hpp).
void LimitVisibleFrame(int targetFps)
{
    if (targetFps <= 0 || targetFps > 1000) {
        ResetLimiter();
        return;
    }
    const uint64_t frequency = QpcFrequency();
    if (frequency == 0) return;
    const double periodTicks = static_cast<double>(frequency) / static_cast<double>(targetFps);
    const double ticksPerUs = static_cast<double>(frequency) / 1000000.0;
    uint64_t now = QpcNow();
    auto& state = g_limiter;

    const bool reset = !state.initialized || state.targetFps != targetFps;
    state.nextDeadline = AdvanceDeadline(state.nextDeadline, static_cast<double>(now), periodTicks, reset);
    state.initialized = true;
    state.targetFps = targetFps;
    if (state.nextDeadline <= static_cast<double>(now)) return;

    constexpr double kSpinFinishUs = 55.0;
    for (;;) {
        now = QpcNow();
        const double remainingTicks = state.nextDeadline - static_cast<double>(now);
        if (remainingTicks <= 0.0) break;
        const double remainingUs = remainingTicks / ticksPerUs;

        const double headroomUs = ClampD(state.wakeErrorUs + 90.0, 180.0, 600.0);
        if (remainingUs > headroomUs + 650.0) {
            HANDLE timer = GetLimiterTimer();
            if (timer != nullptr) {
                const double waitUs = remainingUs - headroomUs;
                LARGE_INTEGER due{};
                due.QuadPart = -static_cast<LONGLONG>(waitUs * 10.0);
                const uint64_t before = QpcNow();
                if (SetWaitableTimer(timer, &due, 0, nullptr, nullptr, FALSE)) {
                    WaitForSingleObject(timer, INFINITE);
                    const uint64_t after = QpcNow();
                    const double actualUs = static_cast<double>(after - before) / ticksPerUs;
                    const double error = MaxD(0.0, actualUs - waitUs);
                    state.wakeErrorUs = state.wakeErrorUs * 0.92 + error * 0.08;
                    continue;
                }
            }
            if (!SwitchToThread()) Sleep(0);
            continue;
        }

        if (remainingUs > 180.0) {
            if (!SwitchToThread()) Sleep(0);
            continue;
        }
        if (remainingUs > kSpinFinishUs) {
            Sleep(0);
            continue;
        }
        YieldProcessor();
    }
}

} // namespace

void OnEndSceneFramePacingX64()
{
    if (!g_modConfig.framePacingEnabled) {
        return;
    }
    const int targetFps = static_cast<int>(GetDvarFloatX64_Exported("com_maxfps"));
    if (targetFps <= 0) {
        ResetLimiter();
        return;
    }
    LimitVisibleFrame(targetFps);
}
