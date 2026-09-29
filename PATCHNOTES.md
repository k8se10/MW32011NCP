# Patch Notes

All notable changes to the project, per release. See
`re_notes/known_issues_x64.md` for the full, actively-tracked issue list and
reverse-engineering trail behind each entry. The prior `-x86` line's own
patch history is preserved in
[`legacy-x86-docs/PATCHNOTES.md`](legacy-x86-docs/PATCHNOTES.md).

---

## Unreleased

**Summary:** Multiplayer's first real GAMEPLAY controller hook (not menu navigation or a performance fix): Sprint, Hold Breath, and CrouchProne auto-stand-on-sprint now work on `iw5mp.exe`, driven by a real, independently-verified MP twin of the shared kbutton case-dispatch function SP's own Sprint/Hold Breath/CrouchProne already depend on. Required standing up a full Ghidra analysis of `iw5mp.exe` from scratch (none existed before) to confirm the twin structurally, byte-for-byte, against SP's own dispatcher rather than trusting a signature-only match. Build-verified on both x64 and Win32; not yet live-tested. MP controller movement/look/Fire/ADS/Reload was built and largely working but is held back from this release over a real, unresolved jitter bug. Also: `SkipRedundantShadowActivation` (default ON since v0.0.3-x64) is confirmed to break close-up shadow quality and its default is reversed to OFF — existing v0.0.3-x64 installs need a manual config edit, see Fixed below.

### What's New
1. **Multiplayer: Sprint, Hold Breath, and CrouchProne auto-stand-on-sprint.** Mirrors SP's own final design exactly — the real `+sprint`/Hold Breath kbuttons are driven directly (native duration/recovery timer and any perk override apply automatically; vanilla keyboard Sprint is untouched), and standing up from crouch/prone on Sprint's rising edge uses the real native stance-toggle dispatch, not a raw state write. Required real, independently-verified MP data: the shared case-dispatch function every one of these three features calls into (`FUN_1400ce950`) was confirmed, via full decompile, to be a byte-for-byte structural twin of SP's own dispatcher (same activate/deactivate call-pair shape, same case grouping for Fire/Reload/ADS/Sprint+HoldBreath/CrouchProne, same 31-byte function prologue prefix) — not just a plausible address match. Real, MP-specific differences found and accounted for, not assumed to carry over from SP: the per-client struct stride differs (600 vs SP's 560 bytes, irrelevant here since this project always operates on the local player only), the CrouchProne toggle case numbers are shifted (MP `0x52`/`0x53` vs SP's `0x48`/`0x49`, MP's dispatcher has extra killstreak action-slot cases inserted earlier), and MP's real stance field is a genuinely separate global — not derivable from the ADS-toggle-flag's own address by SP's "+0x1c" trick the way SP's own is. See `re_notes/x64_migration/mp_port_plan.md` step 7 for the complete derivation and every resolved offset.
2. **Multiplayer: controller movement (left stick), look (right stick), and Fire/ADS/Reload — HELD BACK from this release.** Movement is still confirmed broken live (jittery/stepping) after three real fix attempts (local-client gating, a per-tick dedup, then a direct-set rewrite); the actual root cause is still unknown, not just unpolished, and shipping a known-broken MP gameplay control is worse than not shipping it at all. The install call site is commented out; the code stays in the tree, fully described below, for the next attempt. The real MP movement-tick function (`FUN_1400d0050`) was confirmed via decompile to write `forwardmove`/`rightmove` at the exact same `+0x1c`/`+0x1d` usercmd_t offsets this project's layout research already established for SP — a strong independent structural confirmation, not just an address-matcher guess. The same function's own real pitch/yaw angle accumulators were located via a full disassembly dump (previously an open, unresolved research gap — no `.pdata`-visible candidate existed) and identity-confirmed two independent ways (which native mouse-delta output feeds which accumulator, and matching SP's own pitch-then-yaw read order), so right-stick look now injects the same way SP's does — pre-hook, before the native call both consumes and packs the accumulators in one pass. Fire/ADS/Reload reuse the same anchor-resolved struct addresses Sprint/HoldBreath already depend on (What's New item 1), including the real "explicitly set the ADS-active flag" fix SP's own history already had to learn the hard way (the kbutton call alone doesn't drive actual ADS engagement). **Explicitly NOT included this pass, real known gaps, not silently dropped**: the sniper Fire/ADS notify-bind-dispatch fix SP needed — its own MP address hasn't been researched yet, so MP Fire/ADS may reproduce SP's pre-fix sniper-class regression; gyro-aim and the ADS-FOV zoom-magnitude nuance; and D-pad action slots, Weapnext, CrouchProne/Jump/Melee/Lethal/Tactical/Interact/Scoreboard, all still unresolved (MP's own action slots differ structurally — killstreaks, not the same layout as SP). **First live-test pass found and fixed two real bugs**: the movement-tick function's own first parameter is a genuine per-client index, not always the local player — this project's own injection was silently being applied to OTHER players' usercmds too, corrupting their movement (reported as "jitter everywhere") and plausibly corrupting team/ready state via stray Fire/ADS/Reload kbutton calls while navigating the team/class menu (reported as always landing in spectate) — fixed by gating every piece of injection on the local client's own index. Separately, `g_adsToggleFlagMP` was wrongly modeled as a simple boolean flag copying SP's own design; MP's real anchor calls that address through the actual kbutton activate/deactivate function instead (a second kbutton struct, not a flag), which made ADS a complete no-op until fixed to match. **Second live-test pass found two more**: the movement-tick function can genuinely fire more than once for the same real tick (MP's own client-side prediction, unlike SP's simpler cadence) — both LOOK and MOVEMENT are additive, so a repeat call within the same tick added the same delta twice, saturating (and hiding) on a pure-axis push but showing as a real discrete "stepping" jump on diagonal input, where values sit unsaturated — fixed with a per-tick dedup keyed on the real shared timestamp. And ADS look-slowdown was missing because the LOW-confidence FOV candidate flagged as an open gap turned out to be structurally the wrong shape to call directly (it tail-calls into a second function that consumes the FOV as an argument rather than returning it) — calling it directly would have silently read wrong data, not just done nothing; fixed with a safe capture hook on the real sink function instead. **Third live-test pass**: movement still felt "like it's emulating k+m... stepping and plain janky" even after the dedup fix — real, distinct root cause: unlike SP, MP's own native tick body also writes the movement bytes itself every call, from a real mouse-residual quantization path, and does not reliably settle at a clean 0 for controller-only play the way SP's baseline does — the additive design (ported verbatim from SP) was compounding our own delta onto that fluctuating native residual every call. Fixed by directly setting the movement byte from the current stick position instead of adding to whatever's already there, sidestepping the native residual question entirely; the accepted trade-off is that MP controller movement no longer combines with simultaneous keyboard movement, which was never a realistic use case. See `re_notes/x64_migration/mp_port_plan.md`'s own newest status update for the full trail.

3. **Runtime texture-upscale-cache: real parallelism, a one-time "expect hitching" notice, and a live progress bar.** Following live playtesting that found character/weapon textures visibly sharper but world-geometry textures never catching up (a real 1813-queued/361-processed-per-session backlog on a single background worker thread), the worker now runs 3 concurrent threads against the same queue -- safe because the model's own inference API (ncnn) already creates a fresh, thread-local `Extractor` per call and only reads the loaded network afterward, exactly ncnn's own documented concurrent-inference pattern. The old per-texture "[NCP] Upscaling .../Cached upscaled ..." toast pair (unusable once a session queues 1000+ textures) is replaced by a single persistent, live-updating status bar (bottom-left, "Caching textures: N/M (P%) [####----]"). A new dismiss-required modal fires once, the first time the real queued count crosses 100: explains hitching is expected, that pausing is recommended, that the process is genuinely one-time per texture (served instantly forever after, every future session too), and that anything already loaded this session needs a restart to visually update -- if left on screen 5 real seconds, the game pauses itself automatically (skipped if the player already paused or opened any menu). See `re_notes/known_issues_x64.md` for the fuller trail.

### Fixed
1. **REGRESSION FOUND AND REVERSED: `SkipRedundantShadowActivation` (default ON since v0.0.3-x64) breaks close-up shadow quality — default reversed to OFF.** Extended Campaign playtesting after v0.0.3-x64 shipped found close-up shadows genuinely degrading/breaking up compared to distant (LOD) shadows — backwards from what a truly redundant skip should ever be able to cause. Confirmed by direct live A/B: setting the toggle to `0` fixes the close-up shadow quality immediately, at the cost of the fps gain it was shipped for. The real mechanism (one specific call site out of ~40 real activator callers, very likely the per-light reactivation path for nearby dynamic lights) is understood; the exact reason skipping that call site degrades quality despite the hardware capability probe matching correctly is not yet understood. Compiled default reversed, config version bumped 46→47. **Anyone who already launched v0.0.3-x64 once needs to manually set `SkipRedundantShadowActivation=0` in `mw3ncp_config.ini`** — this project's own "an explicit value in the file always wins" config design means the version bump alone cannot retroactively override a value the file already has written. See `re_notes/known_issues_x64.md` issue #4's newest round.
2. **CRITICAL: real crash in the texture-upscale-cache feature, root-caused via a live WER crash dump, not guessed at.** `TextureUpscaleWorker::IsInFlight` could reach `EnterCriticalSection` on a still-uninitialized lock -- the new viewport-capture path (`Hook_SetTexture` -> `IsNameInFlight`) could call in before any load-time capture had ever run the worker's own lazy-init step, on a completely fresh launch. A related, separate race in that same lazy-init step (two different threads -- render thread vs. load thread -- both able to pass its own non-atomic "already started?" check at once) was fixed the same pass with `InitOnceExecuteOnce`, the real WinAPI primitive for exactly this. Also fixed this pass: a real memory leak in `TextureUpscaleNcnn::UpscaleRGBA4x` (the source alpha-plane buffer was only freed on the early-failure path, leaking on every successful upscale -- live-reported as `privateBytesMB` climbing past 10GB in one session) and a genuine native engine limit this feature was the first thing to ever exceed (a fixed 26MB image-decode scratch pool, real for vanilla asset sizes but too small for a single 4x-upscaled world texture -- widened via a new, signature-scanned `[Experimental] ImageScratchMB` config, default 256).
3. **The `[x64-capture-outcome]` diagnostic used the wrong raw format values, wrongly reporting the large majority of real, valid, already-working texture captures as failed.** Its own hand-rolled check compared against 0/1/3 instead of `TextureUpscaleIwi::Format`'s real values (DXT1=0xB, DXT3=0xC, DXT5=0xD) -- one real session's log showed 1164 "formatOk=0" lines, 1105 of which were genuinely valid DXT1/3/5 captures the real production parser (`texture_upscale_worker.cpp`) had already queued and upscaled successfully the whole time; this was a misleading diagnostic, not an actual pipeline gap. Fixed, and split into distinct `[x64-capture-outcome-ok]`/`-fail`/`-skip` tags so a real failure never needs to be told apart from a success by parsing trailing fields again.
4. **CRITICAL: the texture-upscale-cache's real worker parallelism (3 threads, shipped earlier this session) broke almost every real upscale job -- root-caused to the GPU (Vulkan compute) backend, not a memory or logic bug.** The bottom-left progress indicator was reporting nearly everything as failed; the real log showed `Extractor::extract failed` on virtually every job, from essentially the start of the session (not a late-session/memory-pressure pattern). Root cause: `RunInferenceOnTileRgb`'s own "each thread creates its own Extractor" design is ncnn's documented-safe pattern for CPU inference, but this feature runs with `use_vulkan_compute=true` -- concurrent GPU submission across threads needs its own synchronization ncnn doesn't provide for free. Fixed by serializing just the real GPU submission (create/input/extract) behind the same lock already used for the one-time model load; decode/tiling/compositing/file I/O around it, in the caller, stay fully parallel across the worker threads.
5. **Texture caching: `kTileMaxDim` raised 256->512, live-tested, then reverted back to 256 within the same session -- the "fewer round trips = faster" theory was wrong.** With GPU submission correctly serialized (item 4), total GPU compute time is roughly proportional to total pixels processed regardless of tile size -- bigger tiles just make each individual serialized call take proportionally longer, no net throughput win, and a real, worse cost: one large texture now occupies the single GPU slot for far longer, making the whole session look completely stalled. A live test confirmed this: 986 real jobs queued, ZERO completed. Reverted to 256; explicit `opt.num_threads` (real hardware core count) for ncnn's genuine CPU-side work kept.
6. **The real fix for both "criminally slow" and "everything looks stalled": the job queue is now inserted in ascending pixel-count order, not FIFO.** With only one GPU inference ever in flight at a time (item 4), a single large texture queued early could block every small, fast job behind it for its own full processing time. Small/common textures (UI, icons, most weapon textures) now jump ahead of large ones already queued, so completions happen steadily and visibly throughout a session instead of stalling behind whatever happened to load first; large world textures still get processed, just after the cheap ones already in line.
7. **CRITICAL: a second, separate crash -- a real, guaranteed `sprintf_s` buffer overflow in the Streamline/DLSS per-frame timing log, the same recurring bug class this project has now hit five times.** Live-tested after the file-handle-table fix below and still crashed, this time during a Campaign cutscene/loading transition -- a fresh WER crash dump's own call stack (`Hook_EndScene -> sprintf_s<200>`) pointed straight at it. The `[x64-streamline-timing]` diagnostic's literal format string is 277 bytes on its own, before any `%.3f` substitution -- guaranteed to overflow its 200-byte buffer on every single call, not an edge case. It only fires when a frame's own DLSS-related timing exceeds 1ms, which explains why it went unnoticed until a real frame-timing hitch (exactly what a cutscene/loading transition causes) finally triggered it. Widened with real margin (400 bytes).
8. **CRITICAL: found and fixed the real root cause of known issue #12's rare startup/gameplay crash -- a texture-substitution bug corrupting a shared native file-handle table.** The synthetic read-callback the texture-upscale-cache feature uses on every cache hit was writing a dummy "handle" value of `1`. Full decompile of the real native file-table close function confirmed it treats ANY nonzero value as a real table-slot index to potentially close -- only `0` is safely special-cased as "no real handle." With real sessions serving hundreds of cache-hit substitutions, every one of them was closing file-table slot #1 regardless of what real file (e.g. `config.cfg`'s own open) happened to occupy it at the time -- almost certainly the actual cause behind issue #12's `FAIL_FAST_INVALID_ARG` crashes, not just a contributing factor as previously suspected. Fixed by using the real native "no handle" sentinel instead. See `re_notes/known_issues_x64.md` issue #12's newest round.
8. **The texture-cache-building modal's auto-pause is now gated on real active gameplay, and its timer raised 5s -> 15s.**
9. **Session continuity for the texture-upscale-cache queue, plus a real, bundled basemap of every known Campaign+Survival texture.** The on-disk cache itself already persisted forever once a texture was cached, but the in-flight QUEUE never did -- a crash or early exit before the backlog finished lost every not-yet-processed job, only re-discovered if the player happened to revisit the same content again. A small manifest (`texture_upscale_cache\pending_manifest.txt`, just names, never the actual captured texture bytes) is now written periodically (every 30s, not per-event) and read once at startup; any name a prior session left pending jumps straight to the front of the queue next session, ahead of even same-size new work, so genuinely interrupted work actually finishes instead of restarting cold every launch. Also ships `texture_names_basemap.txt`, a real, bundled, 17,008-name list -- the deduped union of every already-cached and still-pending texture accumulated across extensive real Campaign+Survival play -- so a fresh install starts with that coverage already known instead of needing the same extensive playtime to rediscover it. The modal can show at the main menu too (its own weapon-preview model captures real textures), where auto-pressing pause would be meaningless or could back out of a menu the player is actively navigating -- now only fires once the timer AND the same real in-level+clcState "genuinely in a level" signal this project's visual-suite gates already use are both true.

### Groundwork
1. **Level asset-pool table resolved, plus a real, opt-in proactive texture preload trigger -- hijacks upscaled textures directly at level load instead of waiting for organic play.** RE into the native map/asset-loading pipeline found that image pixel data is lazily demand-loaded at first material-bind use, not eagerly at level load -- there's no earlier native "load everything" step to hook. Found and resolved (via real signature matches, not hardcoded addresses) a shared, fixed-size 42000-slot native asset table (every currently-registered asset of every type, populated at zone/level load) plus the real native "force-load this image now" function -- on every real map change, every not-yet-loaded image the level's own asset table references is now queued and drained a couple at a time per frame through the exact same capture pipeline the existing reactive hooks already use. Ships OFF by default (`[Video] ProactiveLevelTexturePreload=0`): the trigger function has a real side effect (decrementing a shared global counter) this project hasn't independently verified is always safe to invoke ourselves, so this needs a live session's own log confirmation before being promoted to default-on. **Its first live test found and fixed a real signature bug**: the asset-table lookup signature shipped without independently re-verifying uniqueness and matched two different functions (a near-identical inline hash-loop shared with the asset-creation function) -- extended to include the real divergent code and re-verified as a unique match before redeploying. See `re_notes/x64_migration/level_asset_pool_hijack_research.md`.

---

## v0.0.3-x64 — Alpha (2026-09-27)

**Summary:** The largest release in this project's history. Four headline threads, each live-confirmed: **native Vulkan rendering with NVIDIA DLSS/DLAA** — Campaign/Survival now render through the bundled `MW32011DXVK` fork by default, and DLSS/DLAA via NVIDIA Streamline works end to end as an opt-in, including above 100% render scale, after 24 rounds of investigation through a dozen stacked real root causes; **performance** — Activision's x64 recompile's render regression (issue #4) attacked from every side, headlined by "the 67 bug" (a render-scale-coupled blur/downsample loop: up to ~3.3x FPS at 250% render scale, and the long-standing "pause runs worse than gameplay" regression fully reversed), plus a lost shadow-activation capability gate, three redundant per-frame render-view reactivations and a redundant console-font reload, all default ON; **audio** — the "everything but my own gun sounds like it's in a room" reverb bug, root-caused to x64 replacing Miles Sound System with a from-scratch X3DAudio mixer, fixed and on by default; and **Multiplayer's first controller-pipeline work** — native controller menu navigation plus the performance fixes above ported to `iw5mp.exe` through verified signatures. Also in this release: the real x64 dvar **write** path (issue #6, open since the port began), new in-process diagnostics (F9 self-dump, F10 sampling profiler, F11 RenderDoc capture, GPU-inclusive frame timing), a standing crash-prevention tool for MinHook targets, and full Dome/Underground zone loading in `tools/iw5oat`. See `re_notes/known_issues_x64.md` for the complete investigation trails.

**Development break and LTS candidacy:** a planned one-week development break starts on release day (2026-09-27 to 2026-10-04), so this release can sit under real play with nothing new landing on top of it, as this project has done before. v0.0.3-x64 is the LTS candidate for the `0.0.x` line under [`LTS_POLICY.md`](LTS_POLICY.md): promotion still requires the policy's full 4-week post-release window with no confirmed major regression (2026-10-25 at the earliest), and the break doesn't shorten that window. Keep reporting bugs during the break — the LTS decision depends on them.

### What's New
1. **Native Vulkan rendering, on by default in Campaign/Survival.** `[Video] GraphicsApi` now defaults to `Vulkan` on `iw5sp.exe`: the game renders through the bundled `MW32011DXVK` fork (DXVK's D3D9-to-Vulkan translation, embedded inside `d3d9.dll` and extracted at launch), with a validated fallback to the system `d3d9.dll` if the bundled runtime is ever missing or corrupt. `GraphicsApi=LegacyD3D9` restores the previous path; Multiplayer always uses D3D9. See Groundwork items 1-4 and 42.
2. **NVIDIA DLSS/DLAA — opt-in, RTX GPUs, Campaign/Survival on Vulkan.** Set `[Video] StreamlineEnabled=1`; `[Experimental] DLSSModeX64` picks the mode (default `3`, Quality). Above 100% render scale, DLSS's upscaling modes can't run, so DLAA takes over automatically for that session (your config is never changed), with an on-screen notice each time it switches. Live-confirmed: stable evaluation, the correct scene input, and camera-motion ghosting fixed. Known, deliberate gaps: bloom and depth of field aren't applied to the DLSS image above 100% render scale yet, and independently moving objects have no per-object motion vectors yet. DLSS 5 Neural Rendering is groundwork only (detection, RTX 50-series). See Groundwork items 5-32.
3. **Major performance fixes, all default ON.** `PauseBlurStepCap`/`LiveBlurStepCap` ("the 67 bug"), `SkipRedundantShadowActivation`, `SkipRedundantMasterSequencerReactivation`, `SkipRedundantOrchestratorExtraCalls` and `SkipRedundantConsoleFontInit`. Measured live: Dome at 250% render scale went from 23.3 fps to a 76.1 fps average, the pause menu now runs faster than gameplay instead of slower, and the shadow-activation and render-view fixes add real measured gains on top (~33% and ~10% each). See Fixed items 1-3 and 8-9 and Groundwork item 42.
4. **World/AI audio no longer sounds like it's in a small room.** `[Experimental] ReverbWetScale` now defaults to `0.5`, with concurrent-voice normalization so overlapping gunfire no longer piles up reverb. See Fixed item 7.
5. **Multiplayer: controller menu navigation and the performance fixes.** D-pad + A/B and B-back now drive MP's native menus, and the blur-step caps, render-view skips, console-font skip, wait coalescing, `.iwd` read cache and memory-detection fix all install under `iw5mp.exe` too. In-game MP controller movement/look is not supported yet, and the MP-specific pieces are build-verified but not yet live-tested. See Groundwork items 50-55.
6. **`ForceAnisotropicFiltering`/`ForceHighQualityShadows`/`ForceHighQualityLighting` and the Custom Options screen's vanilla settings now have a real x64 write path** (Campaign/Survival). Build-verified, not yet live-tested — each write logs an `[x64-dvarwrite]` read-back line for confirmation. See Fixed item 11.
7. **Every startup modal now carries a VAC risk notice.** The welcome, possibly-outdated and Multiplayer modals state that online play is subject to VAC: no user of this mod has ever reported a ban, but the risk exists, and using the mod means accepting and acknowledging it. The modal panel grew to fit (text canvas 540 → 660px, panel 600 → 720px in 1080p design space, text buffers 1024 → 2048 bytes); the welcome modal still leaves ~3 lines of headroom, measured against the real modal font.

### Fixed
1. **Real fix: redundant console-font/UI-localization reload (~90-100ms per redundant call) now skipped when the engine's own "already initialized" state says nothing changed.** `FUN_140081900` (the native console/UI font-init function, loads `"fonts/consoleFont"` and its companion materials) has no "already done" check of its own — it always redoes the full reload. Of its two real native callers, one already respected the engine's own `DAT_14064fdf4` flag before calling; the other didn't, silently re-paying the full cost on every menu/level transition that reached it, several times per session. Live-timed via `QueryPerformanceCounter`: **962ms on the genuine first call, then a consistent ~91-102ms on every subsequent (redundant) call** — directly in the same magnitude as this issue's own already-documented 100-165ms main-thread-fallback frame spikes. New `[Video] SkipRedundantConsoleFontInit` (default ON) closes the gap for the caller that was missing the check; a genuine reset (real console-shutdown/`vid_restart`/language-change events, independently confirmed via new diagnostic hooks on the two real reset functions) still clears the flag and lets a real, necessary reload through normally. See `re_notes/known_issues_x64.md` issue #4's newest round.
2. **Real fix, LIVE-CONFIRMED ~10% framerate improvement: a confirmed-unconditional extra per-frame render-view reactivation the original x86 binary never had at this exact position, now skipped.** A full x86 chain trace (this and a prior session) found `FUN_14018a240` (the master per-frame render sequencer) re-activates render view/pass 1 via a full activator call at a specific structural position — right before the final overlay-list walk and `EndScene` — that x86's structurally identical counterpart (`FUN_004e0ab0`, confirmed via the same call chain, same position) never does at all; it only refreshes the already-bound view's viewport rect. Not gated behind anything on either architecture — this is a genuine, confirmed extra cost x64 introduced, not a missing capability check. New `[Video] SkipRedundantMasterSequencerReactivation` extends the already-shipped shadow-activation-skip mechanism (the shared render-view-activator hook now also matches this one specific call site by its own resolved return address, skipping forwarding only there) — **live-tested by the user the same day: a real, measured ~10% performance improvement, no visual regression reported.** Graduated from `[Experimental]` to `[Video]`, **default ON**, on the strength of that confirmation. See `re_notes/known_issues_x64.md` issue #4's newest round.
3. **Real fix, LIVE-CONFIRMED (compounding with the fix above for a real ~19-20% total improvement): two more confirmed-unconditional extra per-frame render-view-activator calls x86 never had, now skipped.** A full x86 chain trace found `FUN_14018e0d0` (the per-frame orchestrator) makes 3-4 direct activator calls where x86's structurally identical counterpart makes exactly 2 — the first two match x86's own baseline exactly, but two further calls (each gated by a real per-frame content flag, with no x86 equivalent at either position) are a genuine extra cost. New `[Video] SkipRedundantOrchestratorExtraCalls` extends the same shared activator-hook mechanism to skip both. **Live-tested alongside the fix above: another real ~10% improvement on top, no visual regression** — graduated to `[Video]`, **default ON**, the same way. A third, related fix for a different call pair in a different function (the scene-wide post-effect function's two "guaranteed" calls) was also attempted the same day, blindly skipping both unconditionally — that version caused real viewport corruption during live gameplay and was reverted before release. **Same day, the real root cause was found and a conditional fix built instead**: the activator (`FUN_1401dfd80`) has its own live dedup check at entry (skip only if the requested pass already matches the currently-active one) — the blind version wrongly assumed both calls were always redundant; the new version reads the same live state the activator itself reads and only skips when it's genuinely a no-op, falling through to the real call otherwise. Build-verified; not yet live-tested, ships off by default (`[Experimental] SkipRedundantScenePostfxGuaranteedCalls`) pending that confirmation. See `re_notes/known_issues_x64.md` issue #4's newest rounds for the full record.
4. **Motion blur was broken for controller (regressed by the v0.0.2-x64 "not controller-only" fix), now fixed for both controller and keyboard/mouse together.** That fix replaced motion blur's controller/gyro-only direct delta capture with a universal pre/post-native-call accumulator diff, intending to add real K+M support — it did, but broke controller: a large single-tick controller-stick delta doesn't round-trip through the native compressed usercmd angle-pack step the same cleanly as a small, natural mouse delta does. Fixed with a hybrid capture (`Hook_MovementTick`, `analog_input_hooks_x64.cpp`): the exact pre-regression direct controller/gyro values are used whenever the stick or gyro contributed this tick, and the diff technique is only used when neither did (pure mouse/keyboard motion, the case it's actually correct for). Live-confirmed fixed by the user, both input methods, on both `GraphicsApi` backends (LegacyD3D9 and Vulkan/DXVK) — see `re_notes/known_issues_x64.md` issue #2's newest round.
5. **Two real static-analysis (CodeQL) security alerts fixed.** An unsigned-multiplication-before-widening-to-`size_t` alert (a real overflow-before-widen pattern on a 32-bit intermediate, even though not reachable with attacker-controlled input in this specific case) and a format-string-usage alert, both closed with the safe, standard fix (widen before multiplying; pass the format string as a literal argument, not a variable). Two unrelated dev-tool build breaks found and fixed in the same pass.
6. **Ruled out DXVK as the cause of the above along the way** — a real DXVK build toolchain was stood up and used to test five separate DXVK-source-level hypotheses, all ruled out; the decisive test (K+M motion blur working correctly under Vulkan while controller didn't) proved the bug was never in DXVK's own D3D9-to-Vulkan translation. See `dxvk/re_notes/known_issues.md` issue #1 (now Resolved) for the full corrected record. The "VULKAN 60 FPS 16.6ms" corner text cited in Groundwork item 3 as "DXVK's own built-in indicator" is corrected here too — it's RivaTuner Statistics Server's own overlay, not DXVK's.
7. **LIVE-CONFIRMED ("perfection"): the "world/AI gunfire and explosions sound like they're in a room, my own gun doesn't" report root-caused and fixed.** A deep investigation (real live diagnostics, not guesswork) found the actual cause: **x64 replaced Miles Sound System (the closed-source middleware x86 uses) with a from-scratch reverb/3D-audio mixer built on Microsoft's X3DAudio API** — confirmed via an import-table diff (`mss32.dll` on x86, `X3DAudio1_7.dll` on x64) after `X3DAudioCalculate` turned up with zero references anywhere in the x86 binary. Live data then showed the player's own weapon-fire channel never reaches this new mixer at all (routed through a separate, dry path by design), while every other sound gets the level's `wet=0.9` reverb level multiplied straight into its output via a flat, unshaped formula with no cap — so N simultaneous reverberant sounds (e.g. several guns firing at once) genuinely summed to N times the reverberant energy of one. New `[Experimental] ReverbWetScale` scales the wet level actually used by the real mixer, reversibly and per-call (the real value is restored immediately after, never touched anywhere else), and is additionally normalized by `sqrt(concurrentVoiceCount)` so the reverb no longer piles up with overlapping sounds. Live-tested and confirmed correct by the user across both single-shot and overlapping-gunfire cases at `ReverbWetScale=0.5`, which is now the **default** (graduated the same day at direct instruction: live-confirmed fixes become defaults). It stays under `[Experimental]` because the "right" scale is a subjective listening-test value, not something RE alone can prove universal; `1.0` restores the unscaled mix. See `re_notes/known_issues_x64.md` issue #10 for the complete investigation trail.
8. **LIVE-CONFIRMED, direct user framing "biggest fix of the mod so far": a full reversal of the long-standing "pause runs measurably worse than live gameplay" regression, not just a fix to parity.** Traced the dominant `view=6`/`7` render-view-activator alternation (nicknamed "the 67 bug") to a real iterative separable-blur/downsample loop (`FUN_14018eec0`) shared by SSAO's downsample chain and the cascade-shadow softening pass, whose real substep count is computed from a formula that scales directly with the render-scale-driven requested resolution — genuine, well-structured math (real per-tap kernel-radius breakpoints), not a bug, so the render-scale coupling is by design. The actual fix is a deliberate trade-off enabled by a sharp observation: the pause menu already draws its own separate Gaussian blur over the dimmed background, so this substep loop's own output is completely invisible while paused. New `[Experimental] PauseBlurStepCap` caps the real substep count only while the pause menu is open — **live-confirmed: Dome went from 24fps to over 80fps while paused, little to no visual change, actually faster than live gameplay now.** A separately-tunable `[Experimental] LiveBlurStepCap` was shipped the same day to test the identical mechanism during real gameplay, where the quality trade-off is genuinely visible — **also LIVE-CONFIRMED the same session: "full fix 85 fps in gameplay on dome 250%," "it looks no worse either," "this is it, we finally fixed the x64 port."** A real, measured ~3.3x improvement (23.3fps → averaging 76.1fps, peaking at 85) on the single hardest map/render-scale combination this whole investigation used as its reference point, with no reported visual regression. See `re_notes/known_issues_x64.md` issue #4's "the 67 bug" entry for the complete trace.
9. **`PauseBlurStepCap` graduated to `[Video]`, default ON; `LiveBlurStepCap`'s default bumped to `3`; the now-obsolete "Render Scale Warning" modal removed.** Both blur-cap fixes from the entry above were confirmed to have no legitimate visual downside — `PauseBlurStepCap` moves out of `[Experimental]` entirely and is on for everyone by default, matching this project's own graduation convention. `LiveBlurStepCap` stays `[Experimental]` (a genuine quality/performance trade-off value, not a universally-safe one) but ships with its live-confirmed-good value of `3` as the new default rather than `0`/off. The old once-per-session "Above 200% InternalRenderScalePercent, severe stutter is a genuine native engine limit" warning modal is removed — that claim is no longer accurate now that the real cause (the blur/downsample loop above) has been found and fixed; README's own "Known Gaps" render-scale entry is updated to match. See `re_notes/known_issues_x64.md` issue #4's newest round.
10. **Pause-menu native "Back ESC" text flickering through, and our B Back glyph flickering with it: three causes fixed.** (1) The native ESC text was only suppressed while the controller counted as the active input, but our hardcoded pause Back glyph drew regardless, so any mouse movement past the deadzone left both on screen, flickering as the check flipped. The pause glyph now uses the same gate: exactly one of the two shows. (2) The pause glyph also required the gameplay tick to have been stale for 250 ms, a timing stand-in for a pause flag (`cl_paused` doesn't work on x64). That was false for the first 250 ms of every pause and on any late tick, handing the glyph back to whichever native Back draw came next, including offscreen blur-pass copies at other positions. It now uses menu-active, in-level and the real topmost menu name (`pausedmenu`). (3) The menu-hint pool kept hold+fade state per slot INDEX while re-appending hints in per-frame request order, so any change in order moved the Back glyph to a slot whose fade restarted at 0 while its old slot drew a stale fading copy. Hints now keep their own slot and fade state for as long as they're requested or fading (this also applies to every other menu corner hint). Build-verified on the real MSVC toolchain (0 errors, 0 warnings); not yet live-tested. See `re_notes/known_issues_x64.md`, "FIX SHIPPED, 2026-09-27 -- pause-menu Back glyph".
11. **The real x64 dvar WRITE path found and built — `ForceAnisotropicFiltering`/`ForceHighQualityShadows`/`ForceHighQualityLighting` and the Custom Options screen's vanilla-setting writes were dead on x64 since the port began (v0.0.2-x64 Fixed item 7); every one of them now has a real, type-correct write path.** Closes `known_issues_x64.md` issue #6 and `x64_feature_parity_audit.md` row #64, both open since the very start of the x64 migration. Real RE found by walking DOWN from two known write entry points (the menu-script `setdvar` handler, and its own resolver chain) instead of UP from the already-known `Dvar_FindVar` getter cluster — the earlier search missed the setters entirely because `Dvar_FindVar` has a second, 5-byte JMP-thunk entry point with 30 more callers, and the real setters (`Dvar_SetBool`/`Dvar_SetInt`/`Dvar_SetFloat`/`Dvar_SetFromStringFromSource`, all funneling into one real sink, `Dvar_SetVariant`) sit further down the same function cluster. Two real, separate traps found and fixed in the same pass: (1) `Dvar_SetVariant` silently drops writes to any flagged dvar from any thread but the engine's own main thread, with no error — this project's own callers all run on the render/input threads, so every write is now queued and applied on the main thread via a hook on the `Com_Frame` body (fires every frame, in menus and gameplay alike, right before the engine's own `config.cfg` archive write, so an applied write is persisted the same frame); (2) the typed setters don't convert between types — calling the float setter on an int dvar passes a raw string pointer as the value and the domain check silently rejects it, which is exactly why `ForceAnisotropicFiltering` (`r_texFilterAnisoMax`/`Min`, real int dvars) could never have worked even with a naive setter. Every write now dispatches on the dvar's real type tag and logs a read-back line (`[x64-dvarwrite] name = value -> read-back … : OK/MISMATCH/SKIPPED/FAILED`) so a rejected value is visible, not silent. The F4 `ai_disableSpawn` glyph-editor toggle rides the same queue now too, replacing its own hardcoded addresses, and works in the pause menu for the first time. **Independently re-verified before merging**: all five new signatures (four setters plus the `Com_Frame` body) confirmed to match exactly once in the real `iw5sp.exe` binary and land on genuine function starts per the PE's own `.pdata` unwind table — this project's own standing safety requirement for any new signature, checked directly rather than taken on faith. **SP only for now** — MP's own `Com_Frame` body has the same real setter signatures but hasn't been RE'd, so MP writes are rejected with a logged reason rather than silently queued forever. Build-verified (0 errors, 0 warnings — a handful of unrelated, pre-existing warnings elsewhere in the file were checked and confirmed to predate this change) after independent signature verification; not yet live-tested. **Not yet touched, a real decision deliberately deferred**: `r_ssao` (the complete, previously-dormant native SSAO pipeline found in v0.0.2-x64's Groundwork item 7) is writable through this same path now, but turning it on changes real rendering behavior and hasn't been done without direct go-ahead. See `re_notes/x64_migration/dvar_write_path_x64.md` for the full trail.
12. **The modal queue and config-value validation report, both started as groundwork last session, are finished and build-verified for the first time.** The modal queue (`overlay_hud.cpp`): `ShowOverlayMessageUntilDismissed` used to silently replace whatever dismiss-required modal was already on screen — a config error raised at device creation could wipe the once-per-version welcome modal, and the player would only ever see one of them. It now queues (FIFO, capacity 8, duplicates skipped, overflow logged) behind an `SRWLOCK` held only for plain memory work, never across GDI/D3D calls, so a `DllMain` caller can raise a modal without risking a loader-lock deadlock against the render thread. Config validation (`mod_config.cpp`): every value `LoadModConfig` used to silently correct — an out-of-range float/int, a non-numeric `ReadBool`/`ReadFloat`, an unrecognized enum string (`GraphicsApi`/`GlyphStyle`/`ButtonLayout`/`StickLayout`), a negative `ReadUlong` that used to wrap to ~4 billion ms — is now logged (`[config][invalid]`) and, if anything was corrected, shown in one dismiss-required modal listing what changed and why, so a player who wrote `StreamlineEnabled=true` (silently read as off) or `DLSSModeX64=9` finds out instead of wondering why the setting did nothing. Every existing manual clamp (`if (x < lo) x = lo;`) in `LoadModConfig` now goes through the same `ClampFloatSetting`/`ClampIntSetting` helpers so it reports too. **New the same pass**: a real DLSS hardware-support check (`slIsFeatureSupported`, resolved best-effort alongside the SDK's other exports) in `RegisterDxvkVulkanDeviceWithStreamline` — `StreamlineEnabled=1` on a non-RTX GPU, an out-of-date driver, or with hardware-accelerated GPU scheduling disabled used to fail silently somewhere further down the chain with nothing but a log line; it now shows one dismiss-required modal naming the real reason (once per process, not re-shown on a device reset) and cleanly disables DLSS for that session while leaving the rest of Vulkan/DXVK rendering unaffected. Build-verified (0 errors, 0 warnings) — the modal queue and config validation were carried over not yet built with MSVC; both compile and link clean on the first pass. Not yet live-tested.
13. **Two real, pre-existing Win32 (`iw5mp.exe`'s own build target) build breaks found and fixed while build-verifying the testing aid in Groundwork item 59 below.** `overlay_hud.cpp` called two x64-only functions (`RunDlssEvaluateAndCompositeX64`/`RunDlssMainMenuWarmupOnceX64` from `TriggerMotionBlurFromEngineHook`, and `GetCurrentJitterProbeCandidateNameX64` from `DrawJitterProbeOverlayIfEnabled`) without the `#if defined(_M_X64) || defined(_WIN64)` guard every other x64-only cross-file call in this file already uses — the first broke the Win32 compile outright (C3861, the declaration itself was correctly guarded and so wasn't visible), the second broke the Win32 link (LNK2019, an unguarded declaration with no Win32-side definition to satisfy it). Both had shipped in the merged v0.0.3-x64 work without a Win32 build ever having been run against them. Fixed by guarding both call sites (and the jitter-probe declaration) the same way the rest of the file does; `DrawJitterProbeOverlayIfEnabled` is a clean no-op on Win32 now, matching the probe itself being x64-only. Win32 Release now builds clean (0 errors, 0 warnings) again.
14. **CRITICAL config bug found and fixed, direct report: "a lot of our new config options arent self geenrating in the config."** 13 real config keys added across this release's DLSS/performance work (`SkipRedundantShadowActivation`/`ConsoleFontInit`/`MasterSequencerReactivation`/`OrchestratorExtraCalls`, `PauseBlurStepCap`, `SkipRedundantScenePostfxGuaranteedCalls`, `ReverbWetScale`, `LiveBlurStepCap`, `OcclusionLodScaleFix`, `DLSSModeX64`, `DlssNeuralRenderingEnabled`, `GpuCaptureEnabled`, `GpuSyncTimingLogging`) were wired into `LoadModConfig`'s own read calls but never added to `WriteDefaultConfig`'s template — exactly the failure mode this project's own `kCurrentConfigVersion` comment history already documents and warns against (a missing key silently never appears for anyone already on the current schema version), hit 13 times in a row this release because none of these additions bumped the version. The real consequence went beyond a merely-missing key: for any existing user whose ini ever gets rewritten by a migration, `WriteDefaultConfig` had no template line to carry a manually-set value for any of these 13 keys forward, so a player's own customization would be silently dropped back to the compiled default on the next load — the actual "auto-migrate is replacing custom settings instead of preserving them" symptom reported. Fixed: all 13 keys added to the template, each sourced from `g_modConfig` (never a hardcoded literal, so a real value already loaded from the old file is what gets written back), and `kCurrentConfigVersion` bumped 45→46 so every existing installation actually receives this fix on its next launch, not just fresh installs. Build-verified on both x64 and Win32 (0 errors, 0 warnings each). Not yet live-tested.

### Documentation
1. **`LICENSE` now separates NVIDIA's components from the project's own grant.** The Third-party components section lists the MIT-licensed Streamline SDK headers, and states that NVIDIA's signed runtime binaries (`sl.*.dll`, `nvngx_*.dll`) are not covered by this project's license: they are governed only by NVIDIA's RTX SDKs License, redistributed unmodified in object-code form, and licensed for NVIDIA GPUs only. The README credit, which still said the binaries would be vendored later, is updated to match.
2. **`LTS_POLICY.md` is now this project's single, authoritative statement of what it does and does not support — every other doc, README included, points here instead of restating it.** Three concrete policy points recorded explicitly for the first time: Steam retail only (and, now that all 4 tracked netcode vulnerabilities are resolved, no remaining reason to ever reconsider that); the `-x86` line is **permanently** discontinued, not just unsupported — it will not be rebuilt or re-released under any name or framing, `legacy-x86-docs/` is historical reference only; and cracked/pirated copies are never supported, on either architecture, under any circumstances. The README's own Compatibility section is trimmed to a short pointer at it, so there's one source of truth instead of two copies to keep in sync.
3. **`LTS_POLICY.md`'s Current status corrected, and v0.0.3-x64 recorded as the `0.0.x` line's LTS candidate.** The section still said no `-x64` release existed, although v0.0.1-x64 and v0.0.2-x64 had both shipped; it now names this release as the candidate, the 4-week window it has to clear, and the one-week development break that starts on release day.
4. **This release's notes reorganized for readability.** Groundwork entries from parallel work had duplicated and restarted numbering (two runs of items 18-29, a second run from 19, then 39-39h in reverse order); they are now grouped by theme and numbered once, with every internal item cross-reference updated to match. Entries that shipped as `[Experimental]`/off and were graduated later in the cycle now state their real default.

### Groundwork

#### Vulkan, DXVK and DLSS
1. **`[Video] GraphicsApi` config selector added** (`LegacyD3D9` default / `Vulkan` opt-in) — the locked architecture decision for native Vulkan/Streamline/DLSS support. **SP-only, direct instruction**: Vulkan mode only actually takes effect under `iw5sp.exe`, enforced independently at two points (`CreateDevice` time and before `d3d9.dll` even loads) — Multiplayer holds more VAC risk for a QoL feature that isn't essential there, so this stays SP-only until real precedent exists.
2. **Real DXVK (v3.1.1) vendored and wired to load automatically** when Vulkan mode is selected — the real, maintainer-confirmed integration pattern (a wrapper calling DXVK's own `Direct3DCreate9` directly, not export-forwarding to a renamed DLL), with a validated fallback to the real system `d3d9.dll` if the vendored build is ever missing or corrupted, so a bad DXVK file can never take the whole game down.
3. **First live confirmation: DXVK's D3D9-to-Vulkan translation genuinely works against this game.** With `GraphicsApi=Vulkan` set, the game launches straight to the main menu correctly rendered, cursor working, a stable 60fps/16.6ms — RTSS's own corner overlay confirms Vulkan is actually the active backend. This is the real go/no-go milestone the rest of the Vulkan/DLSS roadmap depends on; deeper gameplay/Survival/visual-suite-compatibility testing is still needed before this is considered broadly stable, and no Streamline/DLSS integration exists yet on top of it.
4. **`MW32011DXVK` fork created** (`github.com/k8se10/MW32011DXVK`, `doitsujin/dxvk` forked and merged in as a real, history-preserving `git subtree` at `dxvk/`), with a real, working native-Windows build toolchain (MSYS2/MinGW-w64/Meson/Ninja/glslang) stood up from scratch — real groundwork for a future genuinely IW5-specific DXVK patch, not itself shipping any DXVK source change yet. See `dxvk/README.md`.
5. **The real native projection-matrix-build function found and live-confirmed** (`FUN_1401e13e0`) — the jitter-injection hook point the future DLSS/Streamline and FSR 3.1 integrations both need. A read-only diagnostic hook (`Hook_ProjectionMatrixBuild`) confirms the signature resolves correctly and the real computed matrix matches the expected D3D9 perspective-projection shape during actual play; the real jitter write itself is intentionally not yet wired — the matrix's full byte layout (whether it's a plain 4x4 or a packed/compressed representation) isn't fully mapped yet, and writing to an unconfirmed offset risks corrupting live render state. See `re_notes/known_issues_x64.md` issue #2's newest round and `re_notes/x64_migration/vulkan_dlss_pipeline_research.md` item 2.
6. **NVIDIA Streamline SDK vendored and initializing (SP, `GraphicsApi=Vulkan` only, opt-in via `StreamlineEnabled`).** The SDK headers are committed under `proxy_d3d9/third_party/streamline/`; the signed runtime binaries are not (licensing) and must be vendored locally. `slInit()` runs from `Hook_CreateDevice` rather than `DllMain` (calling it from `DllMain` hung the game under the loader lock) and is live-confirmed succeeding. DXVK's Vulkan instance/device/queue are then registered with Streamline via `slSetVulkanInfo`, read through DXVK's own `ID3D9VkInteropDevice` interop interface. Registration is refused, with a log line, if DLSS reports needing extra Vulkan queues, because DXVK creates none for Streamline. **`slSetVulkanInfo` is now live-confirmed succeeding too** (updated from this item's earlier "not yet built or live-tested" wording) — see item 8 below for the full trail. Resource tagging is the next unstarted step. See `re_notes/x64_migration/vulkan_dlss_pipeline_research.md` section 2.7 and section 4.4.
7. **Stage 2 (per-object) motion-vector plan corrected: IW5 skins models on the CPU.** The `skin model` render stage selects an SSE CPU-skinning path via the `r_sse_skinning` dvar, so there are no bone matrices in shader constants to capture. Skinned-mesh velocity will come from diffing each skinned surface's CPU-skinned output against its previous frame instead. The separate `+0x164` "bone palette" lead is weakened: its only visible writer copies a view-parameter-shaped record. Also found: stock DXVK 3.1.1 disables the extensions DLSS needs on native Windows and has no device-import API, so DLSS goes through the `MW32011DXVK` fork (see `dxvk/PATCHNOTES.md`). See `re_notes/x64_migration/vulkan_dlss_pipeline_research.md` items 15-16.
8. **The full DXVK-fork/Streamline chain now works end to end, live, for the first time — through `slSetVulkanInfo`, `slGetFeatureRequirements(DLSS)`, and the start of real per-frame tracking.** A dense same-day pass: (a) the DXVK fork build and all four Streamline/NVIDIA binaries are now embedded inside `d3d9.dll` itself as resources and re-extracted fresh to `%LOCALAPPDATA%\MW32011NCP\runtime_x64\` on every launch, replacing loose files a player had to manually place in the game folder (direct instruction: "we shouldnt need to have extra dlls in the game folder. it should all be inside our dll"); (b) a real jitter-injection shadow-pass gating attempt (return-address-based) turned out to be broken and was replaced with a simpler, correct fix — see item 5; (c) a second `MW32011DXVK` fork patch, `DXVK_VULKAN_LOADER_OVERRIDE`, lets DXVK's Vulkan loader route through Streamline's own `sl.interposer.dll` (needed for its mandatory swapchain hooks); (d) a real launch-time bug (`ERROR_SHARING_VIOLATION` extracting `sl.interposer.dll` twice in one launch, once DXVK had already loaded it) was caught and fixed live; (e) **live-confirmed on a real run**: `VK_NVX_binary_import`/`VK_NVX_image_view_handle` both report `1` (previously `0`), `slGetFeatureRequirements(DLSS)` succeeds (zero extra queues needed), and `slSetVulkanInfo()` succeeds; (f) real per-frame `slGetNewFrameToken()` tracking wired into the confirmed once-per-frame `Hook_EndScene` hook point, the next required step before resource tagging. `DXVK_VULKAN_LOADER_OVERRIDE` itself is build-verified only, not yet live-tested against real swapchain interception. See `dxvk/re_notes/known_issues.md` issue #2 and `re_notes/x64_migration/vulkan_dlss_pipeline_research.md`.
9. **View-matrix RE complete and LIVE-CONFIRMED: real camera position + forward/right/up basis vectors found, mathematically verified, and now driving working camera-relative motion tracking.** A real per-frame camera-context block, wholesale-copied into the same render-state struct the already-known projection matrix lives on, was found to contain camera position and an orthonormal forward/right/up basis — confirmed not guessed: all three vectors are unit-length, mutually perpendicular, and "right"'s exact direction matches the textbook `cross(worldUp, forward)` construction. A real per-frame call-pattern investigation (a live diagnostic burst capture) found the actual per-frame structure: ~13 different callers share one identical render-state struct per real frame, followed by 2 calls with position read as exactly zero (a separate, non-camera UI pass) — the originally-assumed "two-call shadow/main split" turned out not to be the live per-frame path at all, so both the jitter shadow-pass gate and the new camera tracking were switched to a simpler, correct check (skip zero position, let real camera data through). New `streamline_camera_x64.cpp` builds a real camera-to-world matrix every frame and tracks genuine previous-frame history, calling Streamline's own real `calcCameraToPrevCamera` reference math. **Live-confirmed**: smooth, physically plausible camera-relative motion values as the player moved and looked around, zero crashes. Honest, not-yet-resolved: whether shadow-map generation shares this same struct (and would still pick up the jitter offset) remains genuinely unknown; a correctly-formed projection matrix for `slSetConstants` also isn't built yet — this engine's own native projection-matrix layout is confirmed nonstandard. See `re_notes/x64_migration/vulkan_dlss_pipeline_research.md` item 17.
10. **All 4 real resource tags DLSS requires are now wired end to end, closing the gap item 8 above stopped just short of.** Following the real per-frame `slGetNewFrameToken()` tracking item 8 landed: the real depth buffer's Vulkan image is resolved via DXVK's own interop and tagged (`kBufferTypeDepth`, after fixing a real `D3DERR_NOTFOUND` from polling `GetDepthStencilSurface` from the wrong hook point — moved to `SetDepthStencilSurface` itself, which resolves correctly); a missing `eUseFrameBasedResourceTagging` preference flag was found and fixed (a real `eErrorInvalidIntegration` Streamline was reporting); the same proven pattern was then generalized to the real internal-render-scale-resolution color input buffer (`kBufferTypeScalingInputColor`, with a filter added so it doesn't fire on every unrelated `SetRenderTarget` call), a real owned output texture at native resolution (`kBufferTypeScalingOutputColor`), and the motion-vectors buffer (`kBufferTypeMotionVectors`) built from the real camera-to-world/motion-tracking pipeline (item 9). `slSetConstants` (item 11) sends the real per-frame constants on top of this. See `re_notes/x64_migration/vulkan_dlss_pipeline_research.md` item 17.
11. **`slSetConstants` wired — a full, real `sl::Constants` struct is now built and sent every frame, the last piece before `slEvaluateFeature` itself.** Closed a real, previously-flagged gap first: the motion-vectors buffer is now zero-filled via `IDirect3DDevice9::ColorFill` right after (re)creation, instead of holding D3D9-undefined content — this matters now specifically because `Constants::motionVectorsInvalidValue` (set to the matching `0.0f`) is a real, consumed field once `slSetConstants` runs, not a harmless unused one. The struct itself combines everything built so far this session: the real projection matrix and camera-to-world basis, plus `clipToPrevClip`/`prevClipToClip`, built by manually replicating Streamline's own reference math (`sl_matrix_helpers.h`) against this project's own real previous-frame tracking rather than that reference function's own static, "not for production use" cache. `cameraFOV`/`cameraAspectRatio` are now correctly passed in radians (`sl_consts.h`'s own documented unit). `jitterOffset=(0,0)` is an honest value, not a placeholder — no real 3D-geometry jitter exists yet (the one confirmed jitter target found this session is a 2D compositing matrix, unrelated to the 3D scene). `cameraMotionIncluded=eFalse` (this feature's own Stage 1, camera-only design). Build-verified (0 errors), deployed; **not yet live-tested**. Near/far clip-plane values remain the same unconfirmed placeholders flagged in item 5. See `re_notes/x64_migration/vulkan_dlss_pipeline_research.md` item 17, ROUND 4.
12. **Real per-object (Dynamic Object) transform capture — groundwork for true per-object motion vectors, not just the camera-only baseline.** A dense RE pass found the engine already computes and stores exactly what this feature needs every frame — a real world-space position plus a real 3x3 rotation matrix per dynamic entity (confirmed via the engine's own literal debug string, `"R_AddDObjSurfacesCamera"`), in plain, addressable memory, with no need to hook any function or replicate any skinning math. New `streamline_object_motion_x64.cpp` resolves the real array addresses via signature scan (once, cached) and reads a live current/previous transform snapshot for every active dynamic object every frame, logging real diagnostics for verification. SP-only. Build-verified (0 errors), deployed; not yet wired into the actual DLSS motion-vectors buffer — this is the data-capture step only, real velocity computation is separate, not-yet-started follow-up work. See `re_notes/x64_migration/vulkan_dlss_pipeline_research.md` item 17, ROUND 5.
13. **CRITICAL: a real, severe pre-existing performance bug in the Vulkan/Streamline resource-tagging path found and fixed — a sustained 120fps→12fps regression, live-reported the same day item 12 shipped.** Two rounds of chasing: the first fix (moving item 12's own one-time signature scan off the render loop) was real and correct but not the actual cause. The real mechanism, found via direct log inspection: `TagColorResourceForFrame` called `QueryInterface`+`GetVulkanImageInfo` **unconditionally, on every single `SetRenderTarget(0, ...)` call** — before its own resolution-match filter even ran. DXVK's own doc comment states `GetVulkanImageInfo` "flushes outstanding commands" to report the post-flush layout; `SetRenderTarget(0, ...)` fires 9+ times per real frame (shadow maps, post-process, UI), forcing a real GPU pipeline flush that many times per frame for resources this project almost always immediately discards anyway — invisible to this project's own CPU-side timers since the real cost is a forced GPU stall, not hook-body time. Fixed with a cheap, pure-D3D9 `GetDesc` pre-filter before the Vulkan interop path ever runs. `TagDepthResourceForFrame` had the identical unconditional-flush shape with no filter at all — fixed with a same-D3D9-pointer dedup instead. Build-verified (0 errors), deployed; **not yet re-tested live**. See `re_notes/x64_migration/vulkan_dlss_pipeline_research.md` item 17, ROUND 5 FOLLOW-UP #2.
14. **Every Streamline-adjacent per-frame entry point now explicitly gated on real, current backend/init state, closing a real (if currently harmless) correctness gap.** An audit found four real per-frame/per-device-creation call sites doing unconditional work regardless of `GraphicsApi`/`StreamlineEnabled` — genuine wasted CPU work under `LegacyD3D9` or with Streamline off, not a functional bug (the one real Streamline SDK call downstream was already correctly gated), but a real violation of a standing project rule now locked explicitly: any Vulkan/Streamline-specific code path must gate on the most specific real signal available at its own entry point, never rely on a downstream call failing gracefully as the only protection. Fixed at all four sites; the pattern is now a documented standing rule for any future backend-specific work. See `re_notes/x64_migration/vulkan_dlss_pipeline_research.md`.
15. **`slEvaluateFeature` wired — the real DLSS evaluation call, the piece that actually spends GPU work on DLSS rather than just preparing data for it.** New `streamline_evaluate_x64.cpp`: a real, dedicated Vulkan command pool/command buffer/fence (this project's own, not DXVK's), and the previously-missing mandatory `slDLSSSetOptions` call (mode + real output resolution), resolved via `slGetFeatureFunction` like every other Streamline export in this codebase. New `[Experimental] DLSSModeX64` config value (default `3`/`eMaxQuality`) selects the real `sl::DLSSMode`. **A real ordering bug caught before it ever shipped live**: `StreamlineFrameTick()` (which advances the tracked frame token) was running BEFORE this frame's own resource tagging and the new evaluate call in `Hook_EndScene` — harmless while every consumer was a pure diagnostic, but would have silently evaluated DLSS against a token that no longer matched this frame's own tags/constants once evaluation was wired. Reordered so tagging and evaluation both use the still-current token, advancing to the next one only afterward. Build-verified (0 errors), deployed; **not yet live-tested**. Next open question, unchanged: where DLSS's real upscaled output actually composites back into the presented frame. See `re_notes/x64_migration/vulkan_dlss_pipeline_research.md` item 17, ROUND 5 (the second entry under that label — the DObj-capture round above and this one both used the same round number under different item numbering; distinguishable by content).
16. **First real DLSS live test: found the actual reason DLSS can't run — a real, external NVIDIA registration requirement, not a bug in this project's own code — plus a second, real, independent `slSetConstants` bug fixed in the same pass.** Wiring `sl::Preferences::logMessageCallback` (previously never set, so Streamline's own internal diagnostic log was going nowhere this project could see) immediately surfaced the real cause: `sl.dlss`/`nvngx_dlss.dll` both load correctly, but NGX itself refuses to initialize with "Please provide correct application id when calling slInit" — DLSS specifically requires a real, NVIDIA-issued application ID with no documented fallback via engine/version (unlike Streamline's own general init, which this project's existing engine/version fields already satisfy). Confirmed via NVIDIA's own `ProgrammingGuideDLSS.md` and two live, currently-unanswered NVIDIA developer-forum threads from other independent/modding developers asking this exact question — this is a genuine, currently-unresolved external blocker, not something more code can fix. **Separately, a real, independent bug was found and fixed in the same log capture**: `slSetConstants` was failing with `eErrorDuplicatedConstants` on most frames, because the per-frame camera-matrix update function was calling it up to ~13 times per real frame (once per redundant call into a shared render-state struct) when Streamline only permits exactly one call per frame/token. Fixed with a real per-frame dedup keyed to a new frame-sequence counter, not to the camera data itself (which could wrongly skip a genuinely new but stationary frame). Build-verified (0 errors), deployed; **not yet live-tested**. **UPDATE, same day: the "external, unresolvable" application-ID blocker above is RESOLVED.** Reading NVIDIA's own open-source Streamline plugin source directly found the real condition: NGX's "no application id needed" path requires BOTH `engineVersion` AND `projectId` (a self-chosen GUID, not NVIDIA-issued) to be set — this project had only ever set `engineVersion`. Setting `pref.projectId` to a locally-generated GUID is the real, working path an independent developer without an NVIDIA partnership uses. Build-verified (0 errors), deployed; **not yet live-tested**. See `re_notes/x64_migration/vulkan_dlss_pipeline_research.md` item 17, ROUND 7/8.
17. **DLSS live-tested for the first time — a real NGX range-check failure and a real hang, both addressed; DLSS auto-switches to DLAA mode above 100% render scale, session-only, never written to config.** With `projectId` set, DLSS actually evaluated for the first time (`NGXDLAA::EvaluateFeature` fired) — and immediately hit a real, confirmed NGX range check: any input larger than DLSS's own output resolution is hard-rejected ("RenderSubrect (5120x2880) outside of Min/Max dynamic res"), not just an untested edge case. The session then hung outright (confirmed via a kill, not a crash dump) roughly 14 seconds later — most likely because the fence wait after submitting DLSS's GPU work used an unbounded `UINT64_MAX` timeout; fixed with a real 2-second timeout, logged loudly on timeout rather than blocking forever, a correct fix on its own merits regardless of the deeper cause. **Real design decision**: rather than simply disable DLSS above 100% render scale, it now auto-switches to `DLSSMode::eDLAA` (a same-resolution mode — input==output, no resize) with the output set to the render-scale resolution itself, satisfying NGX's own range check while still running DLSS's real denoise/temporal-AA pass on the supersampled image — genuinely testing whether feeding DLSS an already-supersampled image has real merit, not sidestepping the question. This switch is session-only: `DLSSModeX64` in `mw3ncp_config.ini` is never modified, and a one-time in-game modal explicitly notes both the switch and that DLAA is genuinely expensive (the full neural network running at your entire supersampled resolution, not a cheap fallback). **Honest, real remaining gap**: DLSS's enhanced output still doesn't reach the screen — a final downsample-to-display compositing step doesn't exist yet, and this project's own architecture (the DLSS input is deliberately the 3D-scene-only render target) means that step has to happen before UI/HUD drawing, not after, or it would overwrite the HUD entirely. Build-verified (0 errors), deployed; **not yet live-tested**. See `re_notes/x64_migration/vulkan_dlss_pipeline_research.md` item 17, ROUND 9.
18. **The "does DLSS's output ever reach the screen" gap is closed — no new RE needed, this project already had the right hook point for it (motion blur's own trigger), just never applied it to DLSS.** `Hook_EndScene` (where DLSS's tagging/evaluation used to run) fires AFTER the frame's own 2D/UI compositing is already done — compositing DLSS's output there would have overwritten the HUD, not enhanced the 3D scene under it. Moved the whole pipeline (output/motion-vector tagging, `slEvaluateFeature`, and a real new final composite step) onto the SAME earlier per-viewport boundary motion blur/FSR already use — confirmed, by years of live-proven precedent, to fire after the 3D scene finishes but before HUD dispatch begins. The real composite itself is a plain `StretchRect` from DLSS's own output surface onto the current render target at that boundary, resizing automatically for the DLAA-override case. Gated by the same three real gates motion blur requires (menu-active/in-level/clcState), and the motion-blur trigger hook now also installs when `StreamlineEnabled=1` even with motion blur/FSR both off. Build-verified (0 errors), deployed; **not yet live-tested**. See `re_notes/x64_migration/vulkan_dlss_pipeline_research.md` item 17, ROUND 10.
19. **Real groundwork toward DLSS 5 ("Neural Rendering," `sl::kFeatureDLSS_NR`) — feature registration and load-status detection, not yet a working evaluate path.** Real research found a currently-active open-source community project (`MotionflowOffical/UniversalDLSS5`, Apache 2.0) already demonstrating DLSS 5 Neural Rendering on a D3D9 title in the same engine lineage as MW3 (Call of Duty 4), and confirmed via two independent primary sources (NVIDIA's own public Streamline repo, and production engine `GaijinEntertainment/DagorEngine`'s own build-time header check) that the real blocker — NVIDIA's `sl_dlss_nr.h` options-struct header — is genuinely not publicly released yet, only available through NVIDIA's own developer program. A third-party reverse-engineered copy exists but was deliberately not used to build a real options struct here, since its accuracy against any primary source is unverified and a wrong struct layout risks real memory corruption. New `[Experimental] DlssNeuralRenderingEnabledX64` config toggle requests `kFeatureDLSS_NR` alongside `kFeatureDLSS` at init and logs whether `sl.dlss_nr.dll` actually loads — detection only. Set live (`DlssNeuralRenderingEnabled=1`). Build-verified (0 errors), deployed; **not yet live-tested**. See `re_notes/x64_migration/vulkan_dlss_pipeline_research.md` item 17, ROUND 11.
20. **Real scope correction, direct from NVIDIA: DLSS 5 Neural Rendering is officially GeForce RTX 50-series (Blackwell) only — this project's own goal for it is corrected to match.** NVIDIA's own developer blog (2026-09-22) states directly that the model "runs locally on a single GeForce RTX 50 Series GPU," confirming this isn't a driver/software gap on older cards, it's the hardware tier NVIDIA actually built and trained the model for. This project is not attempting to run it on pre-Blackwell GPUs — the real goal, same additive-tiering pattern already used for DLSS Frame Generation/Multi Frame Generation, is shipping real support for players who DO have RTX 50-series hardware, never blocking any lower tier. Once the real header/binary exist (now genuinely obtainable through NVIDIA's own developer channels, since DLSS 5 has actually shipped in a real product), the remaining real step is per-adapter gating via `slIsFeatureSupported` so this cleanly no-ops on any GPU that doesn't qualify. See `re_notes/x64_migration/vulkan_dlss_pipeline_research.md` item 17, ROUND 12.
21. **CRITICAL: first real live test of the DLSS composite pipeline found a genuine GPU device-loss fault causing a real hang — a permanent safety latch fixes the runaway retry, not just the symptom.** Live report: a blacked-out viewport followed by a hang, confirmed via Windows Error Reporting as a real `AppHangB1` fault (a real detected hang, no crash dump exists for hangs). The decisive log line: `vkQueueSubmit FAILED (VkResult=-4)` — `VK_ERROR_DEVICE_LOST`, a genuine GPU/driver-level fault, most likely triggered by DLAA processing a full 5120x2880 buffer at 200% render scale — a heavy, unusual real-time neural-network workload. The real bug: this project's own code had no recovery logic at all and kept retrying the identical Vulkan calls against an already-dead device every single frame, forever — exactly what turns one real GPU fault into an unrecoverable hang. New `g_dlssDeviceLostX64` latch permanently disables further DLSS attempts for the rest of the session the instant device loss is detected, at both the submit and fence-wait call sites. `InternalRenderScalePercent` set back to `100` to validate the base pipeline via the normal upscale path before returning to the higher-risk supersampled case. Build-verified (0 errors), deployed; **not yet live-tested**. See `re_notes/x64_migration/vulkan_dlss_pipeline_research.md` item 17, ROUND 13.
22. **Real fix, same day, sharper user diagnosis: DLSS's own heaviest one-time GPU allocation was firing cold, mid-gameplay, instead of at a quiet moment.** `slDLSSSetOptions` only ever fired lazily on the first real gameplay frame (gated behind menu/in-level/clcState checks) — meaning DLSS's real, heavy first-time resource allocation happened in the middle of an already-busy frame rather than a controlled, idle one, a real, plausible contributing factor to item 21's device-loss fault. New `PrewarmDlssOptionsX64` calls it once, immediately after device registration succeeds — while the frame is still simple, at or near the main menu — using the real display resolution; the existing lazy re-check still upgrades to the DLAA-effective resolution later, now as a smaller, cheaper re-configure rather than the original cold allocation. Build-verified (0 errors), deployed; **not yet live-tested**. See `re_notes/x64_migration/vulkan_dlss_pipeline_research.md` item 17, ROUND 13.5.
23. **CRITICAL: the pre-warm fix above crashed the game before the splash screen on every launch — a real `sprintf_s` buffer overflow in brand-new code, the same recurring bug class this project has hit many times before.** Root-caused via crash-dump analysis: the new pre-warm log line's real worst-case length (238 bytes) exceeded its `buf[200]`, and this UCRT fails fast rather than truncating. Since the pre-warm call now runs unconditionally from `Hook_CreateDevice`, this crashed on every single launch. Fixed by widening to `buf[400]`; swept this session's other new log lines and found one more uncomfortably tight margin, widened as well. Build-verified (0 errors), deployed; **not yet live-tested**. See `re_notes/x64_migration/vulkan_dlss_pipeline_research.md` item 17, ROUND 13.6.
24. **CRITICAL: found and fixed the REAL root cause of the device-loss fault, via DXVK's own primary source — this project's raw Vulkan queue submissions had zero synchronization against DXVK's own internal rendering thread.** The pre-warm fix (item 22) didn't resolve the hang — the same `VK_ERROR_DEVICE_LOST` fault recurred at 100% render scale (the normal path, not the earlier 200%/DLAA hypothesis) after a small, consistent number of successful frames, pointing at a race condition rather than a workload-size-driven timeout. This project's own `vkQueueSubmit` calls submit directly to the same `VkQueue` DXVK itself uses every frame, with zero synchronization — concurrent submission to one queue from different threads with no external locking is undefined behavior per the Vulkan spec. Read DXVK's own real interop header directly (not a guess): `ID3D9VkInteropDevice` exposes exactly the methods needed for this — `FlushRenderingCommands()`, `LockSubmissionQueue()`, `ReleaseSubmissionQueue()` — which this project was calling none of. Extended the vtable declaration to cover them and added a real RAII guard that acquires the lock before every DLSS command-buffer submit/wait sequence and releases it on every return path automatically. Build-verified (0 errors), deployed; **not yet live-tested** — this is the primary-source-confirmed fix for the actual root cause. See `re_notes/x64_migration/vulkan_dlss_pipeline_research.md` item 17, ROUND 14.
25. **CRITICAL: fixed a genuine GPU hang on level/camera transitions — real, already-existing groundwork had never been wired up.** The DXVK sync fix (item 24) resolved the black-viewport symptom, but a live re-test still hung on level entry: two real `vkWaitForFences` timeouts before device loss, meaning DLSS's own GPU work was genuinely never completing. Direct user diagnosis pointed straight at it: "its right after the camera transition." A full-repo grep found `ResetStreamlineCameraHistoryX64()` — built specifically for exactly this case, with its own header comment already explaining why — had zero callers anywhere. Every real camera cut/level load after the first tracked frame was silently feeding DLSS a reprojection matrix built against a previous frame from a completely different scene, a real, plausible mechanism for a multi-pass neural network's GPU dispatch to hang rather than fail cleanly. Fixed with a self-contained camera-teleport detector (500 world units/tick, far beyond real sprint speed) that treats a real transition as a reset instead of feeding DLSS a discontinuous matrix. Build-verified (0 errors), deployed; **not yet live-tested**. See `re_notes/x64_migration/vulkan_dlss_pipeline_research.md` item 17, ROUND 15.
26. **Real progress confirmed (the camera-transition fix worked correctly), but the device-loss fault still hit on the very first real evaluate call — sharper evidence now points at Windows' own GPU Timeout Detection and Recovery (TDR), and a real, code-only fix was wired.** This time `vkQueueSubmit` itself succeeded; it was the fence wait that reported device loss, right after DLSS's own one-time internal setup messages — meaning the very first real evaluate call, doing genuine cold, heavy, one-time work (pipeline creation, shader compilation, first inference), likely exceeded Windows' standard ~2-second GPU watchdog — the same class of problem CUDA/ML developers hit on slow first kernel launches. A registry-level TDR delay increase is the standard fix for that in general, but isn't viable for something shipped to other players. Real, code-only fix: `slAllocateResources`, a documented Streamline API specifically for pre-allocating a feature's resources ahead of the first evaluate call, now called once at the same safe, idle pre-warm moment `slDLSSSetOptions` already uses. Build-verified (0 errors), deployed; **not yet live-tested**. Honestly flagged: whether this alone covers the full slow cold-start cost is unconfirmed against Streamline's own closed-source internals — a larger-scope dummy-evaluate warm-up remains a real fallback if this proves insufficient. See `re_notes/x64_migration/vulkan_dlss_pipeline_research.md` item 17, ROUND 16.
27. **Two real, code-only mitigations for the DLSS evaluate hang, given new evidence the failure is map-dependent (Dome hung mid-transition, faster than before; Underground reached first-person first) — a post-reset stabilization window, and the direct-requested main-menu warm-up experiment. The warm-up's first version had a real bug that prevented it from ever running — fixed the same day.** New `IsStreamlineCameraStableX64()` (`streamline_camera_x64.cpp`) requires 30 real tracked frames to have passed since the last camera reset (first frame or a detected teleport) before the real gameplay evaluate path is allowed to run at all — closing a race the single-shot teleport-reset flag alone didn't cover. Separately, `RunDlssMainMenuWarmupOnceX64` attempts DLSS's real first-ever evaluate while a menu is active, before the gameplay gate applies. **The first live test found the warm-up never actually reached evaluate**: it tried to set its own separate `sl::Constants`, but the specific menu active during the test (the pause menu, which keeps the loaded level rendering behind it) already had a real camera, and the normal per-frame path had already set Constants for that tick — Streamline correctly rejected the warm-up's duplicate call, so the gameplay hang the user then hit was still genuinely the first real evaluate attempt of the session. **Fixed**: the warm-up no longer builds its own Constants at all — it now requires the real per-frame path to have set Constants at least once (`HasStreamlineCameraDataX64()`) and reuses them directly. Reuses the exact same proven safety net (DXVK submission lock, 2s fence timeout, device-lost latch) as the real gameplay evaluate call either way. Fires at most once per process lifetime regardless of outcome; never composited to screen. Build-verified (0 errors, 0 warnings), deployed; **not yet live-tested**. See `re_notes/x64_migration/vulkan_dlss_pipeline_research.md` item 17, ROUND 17 and its follow-up.
28. **A real Vulkan spec violation found in the DLSS evaluate path — the leading candidate for the GPU hang on level load: the DLSS output buffer could not legally be written by DLSS at all.** Read against DXVK's own source (`d3d9_common_texture.cpp`), the output texture this project created via plain `CreateTexture(RENDERTARGET, A8R8G8B8)` got a `VkImage` with no `VK_IMAGE_USAGE_STORAGE_BIT` (usage `0x17`), held in `SHADER_READ_ONLY_OPTIMAL` — but DLSS writes its output through a compute-shader storage-image write, which the Vulkan spec forbids on such an image. That is GPU-level undefined behavior and a sufficient explanation for the first evaluate's fence never signaling and the device then being lost, independent of the cold-start/timing theories from item 27. Now created through DXVK's own interop `CreateImage` (`ID3D9VkInteropDevice` vtable slot 12, newly declared in `dxvk_interop_x64.h` with a layout-checked mirror of `D3D9VkExtImageDesc`) with `ImageUsage=VK_IMAGE_USAGE_STORAGE_BIT`, which DXVK keeps in `VK_IMAGE_LAYOUT_GENERAL`; format moved to `A8B8G8R8` (`R8G8B8A8_UNORM`, spec-mandatory storage support). Also: a 2s fence timeout now latches DLSS off for the session like device loss does, instead of the next frame resetting a still-pending command buffer (itself invalid Vulkan usage). Authored in a cloud session with no MSVC toolchain; build-verified same day on the real x64 toolchain once merged (0 errors, 0 warnings, including the new `dxvk_interop_x64.h` vtable slots/struct). **LIVE-CONFIRMED**: the real output image's logged usage went from `0x17` to `0x1F` (the STORAGE bit present), and DLSS ran multiple consecutive real evaluate+composite cycles with zero device loss for the first time in this whole investigation. See `vulkan_dlss_pipeline_research.md` item 17, ROUND 18.
29. **DLSS inputs are now re-resolved to their current Vulkan image right before every evaluate — a second real, load-dependent cause of the "runs for a few frames, then hangs, sooner on heavier levels" failure.** DXVK's memory defragmentation (on by default on NVIDIA, engaged under memory pressure) moves images to new `VkImage` handles, recording those moves at the end of every DXVK command list — including the flush this project does right before evaluating. Scene color/depth and the DLSS output/motion-vector buffers were all tagged earlier in the frame (depth even cached its image across frames), so DLSS could read or write an image DXVK had already retired: a GPU page fault. Evaluate now flushes, re-resolves and re-tags all four buffers, and only then locks DXVK's queue; the depth-image cache is removed (in the DXVK this mod ships, `GetVulkanImageInfo` only reads fields and doesn't flush, so the cache saved nothing), and full memory barriers now bracket DLSS's own GPU work so DXVK's writes and DLSS's output are visible across the hand-off. Authored in a cloud session with no MSVC toolchain; build-verified same day on the real x64 toolchain once merged (0 errors, 0 warnings). **LIVE-CONFIRMED**: the very first live retest caught a real DXVK image relocation mid-session (`post-flush re-resolve: color image changed 0xc76f0d0 -> 0x21bcded0`) and re-tagged it correctly rather than evaluating against a stale handle. Combined with item 28's fix above, DLSS ran a sustained streak of real evaluate+composite cycles with no hang, no device loss — the first genuinely stable live DLSS session this project has had. See `vulkan_dlss_pipeline_research.md` item 17, ROUND 19.
30. **DLSS above-100%-render-scale black viewport: a real one-frame texture-size lag fixed, plus a read-only pixel readback diagnostic to isolate the remaining cause.** The output texture was sized before the DLAA-override's effective size was recomputed, so the first frame after crossing 100% hit a real NGX "Output subrect... exceed" error; the effective mode/size is now computed first (`UpdateDlssEffectiveOutputX64`) in both the gameplay path and the menu warm-up, and evaluate skips any frame where the texture still disagrees. The black viewport itself still has three possible causes (wrong scene surface tagged as DLSS's input — ambiguous above 100% since every scene-sized target shares the same size; DLAA genuinely writing black; or the final composite blit) that log-level success/failure can't distinguish between. New `[x64-streamline-readback]` diagnostic copies a 4x4 center block from both the tagged input and the DLSS output, inside the evaluate's own command buffer (layouts fully restored, WAR-safe), and logs the average color of each after the fence — first 3 evaluates after any mode/size change, then every 600th, never affecting what's actually drawn even if its own setup fails. Authored in a cloud session with no MSVC/Vulkan-header access; build-verified same day on the real toolchain (0 errors, 0 warnings). **Not yet live-tested** — next step is one run at 200% render scale to read the `[x64-streamline-readback]` lines and identify which of the three causes is real. See `vulkan_dlss_pipeline_research.md` item 17, ROUND 21.
31. **LIVE-CONFIRMED: DLSS's above-100%-render-scale black viewport is fully resolved — direct user report after retest: "it loooks SOOO GOOD."** Closes the whole "GPU hang / black viewport" investigation this file's own entries have tracked since ROUND 13, through five distinct, real, stacked root causes each fixed in turn: the output buffer missing Vulkan `STORAGE` usage (ROUND 18); DXVK relocating images mid-session with this project's own tags going stale (ROUND 19); the output texture lagging the DLAA-override's effective size by one frame (ROUND 21); the wrong render target being selected by size alone, feeding DLSS a post-FX bloom/blur intermediate instead of the real scene — fixed by selecting on the engine's own render-target ID instead (ROUND 22); and finally, the real scene target turning out to be 4x MSAA with no single-sampled copy ever bound as a render target, requiring the proxy to resolve it itself before evaluate (ROUND 23). Every round was a genuine, necessary fix, not a wasted attempt — the symptom had multiple independent causes stacked on top of each other, and this is the first time all of them are resolved simultaneously. **Known, deliberately deferred tradeoff**: bloom/depth-of-field/film blur don't appear in the DLSS-enhanced image above 100% render scale, since DLSS now correctly processes the scene before post-FX runs and its result is composited back afterward — fixing this needs a separate change (compositing DLSS's result in before post-FX instead of after). See `re_notes/x64_migration/vulkan_dlss_pipeline_research.md` item 17, ROUNDS 18-23.
32. **DLSS ghosting fix (ROUND 24): camera reprojection now uses the engine's real view/projection matrices instead of hand-built ones — the actual cause of the "motion vectors catching up" trail reported right after ROUND 23.** With the motion-vectors buffer zero-filled and `cameraMotionIncluded=eFalse`, Streamline derives camera motion itself from the Constants matrices, so reprojection is only as accurate as those matrices — and they were synthesized incorrectly: `cg_fov` was treated as the true horizontal FOV (IW actually defines it at 4:3 and widens it for the real aspect ratio, and ADS changes it entirely), and the near/far clip planes were untested guesses (4/4000). A wrong FOV or clip range misplaces every reprojected pixel, producing exactly a ghost trail behind camera movement. Root cause of the wrong values: the hook believed to be the 3D projection setter was actually the engine's 2D (HUD/menu) one — a real correction to this project's own renderer-reference documentation, not just a code bug. The real fix reads the engine's own current view/projection matrices directly (`GfxViewParms` via the render state's `+0x1790` pointer, written by the same native function post-FX calls right before the DLSS boundary) once per frame, validates them as a genuine perspective view with an orthonormal rotation, and derives near/far/FOV/aspect/depth-direction from the real projection math — falling back to the old synthesized builder (with a forced reset on switch) only if the real matrices are ever unreadable. Caught and fixed a real build break in the same pass: the merged diff removed a local variable (`camPos`) still needed by the unrelated projection-jitter feature lower in the same function, missed since the cloud session that authored it has no compiler. Build-verified (0 errors, 0 warnings) after that fix, deployed. **LIVE-CONFIRMED**: direct user report after retest, "clean as day, beautiful stuff" — pure camera-pan ghosting is gone. Closes the last open issue from the whole ROUND 18-24 DLSS investigation; independently-moving objects (Stage 2, no per-object motion vectors yet) are a separate, already-known gap not yet specifically re-checked. See `re_notes/x64_migration/vulkan_dlss_pipeline_research.md` item 17, ROUND 24.

#### Performance: the x64 render regression (issue #4)
33. **`TriggerSelfMemoryDumpX64` (F9 self-dump tool) hardened across two rounds after real failure modes found during a live pause-vs-gameplay performance investigation.** Round 1: the bounded-size dump flags shipped earlier this cycle worked at the main menu and during live gameplay, but failed with `ERROR_INVALID_PARAMETER` specifically during in-game-pause captures, producing empty ~250KB files — fixed a `GetLastError()`-read-after-`CloseHandle()` ordering bug and added a one-time fallback to a smaller flag combination. Round 2, same day: the fallback failed with the identical error, ruling out the specific flags removed, and reusing the same file handle across both attempts turned out to also regress the previously-reliable live-gameplay capture. Now retries across three tiers (full flags, reduced flags, bare `MiniDumpNormal`), each opening its own fresh, truncated file handle rather than reusing one across attempts. See `re_notes/known_issues_x64.md` issue #4's newest round.
34. **New F10 tool: a real, repeated in-process sampling profiler (`self_sampling_profiler_x64.cpp`), built after both live x64dbg attach and single-instant memory dumps hit real limitations chasing the pause-vs-gameplay regression.** Samples every other thread's instruction pointer via ordinary `SuspendThread`/`GetThreadContext`/`ResumeThread` against this same process's own sibling threads — never attaches as an external debugger, never touches `PEB->BeingDebugged`, the same mechanism every real sampling profiler and language-runtime GC already uses. Writes a plain-text report of each thread's most time-consuming module+offset (real DXVK worker-thread names like `dxvk-cs`/`dxvk-submit` resolved via `GetThreadDescription`), tuned after the first live captures showed nothing but idle (5ms interval, multi-second window). Decoded a real signal (a DXVK submit-thread Sleep-vs-wait shift) that was then correctly ruled out as the regression's root cause once the same fps gap was confirmed to reproduce under `LegacyD3D9`, where that thread doesn't exist. See `re_notes/known_issues_x64.md` issue #4's newest round.
35. **New F11 tool: real GPU-side frame capture via RenderDoc's official in-application API (`renderdoc_capture_x64.cpp`)**, since the CPU sampler above can't distinguish "idle" from "correctly blocked on the GPU." Vendors only `renderdoc_app.h` (MIT, fetched verbatim from the official repo — the struct layout is safety-critical to get byte-exact). Initializes from the top of this DLL's own `Direct3DCreate9` forwarder, before DXVK creates its real `VkInstance` — deliberately not from `DllMain`, matching this project's own real, live-reproduced lesson about DLL init hanging the game from inside the loader lock. Writes a real `.rdc` file beside the game exe, openable in RenderDoc's own UI. RenderDoc itself is not bundled — install it separately (free, renderdoc.org) for F11 to do anything; degrades cleanly and logs clearly otherwise. See `re_notes/known_issues_x64.md` issue #4's newest round.
36. **A real `cl_paused` value now logged alongside the render-view diagnostic**, closing a real gap that cost time this same session — this project had no reliable signal to tell "genuinely at the in-game pause menu" apart from the main menu, a loading screen, or any other menu state (the existing `menuActive` flag is a blanket "some menu is active" bit). `cl_paused` was deliberately left unread on x64 back on 2026-09-16 because the dvar-lookup function was still an unconfirmed hardcoded address at the time; it's since been converted to a real signature scan, so the read is now safe. See `re_notes/known_issues_x64.md` issue #4's newest round.
37. **A real, sustained ~30-32fps floor on one specific heavy Campaign mission, investigated and ruled a genuine content-cost characteristic, not a bug.** A live report that a specific mission runs at a hard, constant ~30fps (vs. this project's own typical 120-140fps under Vulkan) raised a real, reasonable theory — a level-scoped `com_maxfps` cap, a known technique for protecting scripted-sequence timing in this engine family. Tested via a real diagnostic (an existing `com_maxfps`-change log line was moved above `frame_pacing_x64.cpp`'s own `framePacingEnabled` early-return so it logs unconditionally, without engaging the actual limiter) and cleanly disproven: `com_maxfps` stayed at `0` for the entire session. `frametime_benchmark.csv` cross-checked the same session and showed this mod's own hooks and texture-creation cost together under 3% of the ~33ms frame budget throughout — the remaining cost is genuine native engine rendering work, holding even at a modest 200% render scale on high-end reference hardware. See `re_notes/known_issues_x64.md` issue #4's newest round.
38. **A real, public vanilla-game report tracked as a pre-release investigation lead**: a `r/mw3` post ("The 64bit campaign is broken") describes a periodic, roughly-once-per-second stutter on a mobile RTX 2060 laptop since Activision's own x64 update — not a `MW32011NCP` regression, but a real candidate mechanism (the already-shipped 3GB memory-detection-cap fix, issue #4) is on record for a future test pass. See `re_notes/known_issues_x64.md` issue #8.
39. **CRITICAL, real cross-binary confirmation: Activision's own x64 recompile introduced a genuine draw-submission regression, unrelated to this project's own code — issue #4's real root cause, not yet fixed (a base-game issue, not this mod's).** A live stack-capture diagnostic fully explained the rare `EndScene` main-thread fallback (a real `D3DERR_DEVICELOST`/`map_restart` recovery path, `FUN_1401950a0`, confirmed via the exact two's-complement constant match) — but confirmed it's NOT the main driver (4 events vs. 2866 slow frames in the same session). The real signal: a new per-frame render-view fire-count diagnostic found a clean, monotonic ~2-4x more render-pass activations per frame during slow stretches vs. 120fps play (317 samples: 19.1/frame at 100+fps → 78.4/frame at 0-19fps). **Cross-referenced directly against the real x86 binary, same methodology**: the equivalent function has exactly 2 real callers on x86 vs. 40 on x64 — the same logical render-view-activation function went from 2 call sites to 40 across the recompile. This is real, structural, cross-binary evidence of a genuine Activision x64-port regression, not scene/content cost. Not this project's own bug and not yet fixed — real next step is identifying which specific render pass is being redundantly resubmitted (a sequence-capture diagnostic, logging the actual view indices fired during slow frames, is deployed and awaiting live data). See `re_notes/known_issues_x64.md` issue #4's newest round.
40. **The render-view activator (`FUN_1401dfd80`) turned out to have its own same-pass dedup early-out — the raw fire-count diagnostic from item 39 measures an upper bound on real cost, not real cost directly, and a sharper diagnostic now exists to settle it.** Deep cross-binary tracing found the render-view activator itself returns immediately, doing none of its real work, when the requested pass value already matches the currently-active one — meaning repeated calls with the SAME pass value (confirmed to be exactly what `FUN_1401939f0`, the leading suspect for the fan-out, actually does) are likely cheap no-ops, not real reactivations. Two independently-found x86 chains (a material-rebind path and a full DOF/color-grade/fog/lens-flare-equivalent postfx-setup batch, all five functions individually decompiled and confirmed) corroborate this: x86 achieves the same visual work via direct struct writes and never touches the activator at all. A new `realTransitions` counter (`RecordRenderViewFireX64`/`GetAndResetRenderViewTransitionCountX64`, `analog_input_hooks_x64.cpp`) now tracks genuine pass-value changes alongside the existing raw fire count, logged in `[x64-renderview-rate]`. Build-verified (0 errors), deployed; not yet live-tested — the next real play session's data will show whether the raw fire count was substantially overstating the real regression severity, or whether something in practice defeats the dedup. See `re_notes/known_issues_x64.md` issue #4's newest round.
41. **Root cause found AND LIVE-CONFIRMED for a major share of issue #4's whole regression: x64 lost a real hardware-capability gate on the per-light shadow-activation call — a genuine, missing "skip the expensive path on capable hardware" check x86 has always had.** A live capture from a laggy repro-mission session (the `realTransitions` diagnostic from item 40, first real data) found the dedup only saves ~18-27% of raw calls — most of the fan-out during slow frames is genuine, expensive work, not free no-ops — and its captured sequences finally identified the long-unexplained `6→7→6→7…` alternation pattern (over half the calls in the worst frames) as a per-light shadow-dispatch function (`FUN_140196ad0`) calling the activator unconditionally, with no gate at all. x86's exact structural counterpart gates the identical call behind a real GPU capability probe (the classic D3D9 "NULL render-target" hardware shadow-map trick, true on virtually any modern GPU) — meaning x86 skips this call entirely on real player hardware, using it only as a rare fallback for old/incapable GPUs. A full x64 string search found zero trace of the detection routine anywhere in the binary. A read-only diagnostic (`[null-rt-shadow-cap-diag]`) replicates x86's exact capability check at device-creation, and a new `[Experimental] SkipRedundantShadowActivationX64` toggle (default OFF) implements the actual fix — skipping only this one specific call site (identified via its own signature-resolved return address, every other one of the activator's 40 real call sites unaffected) when hardware is confirmed capable. **LIVE-TESTED same day with two independent, strong, positive results**: the original intensive repro mission (200% render scale, full visual-enhancement suite) went 30fps→40fps (~33%), and — more tellingly, since it rules out any content/scripting-cost explanation — the PAUSE MENU itself (zero simulation running) went 17fps→27fps (~59%). Log-confirmed real hardware capability match (`capable=YES matchedPair=0`, the strongest candidate pair) and 3000+ real skips fired in one session, both types (`view=10`/`11`) matching the already-understood per-light dispatch exactly. **Still genuinely experimental, ships off by default** — visual shadow correctness hasn't been independently confirmed yet, and it's only been tested on one machine/handful of scenarios so far. See `re_notes/known_issues_x64.md` issue #4's newest round for the full trail, including an investigated-but-inconclusive side lead on whether this ties to a broader platform/console-build-unification change in this exact update (real evidence found — new GDK-detection code entirely absent from the original build — but not proof of a specific console target).
42. **`SkipRedundantShadowActivation` graduated to `[Video]`, default ON, after a THIRD independent live confirmation, and `GraphicsApi` itself flipped to default `Vulkan` on `iw5sp.exe`.** The shadow-activation fix picked up a third real-world confirmation on a bright, sun-heavy Survival map (28-30fps→~41fps) on top of the two Campaign results already on record — a real, mechanistically sensible correlation (more simultaneous active lights on sunnier/more open maps directly feeding the same per-light activation loop the fix targets). Shadows confirmed still rendering correctly across all three tests ("visuals looked better if anything, shadows looked better"). Given that strength of evidence, the toggle moved out of `[Experimental]` into `[Video] SkipRedundantShadowActivation` (name drops the `X64` suffix, matching that section's own convention) and now defaults on. Separately, `GraphicsApi` itself now defaults to `Vulkan` on `iw5sp.exe`, following its own real, accumulated live-confirmation track record this session and earlier ones — `iw5mp.exe` is unaffected and continues to always use `LegacyD3D9` regardless, pending dedicated MP VAC-risk research. A new README table documents exactly which `[Video]` features are backend-specific (`StreamlineEnabled`/DLSS is the one genuine Vulkan-only feature; the `ForceAnisotropic`/`HighQuality` trio are currently non-functional on **either** backend, a separate tracked gap, not a backend difference) — everything else works identically under both by construction. Also flagged, not yet explained: pause menu consistently runs measurably worse than live gameplay on the same content, across every pause menu tested, even with this fix applied — a real, distinct, still-open lead for a future session, not assumed to share the shadow-activation fix's own mechanism just because both involve "pause." See `re_notes/known_issues_x64.md` issue #4's newest round.
43. **An exhaustive whole-binary CPU-side static-RE sweep for the remaining pause-vs-live/sunny-map differential closed every other plausible mechanism, then a new in-process GPU-inclusive timing tool was built to test the one class of cause CPU-side tooling structurally can't see.** SSAO, the race-condition/busy-flag hypothesis, I/O and memory-mapping divergence, a full `dumpbin`-based whole-binary import-table diff (new `WINHTTP.dll`/COM traced to DemonWare's own HTTP transport swap; a condition-variable migration traced to genuine CRT/UCRT toolchain internals), the screen-freeze/native `r_blur` system, the full 26-entry UI jump table, GSC/script-VM scheduling, and every direct `cl_paused` reference were all checked via direct x86-vs-x64 structural comparison — every one closed negative (flat/unconditional cost, or genuinely unrelated to rendering). That consistent pattern is itself real evidence the remaining differential is actual GPU execution time, which neither the F10 CPU sampler nor RenderDoc's own `chrome.json` export can measure (the sampler can't distinguish "idle" from "correctly blocked on the GPU"; RenderDoc's export is CPU-side recording time only). New `gpu_timing_probe_x64.cpp` (`GpuSyncMarkX64`) closes that gap directly: forces a real Vulkan `vkQueueWaitIdle` hardware sync bracketing each frame's `EndScene`, making the logged interval genuinely GPU-inclusive wall-clock time — no external tool or capture file needed. Real, deliberate tradeoff: this removes normal CPU/GPU pipelining and costs substantial fps while active, so it's diagnostic-only (`[Experimental] GpuSyncTimingLogging`, default OFF, Vulkan/DXVK-only — a harmless no-op under `LegacyD3D9`). Along the way, extracted DXVK's real Vulkan device/queue resolution out of the Streamline-only init path into its own standalone function so this tool works without `StreamlineEnabled=1`, fixing a real `LNK2019` linkage error caused by the extracted function initially sitting inside an anonymous namespace (the same anonymous-namespace-linkage trap this project has hit before). Build-verified (0 errors), deployed; `GpuSyncTimingLogging=1` set live for the next test pass. **Not yet live-tested** — the real payoff (comparing logged GPU-inclusive frame time between a live-gameplay and a paused capture) is the next step. See `re_notes/known_issues_x64.md` issue #4's newest round.
44. **`GpuSyncTimingLogging` fixed (real Vulkan-instance bug), then live-tested — pause vs. true healthy gameplay is ~5.6x slower, not the earlier ~1.71x; the real double-render-pass mechanism behind it found; a real, pre-existing hang reported and a second unrelated diagnostic-tool gap fixed along the way.** The tool initially never fired at all — `vkGetInstanceProcAddr(NULL, "vkGetDeviceProcAddr")` isn't valid per the Vulkan spec (NULL is only valid for a small fixed set of global functions), fixed by passing the real, already-resolved `VkInstance`. Once working, real per-frame data (found directly in the existing `[x64-renderview-rate]` log, not `[gpu-sync-mark]` itself, which proved too sparse) showed pause running the entire per-light shadow-dispatch loop TWICE per frame — traced via decompile to a real, pre-existing, generic "extra frame requested" reference count in the confirmed main loop, very likely original shared (not x64-introduced) behavior; the x64 regression is that each of the two renders costs more, not that there are two of them. A clean same-session A/B (repro → pause → unpause → healthy-area gameplay) then showed pause is really ~5.6x slower than TRUE healthy gameplay, not ~1.71x — the earlier figure understated the real cost because its own "gameplay" side was already the degraded repro content. Separately: the same test session ended in a real hang (confirmed via log evidence — no clean shutdown, memory/disk activity completely static for 7+ seconds before force-quit) — logged as `re_notes/known_issues_x64.md` issue #9, not yet root-caused. A second, unrelated diagnostic-tool gap was found and fixed in response: `InitRenderDocX64()` (the F11 GPU-capture tool) had no config gate at all, meaning RenderDoc's own Vulkan capture layer attached in-process for the whole session the instant RenderDoc was installed, whether F11 was pressed or not — now gated behind a new `[Experimental] GpuCaptureEnabled` (default OFF). Both diagnostic tools are now off by default. See `re_notes/known_issues_x64.md` issues #4 and #9.
45. **New `re_notes/ghidra_scripts/UnwindInfoLookup.java` — a real, standing crash-prevention tool, built after two live crashes taught this project exactly why a stack-walk return address isn't automatically a safe MinHook target.** Reads the actual PE `.pdata`/`UNWIND_INFO` tables directly (the real Windows x64 SEH exception-unwind metadata) instead of trusting Ghidra's own function-boundary database, which turned out to be capable of silently mis-splitting one real function into two spurious ones at a mid-body return address — exactly what caused the first of two live crashes this session (`iw5sp.exe.20608.dmp`), root-caused via `mcp-windbg` static crash-dump analysis. Gives a clear safe/unsafe verdict for any queried address (a mid-range address, a zero-prologue shared-epilogue fragment, or an explicitly `UNW_FLAG_CHAININFO`-tagged split fragment all flag unsafe; a real, non-chained function start with a real prologue flags likely-safe). Validated directly against every address this session touched — correctly flagged all four crashed/unsafe targets and cleared every already-working hook. A second, distinct lesson from a follow-up crash (a genuinely correct, unwind-verified 18-byte function that was simply too small for MinHook's own jump patch to fit safely) is recorded alongside it: passing this tool's check is necessary but not sufficient — a target also needs enough real bytes for the detour itself. Both crashes were root-caused, fixed, and redeployed the same session. See `re_notes/known_issues_x64.md` issue #4's newest rounds.
46. **The render-thread main-thread-fallback mechanism (previously only partially traced) fully resolved to its real, concrete native trigger.** A real caller-chain capture (`[render-thread-diag-stack]`, already shipped) was resolved, using the new unwind tool above, to two distinct, repeatable, real trigger chains — both funneling into the console/UI font-init function found expensive above, reached either via a menu-transition path or directly from the main thread's own idle loop. A new comparison capture (the backend-thread RECOVERY stack, not just the fallback one) confirmed the dedicated render/backend thread's own resume point is completely invariant across an entire session — real, positive evidence this thread architecture itself is stable, narrowing the fallback's real cause down to exactly the two chains found. See `re_notes/known_issues_x64.md` issue #4's newest rounds.
47. **The real dedicated render/backend thread architecture found and confirmed — x64 gained a genuine second thread for frame submission that x86 never had, and the rare times it falls back to the main thread directly precede a real frame spike.** A live test (checking `EndScene`'s own calling thread ID against the main thread's) confirmed x64's `EndScene` genuinely runs on a distinct, persistent second thread for the overwhelming majority of a session — not the main thread, settling a previously-open architectural question by direct observation rather than static guesswork. A full x86 comparison (a dedicated Ghidra full-analysis pass run against both binaries) then confirmed the contrast directly: **x86's own `EndScene` fires on the main thread every frame, with no separate submission thread at all** — x64 didn't lose parallelism in this recompile, it genuinely gained more of it, and the real regression cost is specifically the rare fallback path where x64 has to hand submission back to the main thread, something x86's architecture never needed to do. The few observed fallback transitions each directly preceded a 100-165ms frame spike (vs. a normal 16-35ms), the concrete lead the rest of this cycle's render-thread-fallback work (items above) traced to its real trigger. See `re_notes/x64_migration/renderer_architecture_map.md`.
48. **The native sun/cascade-shadow fast-vs-slow dispatcher found and live-tested — cleanly ruled out as a cause of the render-scale/content-correlated slowdown.** A genuinely separate shadow subsystem from the already-fixed per-light point/spot-light loop, keyed off its own `sm_*` dvar family (`sm_fastSunShadow`, etc.). A live diagnostic across a real multi-map session (2293 real dispatch fires) found the FAST path selected 100% of the time, zero SLOW fires anywhere — consistent with the user's own follow-up observation that a non-sun-ray mission showed the same lag rate as the sun-ray map. See `re_notes/known_issues_x64.md` issue #4's newest round.
49. **Both remaining extra-activator-call candidates from the x86 comparison above were tried live — one shipped clean (Fixed item 3), one caused real viewport corruption and was reverted, and the failure itself is a real, useful clue.** `FUN_14018e0d0`'s two extra orchestrator calls skipped cleanly (shipped). `FUN_1401939f0`'s two "guaranteed" calls did not: enabling the skip broke the viewport completely during live gameplay, **but not while paused** — direct user observation, worth recording as its own finding: this asymmetry means pause and live gameplay genuinely route through different code paths (or a different viewport/render-view state) at this exact point in the pipeline, not just "the same path running more or less often." Consistent with, and now a second independent data point for, this issue's own earlier finding that pause reruns parts of the per-light shadow-dispatch chain differently from live gameplay — the "x86 never calls the activator here" reasoning that correctly identified the other two fixed points doesn't automatically mean a call is pure overhead; these two specific calls are very likely also doing real viewport/render-view-state activation the rest of the frame depends on. Reverted before release, left in the codebase disabled with a clear warning for a future session. See `re_notes/known_issues_x64.md` issue #4's newest round.

#### Multiplayer
50. **MP controller pipeline: native menu navigation (D-pad+A/B, B-back) now works on `iw5mp.exe`, the first real MP gameplay hook this project ships.** Two of its three real signatures (`GetTopmostActiveMenu`, `ForwardKeyToMenu`) already matched byte-for-byte in both binaries with zero new work; the UI-context anchor's own bytes differ only by register allocation between the two compiles, so its real MP twin was found, disassembly-verified instruction-for-instruction against the SP original (same shape, same offsets), and confirmed to resolve to a real, sane UI-context global (cross-checked: re-deriving SP's own value with the identical method independently reproduced the already-known-correct address, confirming the method itself). The resolve/install logic (previously inline, SP-only, inside the giant `InstallAnalogInputHooksX64()`) is extracted into its own standalone, exe-aware `InstallMenuNavigationHooksX64()`, called for both SP (unchanged behavior) and MP (new). **Live-reported the same day: the A-glyph highlight showed on MP but D-pad up/down/left/right did nothing at all.** Root cause: `InjectControllerMenuNavX64()`/`InjectControllerMenuBackX64()` both gate on `g_menuActiveGateFlag`, which is only ever resolved via `kPauseToggleSignature` — already confirmed to have no MP twin at all ("MP has no SP-style pause," `mp_twins_2026-09-27.txt`), so that flag is always null under MP and both functions silently always no-op'd before ever dispatching a key. Fixed by giving both an MP-specific fallback for "is a menu currently active" — `GetTopmostActiveMenuX64() != nullptr`, using the exact same already-MP-verified functions this feature already depends on, rather than chasing a whole new MP vehicle for that specific bitflag (a separate, bigger problem, since the same flag also backs `IsMenuActiveX64_Exported()`/`TryGetClcStateX64()` project-wide — deliberately not touched here). SP's own behavior is completely unchanged in both functions. Build-verified (0 errors, 0 warnings), deployed; **the D-pad/B fix is not yet live-tested**. The follow-up performance-fix ports this entry originally listed as next (shadow-activation skip, wait coalescing, `.iwd` cache, console-font skip) landed in items 51-54 below; in-game movement/look is still to come. See `re_notes/x64_migration/mp_port_plan.md`.
51. **MP performance-fix pass, first two: `OcclusionLodScaleFix` and `PauseBlurStepCap`/`LiveBlurStepCap` now also install under `iw5mp.exe`.** Both were already standalone, independently-callable install functions (matching the pattern `InstallRenderScaleHookX64` set); their signatures already hit identically in both binaries with no new RE, and their hook bodies touch only self-contained RIP-resolved globals plus config, no SP-specific structures. Blur-cap's own menu-active check degrades gracefully under MP for now (always takes the `liveBlurStepCapX64` branch, since `IsMenuActiveX64_Exported()` can't resolve under MP yet — a separate, bigger problem, not fixed here) rather than crashing or doing nothing unsafe. Build-verified (0 errors, 0 warnings), deployed; not yet live-tested. See `re_notes/x64_migration/mp_port_plan.md`.
52. **MP performance-fix pass continues: sqrt-domain-error/screen-capture-cmd diagnostics, the render-view-select hook, and all three of its real skip optimizations (shadow-activation, master-sequencer reactivation, orchestrator extra calls), plus the `sys_sysMB` memory-detection fix — all now also install under `iw5mp.exe`.** All seven signatures already hit identically in both binaries (confirmed against `signature_resolution_sp_mp_2026-09-27.txt`, 1 hit each or 6/6 for the master-sequencer one) with no new RE needed, and every hook body touches only its own signature-resolved addresses, RIP-relative globals, or config — no SP-specific structures. Extracted from the SP-only `InstallAnalogInputHooksX64()` into its own standalone `InstallCrossExePerformanceHooksX64()`, called for both exes; SP's own call order and behavior are unchanged. This is the same render-view-select hook DLSS's engine-RT-id selection (ROUND 22) uses, but DLSS itself stays SP-only regardless (a separate gate) — enabling this hook under MP just makes it correctly a no-op there for now. Build-verified (0 errors, 0 warnings), deployed; not yet live-tested. See `re_notes/x64_migration/mp_port_plan.md`.
53. **MP performance-fix pass, wait coalescing and the `.iwd` read cache: real new MP signatures found and verified for backend/render `Sleep(1)` coalescing and the `.iwd` archive `ReadFile` call site.** Unlike the previous batch, these three needed genuine new RE — their SP signatures don't hit in `iw5mp.exe` (the byte patterns deliberately encode a literal, unwildcarded CALL displacement to disambiguate two otherwise-identical-shaped call sites, so a different binary needs its own literal bytes). Found via real capstone disassembly of the HIGH-confidence twin candidates already on record: backend-sleep's `mov ecx,1; call Sleep-wrapper` sequence sits right at its twin's entry; render-sleep's sits 0x8d bytes deeper in the function body; the `.iwd` read call was disambiguated from 5 candidate indirect calls in its twin function by cross-referencing each one's real target against MP's own import table (only one points at `ReadFile`; the rest are `GetLastError`/`ReadConsoleW`/`GetConsoleMode`). All three verified to match exactly once in `iw5mp.exe`'s real `.text` section before shipping. `kRenderWait1Signature` already hit identically in both exes with no new work; `kWorkerWaitSignature` is genuinely ambiguous under MP (5 hits vs. SP's 2) and is left failing gracefully there rather than guessing — a real, documented, safe gap, not a silent wrong match. Build-verified (0 errors, 0 warnings), deployed; not yet live-tested. See `re_notes/x64_migration/mp_port_plan.md`.
54. **MP performance-fix pass: the console/UI font-init redundant-reload skip now also installs under `iw5mp.exe`.** `kConsoleFontInitSignature` already hits identically in both exes; its hook body only touches its own signature-resolved "already initialized" flag and config, no SP-specific data. Extracted from the SP-only `InstallAnalogInputHooksX64()` into its own standalone `InstallConsoleFontInitSkipHooksX64()`, called for both exes. **Deliberately left SP-only**: the two console-shutdown diagnostic hooks (used to verify the skip's own correctness, not load-bearing for it) — their MP twins are only MED-confidence call-graph matches, never independently disassembly-verified the way this session's other new MP signatures were, and unlike a resolve-only signature, a wrong match on a real MinHook detour target is a genuine crash risk, not a safe silent failure. Not worth that risk for hooks with no functional purpose of their own. Build-verified (0 errors, 0 warnings), deployed; not yet live-tested. See `re_notes/x64_migration/mp_port_plan.md`.
55. **MP gets its own real "in level" signal — `TryGetInLevelFlagX64()`, a real MP dead end from an earlier session (its own twin search came back REJECTED), now has a genuine substitute.** A real perspective 3D view with an orthonormal rotation can only exist while genuinely in a level with a live camera — never at a menu, loading screen, or main menu — so MP now reuses ROUND 24's already-verified real-view read/validate helpers (`IsRealEngineViewActiveX64`, `streamline_camera_x64.cpp`) as its own "in level" signal, needing zero new signature work (`kProjectionMatrixBuildSignature`, the real vehicle for it, already hits identically in both exes). The one prerequisite — `Hook_ProjectionMatrixBuild`'s own diagnostic hook, which populates the engine-state pointer this reads from — was itself extracted from the SP-only `InstallAnalogInputHooksX64()` into its own standalone `InstallProjectionMatrixDiagHookX64()` and wired for both exes. SP's own behavior is completely unchanged. This is a real step toward `mp_port_plan.md` step 6 (unblocking FSR/motion blur for MP eventually) — `clcState` (the other of the two remaining safety gates those features need) still has no MP substitute and is a separate, still-open piece. Build-verified (0 errors, 0 warnings), deployed; not yet live-tested. See `re_notes/x64_migration/mp_port_plan.md`.

#### Tools and other research
56. **`tools/iw5oat`: a real upstream OpenAssetTools fix merged in, then a systemic crash of its own fixed — unblocking full, real zone loading for the first time.** Merged the confirmed-good commit range of a real, third-party-tested (but never-upstreamed) fix for the mixed-width serialized ABI this project's own updated x64 fastfiles use (`Laupetin/OpenAssetTools` PR #1007). A regression this introduced (`sp_dubai.ff` breaking) was root-caused via live SEH capture to a real, systemic null-pointer crash in the shared asset-dependency-marking pool lookup (`GameGlobalAssetPools::GetAsset`'s own assert-only-guarded index, a Release-build no-op) — fixed generically for every one of the ~100+ asset types at once via a real `VirtualQuery`-backed pointer-sanity check, not a per-asset patch. A second, already-documented bug (the `SpeakerMap` loading-logic fix from a prior session, which lives only in generated, gitignored code and gets silently wiped on every regenerate) was reapplied. Net result: `so_survival_mp_dome.ff` and `so_survival_mp_underground.ff` both load completely, zero errors, for the first time ever under this fork. See `re_notes/known_issues_x64.md` issue #4 and `tools/iw5oat/re_notes/fastfile_format_research.md`.
57. **The native sound-alias play/blend function found, instrumented, and directly diffed against x86 — a real, clean negative result, not a regression.** Located `FUN_140274100` (`iw5sp.exe`) via its own internal engine warning strings, confirmed to be the real native voice-trigger for `snd_alias_t` playback/chain-blending, and live-instrumented with a timing/caller diagnostic. A full x86 counterpart trace (`FUN_006b2e00`/`FUN_006b2580`) found the core play/blend logic and its chain-alias recursion mechanism — including the real depth cap — are logic-identical between architectures; this specific subsystem is a faithful port, not a source of any duplicate-playback or performance regression. See `re_notes/known_issues_x64.md` issue #10.
58. **Static RE: drawing our own text and controller glyphs in-engine (not via the overlay) is feasible on x64.** Every engine piece is identified: `UI_DrawText` (the function `Hook_DrawTextX64` already hooks; its `color1`/`color2` params are really horizontal/vertical alignment, and the real color is a `float[4]` argument), `ScrPlace_GetActivePlacement`, all nine UI font handles, `Material_RegisterHandle`, `UI_DrawHandlePic`/`UI_DrawStretchPic`, and `R_SetSampler` (the D3D texture sits at `GfxImage`+0). The backend text renderer also supports the IW-engine inline-icon escape (`^\x01` + width + height + name), with a single-caller material lookup that makes a surgical hook point. Custom button art would come from a runtime-cloned UI material wrapping our own texture. Findings and next steps: `re_notes/x64_migration/engine_native_draw_x64.md`.
59. **New `[Overlay] TestShowAllModals` testing aid — queues every real dismiss-required modal this mod can show, in one sitting, without needing to trigger any of their real conditions.** Direct request, following the modal queue and DLSS-hardware-support-check work above: welcome, possibly-outdated, Multiplayer, the DLSS DLAA-override notice, the DLSS hardware-unsupported notice, and a sample config-value-corrected report all queue at startup and dismiss one after another, exercising the new modal queue itself along with every individual modal's own formatting — directly useful for live-testing everything shipped in items 12/13 above without needing a fresh version, a 4-week-old build, an actual `iw5mp.exe` launch, `InternalRenderScalePercent` above 100% with DLSS on, or a non-RTX GPU. Each preview reuses the real modal-building code (`BuildWelcomeModalText`/`BuildOutdatedModalText` in `d3d9_hook.cpp`, or a shared constant/helper factored out of the owning file for `dllmain.cpp`'s Multiplayer modal and the two Streamline modals) rather than a separate copy, so nothing here can drift out of sync with what a player actually sees — the same real call sites were updated to use the shared text/helpers too. Default off; never enable for normal play. Same pass found and fixed two real, pre-existing Win32 build breaks (see Fixed item 13) while build-verifying this feature against both architectures.

---

## v0.0.2-x64 — Alpha (2026-09-23)

**Summary:** A critical MP launch crash (a hardcoded x86-only address reached for the first time under MP) was root-caused and fixed via `git worktree` bisection, alongside a full audit converting every other remaining hardcoded x64 address to a proper signature scan. A real, severe, sustained stutter tied to `InternalRenderScalePercent` above 200% (damage, pause, ADS, level-transitions, an explosion/downed-state transition) was investigated in real depth across the whole rendering/memory/I/O pipeline and root-caused to a genuine native engine stability limit at large render-target sizes, not a bug in this project's own code — confirmed via five separate real fix/rule-out attempts, all eliminated, plus two genuine native engine bugs found and fixed along the way (a hardcoded 3072MB system-memory detection cap; a `sprintf_s` overflow introduced by this session's own new diagnostics, the sixth real recurrence of this bug class). **This boundary was then confirmed content-dependent, not a fixed number**: the exact same 200% that's clean in SP causes constant lag under MP — see the README's own new warning on this. `InternalRenderScalePercent` now works under Multiplayer (the first MP-enabled visual-enhancement feature), and motion blur is no longer controller-only. Real, extensive new RE groundwork was laid throughout: the classic id-Tech Hunk/Zone memory allocators confirmed intact, a first real map of the native 3D renderer's own architecture (a frontend/backend command-buffer split, a fully-named scene-setup pipeline, the complete render-target table including shadow maps), and a genuine, previously-undocumented native SSAO implementation found sitting dormant in the binary. A real, previously-undocumented bug was also caught and corrected in the record: three forced-quality video toggles have been silent no-ops on x64 since the port, not working as the parity audit had claimed.

### Fixed
1. **CRITICAL: MP launch crash from a hardcoded x64 dvar-lookup address.** The in-mod frame-pacing limiter (ported 2026-09-16) read a dvar via a hardcoded, SP-only-verified address, reached for the first time under MP because its own hook (`Hook_EndScene`) isn't SP-gated — root-caused via `git worktree` bisection to the exact introducing commit, fixed by gating frame pacing to SP only until a real MP-specific signature exists. Live-confirmed fixed.
2. **Full hardcoded-address audit.** Converted the three remaining hardcoded x64 addresses anywhere in the codebase (`FindDvarX64`, `GetDvarStringX64`, `GetEffectiveFovX64`) to real signature scans, closing out the same bug class that caused the MP crash above.
3. **CRITICAL: a `sprintf_s` buffer overflow crashed the game on launch**, introduced by this session's own new VRAM-diagnostic logging (the sixth real recurrence of this project's own recurring buffer-overflow bug class). Fixed with real, generous margin, and proactively swept the surrounding new code for the same risk.
4. **A genuine, confirmed hardcoded 3072MB (3GB) system-memory detection cap**, unchanged since this engine's 32-bit era: the native hardware-benchmark code correctly detects real system RAM, then unconditionally throws it away and substitutes 3072MB for any system with more RAM than that (virtually all modern systems). Corrected via a POST-hook that only intervenes on the exact cap-fired case, leaving the real function's own legitimate low-memory-warning logic untouched. Confirmed real and shipped; live-tested to not be the cause of the render-scale stutter investigated this session, but a genuine, independent bug fix in its own right.
5. **Render-scale warning threshold corrected to a real, tested number.** Previously fired at a generic ~150%-linear heuristic ported from x86; now fires at exactly 200% linear, the real boundary this session confirmed live (clean at 200%, a severe stutter above it).
6. **Motion blur no longer controller-only.** Its per-tick yaw/pitch delta was only ever fed from the controller-stick and gyro code paths -- real mouse/keyboard look happens entirely inside the native movement trampoline, a separate path these writes never reached, so keyboard/mouse players got zero motion blur regardless of how much they looked around. Replaced with a universal before/after capture of the real native angle accumulators around the native call, which reflects the true per-tick delta from any input device by construction. Build-verified; not yet live-tested.
7. **`ForceAnisotropicFiltering`/`ForceHighQualityShadows`/`ForceHighQualityLighting` confirmed to be silent no-ops on x64** — not fixed yet, but a real, previously-undocumented gap corrected in the record: these three toggles have done nothing at all on x64 since the 2026-09-04 x64 port (SP included, not MP-specific), because the underlying native dvar-write function has no x64 equivalent yet and was deliberately stubbed to a safe no-op to prevent a crash. The parity audit's prior "confirmed working live" verdict for these three was stale x86-era evidence, corrected in place. See `re_notes/known_issues_x64.md` issue #6.

### Groundwork
1. **The render-scale-gated stutter investigated in full depth** (damage, pause, ADS, level-transitions, and an explosion/downed-state transition independently confirmed the most severe trigger) -- five distinct, real mechanisms traced end-to-end and eliminated via live testing or exhaustive static tracing: the SAVED_SCREEN capture's `StretchRect` cost, the memory-detection cap above, the wait-coalescing I/O-burst feature, the HUD command-stream's per-pixel CPU copy loop (opcode 13 in the real dispatch table), and the POST_EFFECT shader-sampler setup chain. Current working conclusion: a genuine native engine stability characteristic tied to large render-target sizes, plausibly why the original PC port locked its internal render resolution to a fixed reference size in the first place. **Confirmed content-dependent, not a single fixed-percentage boundary**: a follow-up live MP test found the SAME `InternalRenderScalePercent` (200%) that SP tolerates cleanly causes CONSTANT lag under Multiplayer, not gated to any specific trigger event the way SP's own repro is — MP's generally denser per-frame scenes (more players, more concurrently-rendered models) sit closer to this same engine-level boundary before any render-scale multiplier is even applied. See `re_notes/known_issues_x64.md` issue #4 for the complete investigation trail.
2. **The classic id-Tech Hunk/Zone memory allocators confirmed genuinely intact** in the x64 binary, with real hardcoded pool-size constants (a 10MB Hunk, a ~300MB Zone) traced to their one real init caller, zero system-memory-adaptive sizing on either architecture, and byte-for-byte identical constants confirmed in `iw5mp.exe` independently -- concrete evidence these are untouched, shared-source, console-era leftovers. The ~300MB Zone was traced to its real consumer, the FastFile content-zone loader.
3. **A real per-pixel CPU copy loop found in the HUD/2D command-stream dispatcher** (opcode 13), a genuine scalar StretchRect-adjacent screen-capture routine reached the same way this project's own motion-blur trigger is -- fully mapped and diagnosed, ruled out as this specific symptom's cause but real, reusable infrastructure knowledge for future render-pipeline work.
4. **Real driver-authoritative VRAM diagnostics added** (`IDXGIAdapter3::QueryVideoMemoryInfo`, replacing reliance on the legacy, widely-unreliable D3D9 `GetAvailableTextureMemory`) and a real disk-I/O diagnostic (`GetProcessIoCounters`), both confirming no VRAM ceiling and no disk-I/O correlation during the investigated stutter.
5. **New reusable Ghidra headless RE scripts** added for future sessions (multi-address caller/reference batch lookups, raw stack-trace/table dumping, signature-byte extraction, and — new this pass — raw byte-pattern scanners for `LEA`/`MOV imm64`/`CALL` instruction references and data-table value occurrences, needed since `-noanalysis` mode leaves many call/data references unresolved for the usual reference-based tools).
6. **The native 3D renderer's real architecture mapped for the first time** — groundwork for a planned future renderer replacement (see `re_notes/x64_migration/renderer_architecture_map.md`, new). Confirmed the engine uses a classic id-Tech frontend/backend split: the per-frame CPU logic queues typed commands into a render command ring buffer rather than issuing D3D9 draw calls directly, and a separate, fully-named profiler/telemetry checkpoint table revealed the real scene-setup pipeline (visibility culling, shadow-caster gathering, draw-surface generation) by name for the first time. Found and fully decoded the complete render-target descriptor table (19 named targets, including `$shadowmap_large`/`$shadowmap_small`) — closing a real gap this project's own visual-suite work (issue #107) had left open since 2026-08-29 (shadow-map creation was never located). A newly-identified reusable frustum-culling primitive was found along the way. The render command buffer's real backend consumer, and the render-target table's own creation-loop caller, were both searched for via multiple real techniques and remain open — flagged honestly as needing heavier tooling (a scoped Ghidra analysis pass, or live debugging) than this project's usual `-noanalysis` convention provides.
7. **A complete, real, previously undocumented native SSAO implementation found** — not a stub or leftover fragment: a full dvar set (`r_ssao`, `r_ssaoStrength`, `r_ssaoPower`, `r_ssaoBlurRadius`, `r_ssaoDownsample`, `r_ssaoDebug`), full-res and downsampled quality tiers, and its own debug visualization mode, all fully built by the original developers and never touched by this project. Currently unforceable (see Fixed item 7 above) — a genuine future visual-enhancement candidate once dvar-writing is restored on x64.

### What's New
1. **Real on-screen render-scale warning at 200%.** Fires once per session, on-screen and dismissible, and never clamps or restricts the setting itself — explicitly worded as hardware-dependent, since more powerful GPUs may have a genuinely higher safe ceiling this project has no way to detect automatically. See `re_notes/known_issues_x64.md` issue #4 for the full investigation trail.
2. **`InternalRenderScalePercent` now works under Multiplayer.** The first real MP-enabled visual-enhancement-suite feature — its hook signature was independently re-verified against `iw5mp.exe` and its own hook body has no dependency on anything SP-only. Motion blur/FSR remain SP-only for now (their required safety-gate signature doesn't resolve under MP yet). Build-verified, not yet live-tested.
3. **Motion blur no longer controller-only** — see Fixed item 6 above; listed here too since it's a real behavior change for keyboard/mouse players, not just a bug fix.

---

## v0.0.1-x64 — Alpha (2026-09-22) — first release on the rebuilt 64-bit line

**Early release -- expect hidden bugs and unfinished or unported features.** Survival is the recommended mode
(Gameplay Complete — every core control is live-confirmed); Campaign ships best-effort, Multiplayer is not supported
yet. The netcode security fixes protect every mode, Multiplayer included.

**Summary:** The first release on the `-x64` line, rebuilding this project
from scratch against MW3's recompiled 64-bit binaries — shipping almost three
weeks after that recompile broke every prior release, rebuilt more resilient
than before, with real fixes and features beyond what the `-x86` line ever
shipped. **Survival's own controller-support scope reached Gameplay Complete
on 2026-09-22**: every core control is live-confirmed working, including
Predator Missile's post-fire guidance (never fixed on either architecture
until now), D-pad, DPV/Goalpost mortar/turret aiming, Campaign QTE button
presses, and cutscene-skip audio. Survival ready-up and buy-station/use
prompts are now full on-screen glyph+text replacements, not just detection.
The visual-enhancement suite's headline features (render scale, motion blur)
are live-confirmed and default-on, alongside three performance techniques
absorbed from `legoliamneeson/MW3_Standalone_D3D9_Project` (frame pacing,
wait coalescing, an `.iwd` read cache). Netcode security patching — four
tracked, genuine, exploitable vulnerabilities in the base game's own netcode
— ships complete, built in by default. Along the way this release closed
several real, previously-undiscovered bugs, including a movement-tick
early-return that silently disabled most controls whenever the stick was
centered, multiple launch-crashing `sprintf_s` buffer overflows, and the
real, three-part root cause of the long-standing "needs a click/input at
launch" bug family (replacing the old pause/unpause workaround entirely).
**One known cosmetic bug ships with this release**: the pause-menu Back
glyph flickers (Back itself still works). Genuinely open, not blocking:
sentry/turret-placement and Campaign QTE prompt *text* (still native),
AC-130 gun-type switching (confirmed GSC/data-driven, no native hook point),
SMAA (implemented, parked off by default), and the Custom Options screen's
real vanilla-setting tabs (deliberately deferred — the INI config already
covers everything this mod needs to expose). See `re_notes/known_issues_x64.md`
issue #1 for live, detailed status on every item below.

### What's New
1. **Every core gameplay control implemented.** Movement, look, Sprint,
   Fire, ADS (true hold-to-aim), Reload, weapon switch, Melee, Lethal,
   Tactical, Jump, Interact, Crouch/Prone (tap vs. hold), pause menu
   open/close, and D-pad actionslot all hook the game's real engine
   functions directly, resolved via runtime signature scanning. Most are
   confirmed working live by direct playtest; see the table in `README.md`
   for exactly which.
2. **Jump auto-stand.** Jumping while crouched or prone now stands the
   player up first, matching console behavior — forces the real
   `togglecrouch`/`toggleprone` case matching whatever the current stance
   actually is.
3. **Auto-unstick.** An automated pause/unpause cycle on level start fixes
   the "needs a click at launch" input-gate issue automatically.
4. **Plugin API ported.** The loading infrastructure needed no code changes
   at all — it was already architecture-neutral. The bundled RGB Text
   example plugin gained its own x64 build configuration.
5. **Custom Options screen wired into the input pipeline.** The screen's own
   draw/navigate code was already cross-platform; a new poll function drives
   it from the same always-on tick Pause's own toggle uses. Its real, native
   open trigger (focus landing on the actual in-game "Options" menu item) is
   now also ported and wired in alongside the original temporary LB+RB
   chord, which stays as a fallback until the real trigger is live-confirmed
   — see item 11 below.
6. **Addressing architecture: runtime signature scanning.** Every hook
   target is resolved via a wildcarded byte-pattern scan against the game's
   own main module, once at process startup and cached for the session —
   see `CODE_STANDARDS.md` for the full policy and rationale.
7. **"Greenlit" trusted-plugin allowlist.** A small, explicit allowlist of
   first-party plugin filenames now load automatically, without requiring
   `[Plugins] Enabled=1` — this project's own `security/` component's
   netcode security-fix plugin ships built in this way by default. Every
   other, arbitrary third-party plugin still needs the normal opt-in — see
   `PLUGIN_API.md` for the full design and its real caveat (filename
   matching isn't cryptographic).
8. **Native D-pad+A/B controller menu navigation.** Previously 100% absent
   on x64 — a controller player could not navigate any native menu (main
   menu, pause menu, options screen, buy-station/armory lists) and needed
   keyboard/mouse for every menu interaction. Now drives the same real
   engine call the game's own ESC key uses to forward input to whatever
   menu is active, resolved via signature scan. Also fixes two real
   conflicts found during the port: D-pad's menu navigation could have
   double-fired against the existing raw D-pad actionslot dispatch, and B's
   menu-back could have toggled real crouch/prone underneath an open menu
   — both now correctly suppress the gameplay-side action while a menu is
   active. Not yet live-tested. See `re_notes/known_issues_x64.md` issue #1
   for the full RE trail.
9. **Vibration/rumble ported to x64.** Previously 100% absent — the code
   that installs it was only ever called from an x86-only-guarded path, so
   it silently never ran, despite not appearing anywhere in this project's
   own prior gap list. Both real mechanisms ported: fire rumble (a real
   engine hook, its x64 target confirmed three independent ways) and damage
   rumble (a per-frame health poll against the real x64 entity array,
   confirmed via three independent consumers computing the same array
   layout). Not yet live-tested.
10. **The visual-enhancement suite ported to x64** — internal render scale,
    FSR 1.0 RCAS sharpening, and camera motion blur. Resolves a target
    (`InternalRenderScalePercent`'s own resolution-compute function) two
    prior static-RE passes couldn't find. All three of x86's own proven-
    necessary safety gates (menu-active, `clcState`, in-level) are wired for
    both FSR and motion blur — deliberately not shipped on a weaker gate
    than x86's own documented crash history (issues #103/#104) proved
    necessary. Not yet live-tested.
11. **Menu-focus/itemDef-array tracking ported to x64.** The underlying
    mechanism controller-glyph icons, the custom cursor, and the custom
    Options screen's real (non-chord) open trigger all depend on — every
    struct offset independently re-derived and cross-confirmed for x64's
    different (64-bit-aligned) layout, not assumed from the x86 original.
    The real Options-screen trigger is now wired (see item 5). **Scope
    note**: this resolves the focus-DETECTION half only — actually drawing
    gameplay glyph icons still needs a separate, not-yet-ported native
    text-draw hook (a different, larger RE task) — see Investigated, Not Yet
    Resolved below.
12. **Survival ready-up (hold Y) ported to x64.** Previously 100% absent —
    the same narrowly-scoped, already-approved exception `-x86` ships (no
    native dispatch was ever found for F5/"skip" after an exhaustive search,
    see `CLAUDE.md`): holding Y for `[Survival] ReadyUpHoldThresholdMs`
    (740ms default) synthesizes a real `WM_KEYDOWN`/`WM_KEYUP` F5 via
    `PostMessageA` at the game's own window; releasing early instead fires
    the normal weapon-switch, same hold-vs-tap split as `-x86`. The extra
    `IsInSurvivalMode()` gate `-x86` also uses — originally omitted, since
    x64's own `Dvar_FindVar` equivalent needed to read the `mapname` dvar
    was a separate, unresolved RE target — is now wired in too (see item 20
    below for the resolution). Build-verified, not yet live-tested. See
    `re_notes/known_issues_x64.md` issue #1 for the full trail.
13. **Hold Breath (L3 while ADS'd, sniper-class) ported to x64.** Previously
    100% absent (parity audit item #23) — L3 only ever drove raw Sprint,
    with no ADS-aware branch to a separate kbutton. Now edge-triggers the
    real kbutton activate/deactivate handlers on a newly-resolved,
    dedicated struct, matching `-x86`'s exact gating (`sprintHeld && adsHeld`,
    no explicit sniper-class check in either platform's own code — the real
    native kbutton is what limits the sway-reduction/accuracy effect to
    sniper weapons). Also fixes a real bug found while implementing this: a
    pre-existing early `return` in the same per-tick function would have
    silently skipped Hold Breath's own edge check on every tick where
    Sprint's own state was steady — restructured so Sprint and Hold Breath
    update as two independent state machines, matching `-x86`'s own design
    shape. Build-verified, not yet live-tested. See
    `re_notes/known_issues_x64.md` issue #1 for the full trail.
14. **DualSense gyro-aim ported to x64, staying PREVIEW/WIP.** Turned out to
    be the same "real mechanism exists, was just never called from the x64
    tick" shape as the vibration/rumble gap this session already closed —
    `dualsense_input.cpp`/`controller_input.cpp`'s gyro-read path has no
    architecture guard at all, it simply wasn't wired into the x64 look
    pipeline. Now applied additively on top of stick-based look (the same
    `+=` pattern `-x86`'s own `InjectControllerLookAngles` uses for its gyro
    contribution), gated by the existing gyro-only-while-ADS toggle. Axis
    mapping and invert-sign handling copied verbatim from `-x86`'s own
    still-unverified-on-real-hardware mapping. Build-verified, not yet
    live-tested (needs real DualSense hardware to exercise). See
    `re_notes/known_issues_x64.md` issue #1 for the full trail.
15. **Back's `+scores` scoreboard key-synthesis ported to x64.** A small,
    cheap wiring fix (parity audit row 30), not new RE work — `-x86`'s own
    `InjectControllerScoreboard()` had no architecture guard at all and
    would compile fine on x64 as-is, it was simply never called from
    anywhere in the x64 input pipeline. Now wired in with the same
    hold-through-passthrough `PostMessageA(VK_TAB)` mechanism, gated on the
    Back button's real physical mapping. **This is intentionally, correctly
    a no-op in Campaign/Survival** — confirmed by direct Xbox 360 console
    testimony that no scoreboard UI exists in SP at all, on any platform
    (`known_issues.md` issue #28) — real value arrives once Multiplayer
    ships with its own actual scoreboard. Build-verified, not yet
    live-tested. See `re_notes/known_issues_x64.md` issue #1 for the full
    trail.
16. **Native text-draw hook ported to x64, with Mantle-hint detection wired
    on top.** This was the single remaining blocker for gameplay controller-
    glyph icons, on-screen hint prompts, the F2/F3 glyph-position editor,
    and Auto-Mantle's own ledge-detection signal (`known_issues_x64.md`
    issue #1's 2026-09-12 "Scope note" round and the separate Auto-Mantle
    investigation the same day). Finds and hooks `FUN_14029a2b0` — the x64
    equivalent of x86's `Hook_DrawGlyphText` target (`FUN_00690c80`),
    confirmed via 22 real callers spanning every kind of HUD/hint text drawn
    on this engine — via the same `RawStringScan.java` anchor technique x86's
    own discovery used. On top of the plain passthrough hook, wires a real,
    language-independent structural match against the LIVE localized
    `PLATFORM_MANTLE` template (resolved via `FUN_14029f120`, the real x64
    `SEH_GetString` equivalent — not `real_settings.cpp`'s x64
    `GetLocalizedString()` stub, which just echoes the key back and would
    never match). **This unblocks Auto-Mantle's own detection DEPENDENCY**
    specifically (see item 17 below for the feature itself) — at the time
    this hook first shipped, no visual glyph-icon substitution was drawn yet;
    see item 18 below for the same-day follow-up that changed this for three
    hint families. Full honest scope, discovery trail, and what's still
    missing: `re_notes/x64_migration/drawtext_hook_x64.md`. Build-verified,
    not yet live-tested. See `re_notes/known_issues_x64.md` issue #1 for the
    full trail.
17. **Auto-Mantle's real `+gostand`-forcing feature wired on x64**, ships off
    by default matching `-x86`'s exact default (`AutoMantleEnabled=0`).
    Composes `IsSprintActiveX64()` from three already-existing x64 tracking
    variables (`g_sprintKbuttonActiveX64`, `GetRealStanceX64()`,
    `g_adsHeldX64`) — an exact parity port of `-x86`'s own `IsSprintActive()`,
    no new reverse-engineering needed, since Sprint's earlier move to a real
    kbutton (item 8's own Sprint work) turned out to relocate the pieces this
    needed rather than remove them. Wired together with item 16's Mantle-hint
    detection into the same real `+gostand` usercmd bit (0x400) Jump already
    uses, with the same 750ms cooldown and forward-stick-cone check `-x86`
    ships. Build-verified, not yet live-tested — see
    `re_notes/known_issues_x64.md` issue #1 for the full trail.
18. **Real glyph-icon visual substitution now draws on x64 for three hint
    families** (same day as item 16, a follow-up pass on top of it): Mantle,
    weapon pickup/swap/pickup-health, and grenade throwback now suppress the
    native hint text and draw this project's own icon+text instead —
    `RequestCustomHintOverlay` actually gets called from x64 for the first
    time. All three are confirmed, via fresh decompile, to flow through the
    same `FUN_14029a2b0` draw call item 16 hooks; detection stays a purely
    structural match against the real, live-resolved reference-key template
    (`PLATFORM_PICKUPNEWWEAPON`/`SWAPWEAPONS`/`PICKUPHEALTH`/
    `THROWBACKGRENADE`, same technique as Mantle), so no font-name filtering
    was needed. A new function, `TryGetPickupGlyphAssetName`, resolves the
    pickup family's icon via the same physical key Reload's own icon already
    uses. **Buy-station, Survival ready-up, Reload, Sentry-Place, and menu
    corner hints remain native/unmodified** — buy-station and ready-up have
    no known reference-key template even on `-x86` (blocked on x64's still-
    unconfirmed `Font_s` `fontName` offset); Reload is confirmed to flow
    through a completely different native draw function this hook can't
    observe; Sentry-Place's own reference string wasn't found anywhere in
    the x64 binary. No position/scale alignment tuning was ported either —
    on-screen alignment for the three working cases is unverified pending
    live test. Full trail: `re_notes/x64_migration/drawtext_hook_x64.md`.
    Build-verified, not yet live-tested. **Superseded — see What's New
    item 22 below**: Reload and every menu corner hint are now covered too.
19. **Highlighted-item A-glyph (menu list navigation) and the F2/F3 in-game
    glyph-position editor wired to real x64 menu-focus tracking** — a
    separate system from item 17's gameplay-hint icon substitution: this one
    draws an A-button icon on whichever native menu list item is currently
    highlighted, using the same manually-calibrated position table `-x86`
    already ships, and the F2/F3 editor is the tool used to build/extend
    that table. Both features only ever depended on one shared debounced
    focus-tracking function, whose x64 branch was still a stub predating the
    real x64 itemDef-array walk built for item 5's own Options-screen
    trigger — now routed to that same, already-working implementation via
    two new thin `extern "C"` wrappers (the functions live in an anonymous
    namespace in a different translation unit). No new reverse engineering.
    Build-verified, not yet live-tested — see `re_notes/known_issues_x64.md`
    issue #1 and `re_notes/x64_feature_parity_audit.md` rows #35/#36.
20. **ADS zoom-aware look-slowdown ported to x64** (`GetAdsLookRateScaleX64`),
    closing parity audit row #3. Its two real dependencies — x64 equivalents
    of `Dvar_FindVar` and `GetEffectiveFov` — were genuinely unresolved RE
    targets until this pass: found via this project's own established
    dvar-value-discovery chain (`FUN_1402c3890`/`FUN_140069e60`, full trail
    `re_notes/x64_migration/getEffectiveFov_dvarFindVar_x64.md`). The formula
    itself is a byte-for-byte port of `-x86`'s own `GetAdsLookRateScale`
    (the power-curve scale plus the close-range taper for low-zoom
    weapons), wired into `Hook_MovementTick`'s Look pre-hook. The same
    `Dvar_FindVar` resolution also closed a second, unrelated gap in the
    same pass: Survival ready-up's `IsInSurvivalMode()` gate (item 12 above),
    previously omitted, now wired at `SendSyntheticF5X64`'s call site.
    Build-verified, not yet live-tested.

21. **Custom mouse cursor overlay ported to x64**, closing parity audit row
    #37 — previously an honest early-return stub (see Fixed item 4 below).
    The real native cursor-visible-flag and UI-state globals were found via
    fresh RE (`kCursorGateSignature`, `analog_input_hooks_x64.cpp`), cross-
    confirmed two independent ways: they're read by a function structurally
    identical to x86's own native cursor-draw dispatcher (same gate/switch
    shape, same literal strings, same `"ui_cursor"` asset), AND they sit at
    the exact same struct offsets from their UI-context base as x86's own
    equivalents do from theirs. A second, previously-invisible gap found
    along the way — the function's menu-active check was silently always
    false on x64, which alone would have kept the cursor from ever drawing
    outside the glyph-position editor — is fixed too. Build-verified,
    not yet live-tested.

22. **Quit/Leaderboards/Game-Summary menu-hint glyphs, plus Special-Ops-modal/
    Friends-list Friends-suppression, now wired on x64** — a menu-hint parity
    follow-up on top of items 16/18/19. The prior claim (this file and this
    hook's own in-code comment) that these depended on "x86-only menu-focus/
    itemDef infrastructure not yet ported to x64" was re-checked against
    `-x86`'s own originals and found stale: `looksLikeCornerHintRow` reads
    only the draw call's own raw screen position (no itemDef dependency at
    all); Quit/Leaderboards/Game-Summary are plain resolved-template string
    compares, same class as the already-working Back/Friends; and the
    Friends-suppression logic needs the focused item's raw NAME, not the
    `(group,index,siblingCount,depth)` tuple the existing x64 menu-focus walk
    exposes — closed with a small, confident extension of that SAME
    already-working walk (`TryGetRealFocusedItemNameX64`, no new RE), then a
    direct port of `-x86`'s own four-iteration sticky-state algorithm on top
    of it. Quit/Leaderboards are gated on position (not font family, which
    x64 still can't confirm) — matching `-x86`'s own BUG-006 precedent that
    position, not font, is the real discriminator that stops a false match
    against a genuine navigable menu item sharing the same label. Nine hint
    categories now visually substitute in total. Build-verified, not yet
    live-tested. See `re_notes/known_issues_x64.md` issue #1 and
    `re_notes/x64_feature_parity_audit.md` row #34.

23. **DPV/Goalpost mortar/Goalpost M2 turret aim — root cause found and a
    fix implemented for the first time on EITHER architecture.** This
    controller-aim bug never worked on the `-x86` line either — genuinely
    new ground, not a parity port. The engine routes aim during these three
    mounted-weapon sequences through a separate per-frame function
    (`FUN_14007de20`) that this project's normal Look hook never runs
    during, so controller aim input had nowhere to go — confirmed via a
    fresh decompile, and also correcting an earlier wrong guess for which
    x64 function was responsible. Fixed with a new, dedicated hook
    (`Hook_MountedAimTick`) that feeds right-stick input into the correct
    native fields. Build-verified, **not yet live-tested** — the mechanism
    is high-confidence; sensitivity/sign are a starting guess pending an
    actual DPV/Goalpost playtest. See `re_notes/known_issues.md` issue #30
    and `re_notes/known_issues_x64.md` for the full trail.
17. **On-screen Multiplayer status warning.** Launching under `iw5mp.exe`
    now shows a real, must-see on-screen warning — through the same
    notifier system "Controller Connected"/"MW32011NCP Started" already
    use, but drawn as a dismiss-required, centered warning-yellow modal
    instead of an ordinary auto-expiring toast, so it can't be missed or
    silently replaced by a routine startup toast racing it (a real gap
    fixed the same day: ordinary toasts previously could clobber an active
    must-see warning outright). **Updated 2026-09-16**: originally read
    "Multiplayer has no functionality working right now" — updated to
    "Multiplayer has no functionality beyond the netcode security
    protections right now" once the `security/` component's netcode-fix
    hooks were confirmed live-installing and firing under `iw5mp.exe` too
    (see the 2026-09-15 MP-hardening entries above). Gameplay hooks still
    aren't installed under MP; the security protections genuinely are
    active. The message swaps in place to "Multiplayer is in pre-alpha and
    will contain bugs and issues. It is not on par with Campaign/Survival."
    once MP gets real partial gameplay support — no new code path needed,
    just a one-line text change when that day comes.
18. **"K+M safe mode" config toggle.** A new, hot-reloadable
    `[General] DisableControllerInput` INI key disables ALL controller/mod-
    side INPUT injection (movement, look, Fire, ADS, Reload, Weapnext,
    Melee, Lethal, Tactical, Jump, Interact, D-pad, CrouchProne, Scoreboard,
    menu navigation, vibration) in one flip, added since keyboard/mouse
    testing has always been comparatively light for this project and this
    gives K+M players a clean way to opt out of any input-side regression
    without losing anything else. Config loading/hot-reload and every
    visual-enhancement feature (motion blur, FSR, render scale, forced
    shadows/lighting) are completely unaffected — those live entirely in
    the render path. Off by default; real keyboard/mouse input always keeps
    working regardless of this setting. Build-verified, not yet live-tested.
19. **Frame-pacing limiter — issue #99's third attempt, ported with credit
    from an external reference implementation.** New `[Video]
    FramePacingEnabled` INI key applies a high-resolution, adaptively-
    corrected wait at the end of every real frame (`Hook_EndScene`), capping
    to the game's own existing `com_maxfps` — structurally different from
    both prior failed attempts: it never writes `com_maxfps` (the second
    attempt's own confirmed failure — the engine treats that dvar
    differently for gameplay than menus) and doesn't use a blind
    fixed-interval wait (the first attempt's own failure). The pacing
    algorithm is ported, with credit, from
    [legoliamneeson/MW3_Standalone_D3D9_Project](https://github.com/legoliamneeson/MW3_Standalone_D3D9_Project)
    — see `README.md`'s Credits section and `frame_pacing_x64.cpp`'s own
    header comment for the full attribution and what specifically was and
    wasn't reused. Off by default; build-verified, **not yet live-tested**.
20. **Wait-coalescing/archive-priority-boost, ported with credit from the
    same external reference implementation.** New `[Video]
    WaitCoalescingEnabled` INI key replaces the game's own coarse-resolution
    `Sleep(1)`/`WaitForSingleObject(1)` busy-polls with a real
    high-resolution wait, but ONLY at three specific, signature-verified
    native call sites (the render thread's own poll, the backend thread's
    own poll, and an archive/job worker's idle wait) — every other
    Sleep/Wait caller in the process, including this mod's own threads, is
    untouched. Also temporarily boosts an archive-loading worker thread's
    priority during a detected read burst, restoring it once idle. Ported
    from [legoliamneeson/MW3_Standalone_D3D9_Project](https://github.com/legoliamneeson/MW3_Standalone_D3D9_Project)
    — see `README.md`'s Credits section and `wait_coalescing_x64.cpp`'s own
    header comment for the full attribution. Off by default; build-verified,
    **not yet live-tested**.
21. **Persistent `.iwd` archive read cache, ported with credit from the same
    external reference implementation.** New `[Video] IwdReadAccelEnabled`
    INI key maps each `.iwd` archive file read-only into this process once,
    on first open, and serves every subsequent read the game's own `.iwd`
    streaming loader makes against it — a real, signature-verified native
    call site, not a blanket "any `.iwd` read" — directly from that mapped
    view instead of a real disk I/O syscall. Deliberately does NOT port the
    source project's own lower-level CRT `_read`/file-descriptor-table path:
    that piece's exact internal struct layout couldn't be independently
    verified against this binary the way every other signature here was,
    and getting it wrong risks real memory corruption rather than just a
    missed optimization — this stays at the real, documented Win32 API
    layer only (`CreateFileA/W`, `ReadFile`, `SetFilePointer(Ex)`,
    `CloseHandle`). Ported from
    [legoliamneeson/MW3_Standalone_D3D9_Project](https://github.com/legoliamneeson/MW3_Standalone_D3D9_Project)
    — see `README.md`'s Credits section and `iwd_read_cache_x64.cpp`'s own
    header comment for the full attribution. Off by default; build-verified,
    **not yet live-tested**.
22. **Survival ready-up: controller glyph overlay, driven by the real prompt.**
    While the native "Press F5 to ready up: NN" prompt is on screen, a
    controller glyph for the ready-up bind is drawn beside it. The prompt is
    detected by a read-only scan of the game's script HUD elements (it is one
    value element labelled `SO_SURVIVAL_READY_UP`), so the glyph appears
    exactly while the prompt is up and disappears when it goes — unlike the
    `survival_player_ready` event, which only fires after the player readies.
    Glyph only: the native text is left untouched (how the client draws script
    HUD elements is not yet mapped, so replacing it is deferred). Default
    position is an estimate; adjust with the F2/F3 hint position editor.
    Build-verified, **not yet live-tested**. Trail:
    `re_notes/x64_migration/ui_text_flow_map.md`.
23. **F4 "AI spawn off" debug toggle ported to `-x64`.** Blocks new enemy
    spawns (`ai_disableSpawn`) — handy for calibration sessions, and as a
    side effect it can trigger an early Survival round transition, which
    makes it a fast way to cycle rounds. Same key and gate as `-x86`
    (`[Experimental] GlyphPositionEditMode=1`, default off, F4 toggles, an
    on-screen "AI = On/Off" readout). On x64 the dvar is written directly
    through the game's own `Cvar_SetInt` (`FUN_1402c5b30`, resolved by
    signature) since the x86 command-buffer address doesn't exist here.
    Build-verified, **not yet live-tested**.
24. **Survival ready-up prompt fully replaced with a controller glyph (`-x64`).** A new hook on the client hudelem text draw (`FUN_140046a30`) hides the native "Press F5 to ready up: NN" text and the overlay draws "Hold [glyph] to ready up: NN" in its place. Applies only in Survival and only when a glyph asset resolves; otherwise the native text is untouched. Live-confirmed drawing correctly.

25. **SMAA 1x edge anti-aliasing (`-x64`), scene-only.** The game ships no working AA. `[Video] SmaaEnabled` adds SMAA 1x (edge detection, blend-weight and neighborhood-blend passes with the area/search lookup textures) before the HUD and menus are drawn, so UI is never filtered; it is skipped while the camera is turning fast enough for motion blur to be active. `SmaaDebugView` 1/2/3 shows the edge pass, blend-weight pass, or a plain capture-redraw test. Off by default; not yet live-tested. An FXAA-style pass was tried the same day and removed: it blurred the whole frame at every setting. SMAA is by Jorge Jimenez, Jose I. Echevarria, Belen Masia, Fernando Navarro and Diego Gutierrez (MIT), credited in `LICENSE` and `README.md`.

26. **`[Experimental] UnboundedDevLog` dev toggle.** Removes the caps and filters on the diagnostic logs (`[x64-fontid-diag]` logs every distinct string, `[x64-hudelem-draw]`, res-scale and auto-mantle diagnostics uncapped) so a capture session can't miss data. The log file is still truncated at every boot. Dev-only; off by default.

27. **Survival buy-station / use-prompt glyphs on `-x64`.** "Hold ^3F^7 to use Weapon Armory / Equipment Armory / Air Support" (and "...to place..." prompts) now get the controller glyph. A live capture showed these reach the native text hook as plain expanded text with no localized template, so they are matched by structure ("Hold|Press ^N<key>^7 to use ...") and drawn through the existing Interact glyph slot, replacing the native text. Build-verified; not yet live-tested.


### Fixed
1. **Crash on launch with the sniper Fire/ADS fix's own log line.** The
   diagnostic message that fix attempt logs on resolving its target
   formatted a 16-hex-digit pointer into a buffer 10 bytes too small,
   which this UCRT fails fast on rather than truncating (surfaced as
   `0xC0000409`, misleadingly labeled `STATUS_STACK_BUFFER_OVERRUN` by
   Windows even though the real cause was a CRT argument-validation
   fail-fast, not a stack-cookie violation). Root-caused via a full
   crash-dump analysis (WinDbg/`cdb` against `%LOCALAPPDATA%\CrashDumps`,
   symbolized against the exact built PDB) rather than Event Viewer alone
   — see `re_notes/known_issues_x64.md` issue #1 for the full trail and
   the reusable diagnostic technique.
2. **D-pad Left's squadmate-call-in exception ported.** D-pad Left now
   synthesizes a real keypress instead of calling the native action-slot
   function directly, matching how a real keyboard press reaches the game —
   a leading fix for a live "sometimes different keys used" report, not yet
   independently confirmed.
3. **A fix attempt for sniper-class Fire/ADS.** Real RE work found that
   every other bind press/release sends a client-side notification the
   game's own scripting layer can react to, which controller Fire/ADS never
   sent; now sends it alongside the existing input logic. **Correction,
   2026-09-13 (live test): the underlying bug is NOT weapon-class-specific
   after all — Fire/ADS fails on the base pistol too, contradicting the
   sniper-specific framing this fix was built around.** See "Investigated,
   Not Yet Resolved" below.
4. **The on-screen cursor was silently non-functional.** It read raw,
   unguarded addresses left over from the 32-bit binary, which safely but
   silently failed against the 64-bit process instead of crashing — fixed
   by gating it off honestly pending a real x64 port of the underlying
   mechanism. **Superseded — see What's New item 21 above**: the real x64
   port has since shipped.
5. **Sprint (L3) was using x86's original, deliberately-abandoned mechanism**
   (forcing the raw `pm_flags`-equivalent bit directly), not the real
   `+sprint` kbutton x86 ultimately shipped — meaning the native sprint
   duration/recovery timer and the Extreme Conditioning perk override never
   applied. Found by a full feature-parity audit against the `-x86` line;
   fixed by driving the same real kbutton activate/deactivate calls already
   used for Fire/ADS/Reload. Also ports the "stand up from crouch/prone on
   Sprint's rising edge" behavior, reusing the same real toggle Jump's own
   auto-stand already calls. See `re_notes/known_issues_x64.md` issue #1 for
   the full RE trail.
6. **Crash on every single launch (2026-09-13), same bug class as item 1
   above, recurring in new code.** A log line added the same day for the
   native text-draw hook's own localized-string-lookup resolve formatted a
   239-character literal into a 160-byte buffer — a guaranteed, not
   conditional, overflow, so the game failed to launch 100% of the time
   once that code path was built and deployed. Root-caused via a live
   crash dump (WinDbg/`cdb`) the same way item 1 was. Given the volume of
   new log lines added across the same session, swept every `sprintf_s`-
   into-fixed-buffer call site added that day rather than fixing only the
   confirmed crash — found and fixed one more guaranteed overflow
   (`InternalRenderScalePercent`'s own resolve log) and two sites
   interpolating raw, unbounded live-resolved game text without the
   truncation (`%.Ns`) this project already uses everywhere else for
   exactly this situation. See `re_notes/known_issues_x64.md` issue #1 for
   the full trail.
7. **A third launch crash from the same bug class, introduced by the
   glyph-position fix below after this same day's own sweep had already
   run.** Confirmed via a second live crash dump. The lesson this
   recurrence forced: a same-day buffer-safety sweep doesn't retroactively
   cover code written after it runs — this needs to be checked per-commit
   going forward, not as a periodic pass.
8. **Motion blur's real x64 trigger hook found and wired.** The
   2026-09-12 fix genuinely wired motion blur's three safety gates and
   its per-frame look-delta feed, but its only real trigger was an
   x86-only raw-`__asm` engine hook that never compiled for x64 at all —
   the gate was armed, nothing ever pulled it, and the parity audit's own
   "FIXED" verdict was a real overclaim that never checked for the
   trigger specifically. Found the real x64 equivalent
   (`FUN_14018def0`) via a new, reusable Ghidra self-recursive-function
   scanner (13,295 functions checked, one real structural match) — unlike
   x86, it uses a plain fastcall convention, so no naked-asm hook was
   needed at all. Live-confirmed 2026-09-14.
9. **Glyph-icon substitution positioning, fixed in two passes.** The
   text-draw hook's captured draw coordinates were wrongly assumed to
   already be the final screen-pixel position — the real transform
   happens in a native function called AFTER this hook's own
   interception point, confirmed via fresh disassembly, producing
   invisible (Mantle) or top-of-screen (Interact/Reload) icons depending
   on how the wrong position happened to land. Fixed by calling the real
   native transform directly. A second pass then ported `-x86`'s own
   already-live-tested empirical nudge constants for the Mantle hint
   specifically, as a first-pass fine-alignment correction on top of the
   now-fixed transform.
10. **Custom mouse cursor overlay wasn't showing at the true main menu.**
    Confirmed via direct log correlation and a fresh decompile: the
    native menu-open state the cursor's own gate depends on has a real
    static writer for every other menu-open case (pause, briefing, buy-
    station, etc.) except the true main menu, which has zero callers
    anywhere in the binary — not a logic bug, a genuine gap in what the
    game itself sets. Fixed by OR-ing in a second, already-proven x64
    signal (menu-stack depth) that correctly covers the main menu too,
    without touching the already-working in-game pause case.
11. **DPV/Goalpost mortar/Goalpost M2 turret aim** — see What's New item 23.
12. **AC-130 gunship-camera look sensitivity now scales with zoom.**
    Previously never scaled to the gunship's own camera zoom (felt "mega
    sensitive" when zoomed in) — the existing ADS look-slowdown formula
    was only ever triggered while a weapon-ADS flag was set, which the
    gunship sequence never sets. Fixed by widening the trigger condition
    to also cover any other real native zoom source (confirmed via
    disassembly that the FOV query already generically covers turret
    zoom), carefully bounded so ordinary hipfire and the already-correct
    ADS case are unaffected. Gun-type switching (105mm/40mm/25mm) was
    also investigated in depth — confirmed entirely GSC/data-driven with
    no native dispatch case to hook, correctly left unfixed rather than
    guessed at against an already-working feature. See
    `re_notes/known_issues.md` issue #40.
13. **Campaign scripted sequences (QTEs) ignoring controller input
    entirely — root cause found and fixed, unifying two previously
    separate reports.** Jump falling through the "Dust to Dust"
    elevator/chopper QTE and a direct "X does nothing during a QTE"
    report share one cause: the script's own detection
    (`notifyoncommand("playerjump", ...)`) only fires on the engine's
    real command-dispatch chain, never on raw usercmd/kbutton state —
    which is exactly how this project's controller Jump works, so the
    script genuinely never learns the jump happened. Fixed using the
    same technique already proven for Survival's own ready-up: a real
    synthetic keypress fired alongside (not instead of) the existing
    input, functionally identical to what a real keyboard player already
    produces. Melee/Lethal/Tactical likely share this bug class but
    weren't part of the live report and weren't touched this pass. See
    `re_notes/known_issues.md` issues #75/#108.
14. **Cutscene-skip audio fixed on both `-x86` and `-x64`.** The real
    engine has a genuine three-way branch for Start's key handler
    depending on cinematic state; x86's own controller handling had
    quietly drifted from that design and unconditionally forced the
    pause menu open regardless of cinematic state (the original
    audio-persists bug); x64's own version was more severe — it always
    called a generic toggle with no cinematic-skip case at all, so
    Start silently did nothing during an actual cutscene. Both now
    route through the real skip chain the engine itself uses. One
    honest, undischarged gap on both platforms: if a given cutscene
    is a GSC-scripted in-engine cinematic rather than a true Bink FMV,
    neither fix touches it — no live-readable flag for that case has
    ever been found. See `re_notes/known_issues.md` issue #98.
15. **SMAW lock-on vs. aircraft** — see Groundwork below; confirmed not
    a bug, not a fix.
16. **Survival ready-up (F5) now shows its own real prompt on `-x64` and
    suppresses the native one, closing a live-reported gap ("ready up
    works but prompt needs to be shown and suppress the old").** The
    native text-draw hook now detects the real "Press F5 to ready up"
    hint and replaces it in place with this project's own icon+text
    ("Hold F5..." — the real verb for how this project's own mechanism
    actually works, a hold not a tap), at the same real screen position
    the native prompt would have drawn (the same accurate draw-location
    transform Mantle/Pickup/Throwback/Reload substitution already uses,
    now applied to this hint too). x86's own font-based safety check
    that protects this text match from false-positiving elsewhere isn't
    available on x64 (a genuine, already-documented RE blocker); a
    different, already-resolved real signal (Survival-mode detection)
    stands in for it instead. QTE prompts and the buy-station hint
    remain unported for the same underlying reason — neither has an
    available substitute signal. See `re_notes/known_issues_x64.md`
    issue #1's newest round.
    **CORRECTION, 2026-09-16/17**: this substitution branch has never
    actually fired on the current retail build — a dedicated diagnostic
    pass (194,701 captured draws) found zero F5/ready-up matches ever
    reached the hook this code lives in; the real native draw call for
    this prompt routes somewhere this project's text-draw hook doesn't
    reach at all. The code above is real and correct as written, kept
    for if/when that draw-pipeline gap closes, but "now shows its own
    real prompt" overclaimed what it achieves today — in practice the
    native prompt has kept drawing unmodified the whole time. See
    `known_issues_x64.md`'s 2026-09-16/17 rounds for the full trail.
17. **Multiplayer (`iw5mp.exe`) no longer crashes navigating menus —
    real groundwork for MP support: gameplay hooks are now gated by
    which game executable actually loaded this DLL.** Live-reported:
    `iw5mp.exe` loaded this DLL fine (XInput polling and other
    exe-agnostic init succeed, hence "controller connected" showing
    even under Multiplayer) but crashed navigating menus. Root cause:
    `iw5sp.exe` and `iw5mp.exe` share the same install directory and
    therefore the same deployed `d3d9.dll`, but every one of this
    project's several thousand lines of signature-scanned gameplay
    hooks was found and verified against `iw5sp.exe` ONLY — this
    project's own standing policy has always been that the two
    binaries are separate reverse-engineering efforts with no assumed
    address/signature parity, but nothing actually enforced that at
    runtime. Hook installation ran completely unconditionally
    regardless of which binary loaded the DLL, so at least one
    SP-verified signature was very likely spuriously matching unrelated
    bytes somewhere in `iw5mp.exe`'s own, differently-compiled code and
    installing a hook at a location that behaves completely differently
    there. Fixed by detecting the real loading executable
    (`GetModuleFileNameA` against the process's own main module,
    compared against the two known real binary names) before any hook
    installation runs: gameplay hooks now only install under confirmed
    `iw5sp.exe`; `iw5mp.exe` (and any unrecognized executable, as a
    fail-safe) skips hook installation entirely while every exe-agnostic
    feature (XInput polling, the plugin loader — including the netcode-
    security plugin, which is meant to protect MP too) continues to run
    normally. This is groundwork, not full MP support: no gameplay hooks
    for `iw5mp.exe` exist yet at all (see `CLAUDE.md`'s MP scope
    decision — static RE first, opt-in-only live/injection work once it
    starts) — this change stops the wrong binary's hooks from ever being
    attempted, it doesn't add new ones.
18. **Real regression from item 17 above, caught the same day via a live MP
    test: the netcode-security plugin's hooks silently failed to install
    under `iw5mp.exe` for an entire real Team Deathmatch session.** A live
    `proxy_d3d9.log` capture showed every one of that plugin's signatures
    resolving correctly, followed by `MH_CreateHook = 2`
    (`MH_ERROR_NOT_INITIALIZED`) — `MH_Initialize()` had never run before
    the plugin's own hook-install attempt. Root cause: item 17's own gating
    change correctly stopped gameplay hooks from installing under
    `iw5mp.exe`, but that installer happened to be the only thing that
    called `MH_Initialize()` before the plugin loader runs — a real gap
    that change didn't anticipate, since it only reasoned about gameplay-
    hook safety, not this separate subsystem's shared MinHook dependency.
    Fixed by calling the already-idempotent `MH_Initialize()`
    unconditionally, before both the SP/MP branch and the plugin loader,
    guaranteeing it always runs regardless of which binary loaded the DLL.
    **Confirmed live** — a follow-up MP session's own `proxy_d3d9.log`
    showed the plugin's hooks installing successfully this time.
19. **Intermittent keyboard Sprint interruptions under Multiplayer, live-
    reported and root-caused the same day.** Direct report following a real
    MP session: Sprint randomly stops triggering, duration dropping to
    under a second, confirmed reproducing across two separate matches (TDM
    and Domination) and confirmed NOT a vanilla issue (only happens with
    this mod running). Traced to `SendPeriodicActivationNudgeX64`
    (`d3d9_hook.cpp`) — a real, repeating (every 2 seconds, for the whole
    session) focus-reassertion workaround (`WM_ACTIVATE`/`WM_SETFOCUS` plus
    real `SetForegroundWindow`/`SetActiveWindow`/`SetFocus`) that was built
    and only ever confirmed necessary for `iw5sp.exe`'s own "needs an
    initial click" issue, but was running completely unconditionally for
    both binaries since 2026-09-04 — MP's own tolerance for it was never
    investigated. A focus-reassertion firing while a key is actively held
    is a plausible, well-reasoned trigger for an engine to treat a
    continuous hold as a fresh press, matching the exact symptom. Fixed by
    gating the call to `iw5sp.exe` only, leaving SP's own already-proven
    behavior completely unchanged. **Confirmed live** — direct user
    confirmation ("fixed") after a follow-up MP session with this fix
    deployed.
20. **Weapon name in the interact/pickup hint now correctly shows as part
    of the substituted overlay, live-confirmed fixed.** Root-caused via a
    live diagnostic and direct user correction: the weapon name (e.g.
    "Model 1887") was never part of the raw text this project's own
    substitution intercepts — it draws through a completely separate
    native call. x86 has a dedicated, already-shipped mechanism for
    exactly this (issue #48/#49: remember the suppressed hint's font +
    row, treat the next matching call as its live continuation, append it
    to the same overlay) that was simply never ported to x64. Ported
    directly — **confirmed live** by the user immediately after deploy.
21. **Back(B)/Friends/GameSummary menu corner-hint position check added,**
    matching the same real precedent already fixed for Quit/Leaderboards
    (a bare text-content match with no position check can hijack a
    genuine navigable menu item sharing the same label). Investigated
    further after direct correction that this did NOT resolve the
    pause-menu flicker specifically — a live diagnostic was shipped
    instead of a second guess; root cause still open, see
    `re_notes/known_issues_x64.md`.
22. **Native cursor draw was never suppressed on x64 — the game's own
    default cursor rendered alongside this project's custom cursor
    overlay.** The 2026-09-13 custom-cursor port only ever resolved WHEN
    to draw our own cursor, never suppressed the native one. x86 has a
    real, dedicated fix (hooks the shared quad-draw primitive the native
    cursor draws through, scoped by exact return address to just that one
    call site) that was never ported. Ported directly. Build-verified,
    not yet independently re-confirmed live.
23. **CRITICAL: pressing B during active gameplay wrongly paused the
    game.** A same-day fix for B not fully closing the pause menu had
    introduced a stale-flag bug — once the flag it relied on went out of
    sync with the real game state, any later ordinary B press during
    normal, unpaused play would silently open the pause menu. Rather than
    patch that flag-tracking approach again (its second real regression
    in one day), both Start's pause open/close and B's menu-back handling
    now synthesize a real Escape keypress instead — the same native key a
    keyboard player already uses for both directions, with the engine's
    own state deciding what to do with it, removing the need for this
    project to track that state itself. See
    `re_notes/known_issues_x64.md` for the full trail.
24. **"Needs an initial click at launch" fixed at its real root cause,
    replacing the pause/unpause workaround.** Decompiling the native
    pause-toggle chain end to end found the real mechanism: unpausing
    calls a native function that force-releases every kbutton the
    engine's own bookkeeping thinks is still held — a genuine stuck-input
    bug, not a window-focus/activation issue. The fix now calls that
    native "release everything" sweep directly, once per level, with no
    pause menu ever opening or closing. See `re_notes/known_issues_x64.md`
    for the full decompile trail.
25. **CRITICAL: both SP and MP failed to launch at all.** A config-summary
    log line's own buffer had grown past its limit as new config keys
    were added incrementally over a long session — this UCRT fails fast
    on a real `sprintf_s` overflow rather than truncating, crashing
    inside `DllMain` before anything else in the mod could run. The same
    bug class as three earlier incidents this project has already hit and
    fixed. Buffer widened with a generous margin; live-confirmed both
    binaries load correctly again. See `re_notes/known_issues_x64.md`.
26. **The real "camera jumps on the first real input at launch" bug found
    and fixed — a native engine gap, not a controller issue at all.** The
    earlier kbutton-release fix (item 24) turned out to address a real
    but separate bug; this one happens on keyboard/mouse too, with no
    controller involved. Root cause: the native mouse-delta baseline is
    never seeded before the first real gameplay tick, so the first delta
    computed is the cursor's own raw screen position applied straight to
    the camera as one large jump. Fixed by seeding the same native
    baseline function ourselves, once per level, to the cursor's current
    position — zero visible movement, pure root-cause fix. **Live-confirmed**
    together with item 27 below. See `re_notes/known_issues_x64.md`.
27. **The missing third piece of the "needs an initial input at launch"
    fix, live-confirmed.** Direct testing found the kbutton-release and
    mouse-baseline fixes above, while both real and correct, didn't fully
    close the bug on their own — a real pause-button press was still
    needed. Added a real, message-queue-routed synthetic Escape keypress
    (the same already-proven-safe mechanism this project uses for other
    menu interaction) at the same per-level trigger point. **The combined
    fix (items 24, 26, and 27 together) is directly confirmed by the user
    ("seamless") — the entire "needs an initial input at launch" bug
    family (stuck kbuttons, camera jump, and the underlying missing input
    event) is resolved, replacing the 2026-09-04 pause/unpause automation
    workaround for good.** Real bonus: a separate, long-standing issue on
    Campaign/Survival mission **restart** (not just first level load) is
    also fixed by this same change, with zero extra code — the fix's own
    trigger detects any transition into live gameplay, not specifically
    "process just launched." See `re_notes/known_issues_x64.md`.

28. **Unbounded diagnostic log growth (perf).** `[x64-fontid-diag]` deduped only against the previous string, so alternating HUD text grew the log without limit (55,842 lines in one session); it now uses a capped distinct-string set. `[overlay-hud][res-scale]` and `[automantle-diag-x64]` are capped, `[x64-video-scale]` is change-only, and the new hudelem-draw log is gated on `HudFontIdLoggingX64`.

29. **Ready-up glyph no longer draws over the pause screen (`-x64`).** The overlay and the native-text suppression are skipped while the gameplay tick is stale (>250 ms), a cheap pause signal that needs no extra RE.

30. **F4 `ai_disableSpawn` toggle was a silent no-op on `-x64`; now works (confirmed live).** The real dvar setter (`FUN_1402c5f30`) drops writes from any thread other than the game's main thread (`FUN_14024a250`), and the F4 poll runs on the render thread. The write is now queued and applied from the gameplay tick, with a read-back logged as `[x64-dvarset]`.

31. **Removed the obsolete 2-second activation nudge for SP.** The periodic `WM_ACTIVATE`/`SetForegroundWindow` workaround for the "needs an initial click" gate was superseded by the native stuck-kbutton release (2026-09-16) and was live-reported as making the game pause itself intermittently. Pending live confirmation that self-pausing stops.

32. **`.iwd` read cache deadlocked the game at launch (`IwdReadAccelEnabled=1` = stuck on splash).** While holding the non-recursive table/slot locks, the cache called `SetFilePointerEx` and `CloseHandle`, which are its own hooked APIs and re-acquire those same locks, so the calling thread deadlocked itself right after logging the first archive view. Those calls now go through the un-hooked originals. Build-verified; not yet live-confirmed.

33. **Motion blur stepped ghost copies.** 8 point-sampled taps over a wide extent produced stacked copies of the scene during fast turns; the pass now uses 24 bilinear taps and a smaller maximum blur extent (0.06 of the screen, was 0.10).

34. **Unreleased config-template bug (caught before release).** The `mw3ncp_config.ini` writer received the new Fxaa values as arguments before their template lines existed, shifting every later value (motion-blur falloff, quality toggles, vibration settings) when the file was rewritten. Template and arguments now match.

35. **Menu "Back" glyph missing on the Survival buy-station popups, and flickering on the pause menu (`-x64`).** The Back corner hint was only substituted when it sat on the standard corner-hint row (y about 995); the buy-station popups draw their own "Back ^2ESC^7" on a different row, so the native text stayed. It now matches the exact resolved `PLATFORM_BACK_SHORTCUT` text anywhere in the bottom band of the screen (design y > 800); matching it anywhere let a second, mid-screen Back-text instance on the pause menu override the real corner one. Separately, menu corner-hint glyphs now re-draw for up to 120 ms on a frame with no fresh request, which removes the flicker where the glyph (and the already-hidden native text) vanished for a frame. Build-verified; not yet live-tested.



38. **Pause-menu Back glyph flicker (`-x64`).** The pause-menu Back glyph is now drawn every rendered frame while the pause menu is up (menu active, gameplay tick stale, client in a level), at a hardcoded position (design 1634.2, 981.7, the standard bottom-right corner; the other native Back draws while paused are offscreen blur-pass instances) instead of depending on the native Back text draw; suppression of the native text stays tied to the native draw. Details of the cause: The pause menu renders its blur/tint through offscreen 2048x2048 targets and EndScene fires for those passes too; the menu-hint requests were consumed on one of them, so the visible pass could have no glyph. Menu hints are now drawn only when the current render target is the real back buffer; Back instances on unknown rows reuse the last good position and every Back instance is suppressed. Build-verified; not yet live-tested.

39. **Level-entry input sweep no longer fires for keyboard/mouse play (`-x64`).** The sweep (kbutton release, mouse-baseline seed, synthetic ESC) now runs only when a controller is connected and there has been no mouse activity for 3 s; otherwise it keeps waiting. Alt-tab, resume and K+M sessions no longer get injected input. Build-verified; not yet live-tested.

40. **Input sweep re-arms only for a level load or window focus loss; "Resume Game" closes fully (`-x64`).** The once-per-level sweep (kbutton release, mouse-baseline seed, ESC) re-armed whenever Pmove was silent for 2 s, which also happens while the pause menu is open, so every unpause re-ran it and re-paused the game. It now re-arms only when the client is in the menu/loading state (`clcState` 0) or the window was deactivated (`WM_ACTIVATEAPP`/`WM_KILLFOCUS`). Separately, A on the pause list's first item ("Resume Game") now closes the menu the way Esc does; forwarding Enter to it only half-closed the menu (UI cleared, blur and pause remained). Build-verified; not yet live-tested.

41. **Gameplay controls no-op while a menu is up and for 400 ms after it closes (`-x64`).** The controller reads made from inside the gameplay hooks (`Hook_MovementTick`/`Hook_SprintTick`) report every button as released while the pause menu is up and for a short grace after any menu closes (menus that leave gameplay running, like Survival buy stations, are not blocked while open), so the press that closes a menu (A on "Resume Game", B for back, buy-station back) can no longer also jump, crouch/prone or fire in gameplay; the unpause can resume the gameplay tick a frame before the menu-active flag drops, which the first (menu-flag-gated) version missed. Menu navigation reads are unaffected. Build-verified; not yet live-tested.

42. **Mod-wide hold + fade for hint glyphs and their text (`-x64`).** Every overlay hint (gameplay hints such as interact/ready-up/mantle, and the menu corner hints) now holds fully visible for 100 ms after its native draw stops being detected, then fades out over 50 ms, and fades in over 50 ms when it first appears. A hint that is detected repeatedly therefore looks solid instead of flickering, and it replaces the earlier fixed 120 ms re-draw guard. Build-verified; not yet live-tested.

43. **Hint glyphs only draw on the visible presentation pass (`-x64`).** EndScene also fires for the offscreen blur/tint passes (2048x2048 targets), and hints drawn there used a different resolution scale each time, so every hint's text texture was re-rasterised at a different font height on alternate EndScenes (heavy, and it read as flicker at the buy stations). Hint drawing and request consumption now happen only when the current render target matches the back buffer size (failing open after 30 consecutive mismatches); requests made during offscreen passes wait for the visible one, and the hold + fade above covers native-draw gaps. Build-verified; not yet live-tested.

44. **Unpause re-paused itself, level restart stuck, and the "{GLYPH}" Back text (`-x64`).** (a) The post-menu input block also hid Start from the gameplay tick only; the pause toggle polls Start from both the gameplay tick and the always-on menu tick with one shared edge tracker, so the next real read looked like a new press and the game re-paused immediately after unpausing. Start and Back are now never blocked. (b) After a level restart the once-per-level input sweep was never re-armed (a restart doesn't pass through `clcState` 0), leaving the new level stuck until a focus event; it now also re-arms after a long Pmove silence with no menu open recently, still never re-arming just because the pause menu was open. (c) When the game treats a gamepad as the active input, the menu corner hints draw a literal "{GLYPH}" placeholder ("Back {GLYPH}") that matched nothing; these are now substituted like the "^N key ^7" form. Build-verified; not yet live-tested.

45. **First-launch welcome modal replaces the obsolete high-render-scale warning (`-x64`).** The on-screen warning about x86 32-bit memory ceilings is gone (this project no longer supports x86). The same dismiss-to-continue modal now shows "Thanks for downloading MW32011NCP" with a short feature list, once per mod version (recorded in `mw3ncp_state.ini`). The modal panel and text canvas were enlarged to fit the list. The feature list and version live in `kWelcomeFeatureList` (`d3d9_hook.cpp`) and `kModVersionString` (`mod_config.h`) and are updated per release (rule added to CLAUDE.md/AGENTS.md). Build-verified; not yet live-tested.

46. **Welcome modal: colour classes and leading symbols/emoji (`-x64`).** The dismiss-to-continue modal text now supports UTF-8 and a small markup: a paragraph may start with a colour marker (warning = orange-red, positive = green, heading = white; unmarked stays yellow) and then with one leading symbol/emoji drawn in a symbol font in a left gutter. The welcome message uses it: a highlighted early-release warning, green check marks for the feature list, and a white heading. Emoji render as monochrome outlines under GDI (colour emoji would need DirectWrite). Build-verified; not yet live-tested.

47. **Three startup messages instead of one (`-x64`).** (1) Once per version: the welcome modal with the feature list. (2) Possibly outdated: on any version below 0.4.0 (the beta milestone) whose build is more than 4 weeks old (compile-time `__DATE__`), a dismiss-to-continue modal asks the player to check GitHub or Nexus for a newer version, at most once per day (recorded in `mw3ncp_state.ini`; `[State] TestOutdated=1` in that file forces it for testing). (3) Every normal launch: the short "MW32011NCP v0.0.1-x64 Started (early release)" toast. Build-verified; not yet live-tested.

48. **Pre-1.0 dev-build watermark (`-x64`).** Every frame draws a small, ~70%-opacity build stamp in the bottom-right corner ("MW32011NCP dev build - DD/MM/YYYY - v0.0.1-x64 (commit)"), the same idea as Fortnite/Rocket League's dev-build corner text. Derived at compile time from the build date, the release version and the exact git commit that built the DLL (a new pre-build step, `gen_git_version.bat`, regenerates a small gitignored header with the short commit hash before every build) -- not a hand-maintained string. Unconditional, no config toggle: remove entirely once 1.0 ships (rule added to CLAUDE.md/AGENTS.md). Build-verified; not yet live-tested.

49. **Predator Missile post-fire guidance now steers on controller (`-x64`, issue #30).** Never worked on either architecture before. Root cause: guidance shares the DPV/Mortar/Turret orchestrator routing bit, so `Hook_MountedAimTick` (not the normal movement/look hook) is the sole writer of the steering bytes -- but its existing right-stick, per-tick-delta design (correct for DPV/mortar/turret's own mouse-delta-style consumer) produced values ~1000x too small for the missile's own absolute-stick-position-style consumer. Now writes the LEFT stick's current absolute position during confirmed missile guidance only; the DPV/Mortar/Turret right-stick path is unchanged. **Live-confirmed working.** Filed as "good enough," not fully polished -- further feel/sensitivity refinement is deferred to a future bulk killstreak-refinement pass, same as every other mounted-weapon/killstreak control in this release. See `re_notes/known_issues_x64.md` for the full trail.
50. **CRITICAL: Multiplayer failed to launch entirely.** A deterministic, 100%-reproducible crash inside native `iw5mp.exe` code on every MP launch. Root-caused via a `git worktree` bisection against real anchor commits (the decisive technique -- static analysis and live crash-dump reading both plateaued first): the frame-pacing limiter (`OnEndSceneFramePacingX64`, shipped 2026-09-16) reads the game's `com_maxfps` dvar via a **hardcoded absolute address**, never signature-scanned and only ever verified against `iw5sp.exe` -- a direct violation of this project's own locked "signature-scan everything, SP/MP share no offsets" policy, which had gone unnoticed because it was the first dvar read to ever run under MP (every other one is part of the SP-gated gameplay-hook install). Under `iw5mp.exe`'s own, differently-compiled binary that address pointed at unrelated code, executed every frame from the very first `EndScene` call. Fixed by gating frame pacing to SP-only, matching the sibling wait-coalescing/`.iwd`-read-cache features' own existing convention. **Live-confirmed fixed.** See `re_notes/known_issues_x64.md` for the full bisection trail.


### Documentation
1. **`re_notes/known_issues_x64.md` established** as the dedicated x64 issue
   tracker.
2. **Full documentation reset.** Every contributor/user-facing doc in the
   repo, the Nexus mod-page copy, and the GitHub wiki were archived to
   `legacy-x86-docs/` and rewritten fresh to describe the current x64-based
   project rather than the discontinued 32-bit line.
3. **Security notice added for unpatched base-game netcode vulnerabilities.**
   MW32011NSP's research confirmed three RCE-class stack overflows in
   `iw5sp.exe`/`iw5mp.exe` netcode survive unchanged into the current x64
   build. Reported to Activision through their official disclosure channel;
   a general risk notice (no exploit-enabling detail) now sits at the top
   of `README.md` pending a fix.
4. **NCP redefined as Native Community Patches; `MW32011NSP` absorbed as a
   nested component.** NCP's own identity expanded 2026-09-12 from "Native
   Controller Project" — name/repo unchanged, meaning redefined, same
   pattern as the earlier 2026-09-03 redefinition — to cover netcode
   security patching alongside controller input and the visual-enhancement
   suite. The former sibling `MW32011NSP` repo's full commit history
   (24 commits, including its original vendor security-disclosure record)
   carried over intact via a `git subtree` merge into this repo's own
   `security/` directory — not a fresh copy. Mechanically unchanged: the
   same three fixes, the same standalone DLL, the same "greenlit" plugin
   loaded the same way — see `security/PATCHNOTES.md` for that component's
   own detailed history, and `CLAUDE.md`'s 2026-09-12 Version Timeline
   entry for the full decision record.
5. **Ko-fi donation link added.** `README.md` and the Nexus page copy
   (`nexus/description.md`/`description.bbcode.txt`) now carry a Support
   section with a Ko-fi button (https://ko-fi.com/officialk8) — purely
   optional, no gated content or features tied to it.
6. **`README.md` given an OpenAssetTools-style badge header** (icon+title,
   a shields.io badge row: release/version, real build status, last commit,
   license, Ko-fi) backed by a real new CI workflow (see Groundwork below) —
   deliberately no fabricated "checks passing" claim until that workflow
   actually existed.
7. **`README.md`'s "Known gaps" section restructured**: was a ~200-line
   wall of interleaved prose (including two already-resolved items still
   sitting in it); now a priority-sorted table (highest-impact/most-blocking
   first) with the full original investigation detail preserved underneath
   in per-item collapsible sections — no content removed, just no longer
   forced into the main reading flow. The top component table and "What
   works right now" table also restructured (the latter split into two
   properly single-purpose tables instead of two unrelated lists forced
   into misleading table rows) for the same reason.
8. **GitHub wiki is now version-controlled and auto-synced.** `wiki/*.md` in
   this repo is the new single source of truth for every wiki page (see
   `wiki/README.md`); a new CI workflow (Groundwork below) republishes it to
   the real GitHub wiki automatically on every push to `main` that touches
   it. Closes a real, previously-manual gap `CLAUDE.md`'s own "Keeping this
   file current" section already documented once (the wiki sitting stuck
   three releases behind with nothing tracking it). Seeded from the wiki's
   actual current live content; `Home.md`, `Known-Issues.md`,
   `Changelogs.md`, and `_Footer.md` were brought current to today's status
   in the same pass (they'd drifted to pre-NCP-redefinition wording); the
   remaining, lower-drift reference pages (Configuration, Compatibility,
   Controller Setup, Installation Guide, Troubleshooting, FAQs, Technical
   Documentation, Development Notes) were carried over as-is.
9. **MP parity release-cadence standard recorded in `README.md`'s own
   tables.** Since Multiplayer never gates a release (already true), it's
   now explicitly allowed to lag Campaign/Survival by 2-4 releases through
   the `v0.4.0-x64` beta milestone — the top component table's
   Release-gating column and the Multiplayer section's own scope paragraph
   both spell out the concrete standard instead of just "fast-follow."
   Explicitly conditional: revisited if Campaign/Survival itself reaches
   full completion before `v0.4.0-x64` ships. Full decision record:
   `CLAUDE.md`'s 2026-09-15 Version Timeline entry.

10. **D-pad squadmate call-in, Left and Right, confirmed working live on `-x64` (2026-09-21).** Direct playtest report. This closes the last "not yet live-confirmed" item for D-pad actionslot / D-pad Left (the Survival AI-squadmate call-in key-synthesis exception).

11. **`[Video] IwdReadAccelEnabled`, `WaitCoalescingEnabled` and `FramePacingEnabled` confirmed working live on `-x64` (2026-09-21).** Each was enabled on its own and then together: all launch cleanly and performance feels good. The `.iwd` read cache's launch hang (a self-deadlock, fixed the same day) is confirmed resolved. With frame pacing on, unbounded diagnostic logging no longer causes hitches. All three now default to on.

### Groundwork
1. **Two new CI workflows.** `.github/workflows/build.yml` — real MSVC
   builds (Release x64) of every component that actually ships to players
   (`proxy_d3d9`, `security/proxy_d3d9`, `security/tools/
   ncp_plugin_netcode_fixes`) on push/PR to `main`, backing `README.md`'s
   real build-status badge. Deliberately does not build `tools/iw5oat` —
   dev-only tooling, never shipped, with its own known premake-generated
   build fragility (see `re_notes/known_issues_x64.md`) out of scope for a
   basic buildability check. `.github/workflows/wiki-sync.yml` — publishes
   `wiki/*.md` to the real GitHub wiki on every push to `main` that touches
   it (see Documentation above). Requires "Read and write permissions" for
   the default `GITHUB_TOKEN` under this repo's Settings → Actions →
   General → Workflow permissions — a one-time manual repo setting, not
   something either workflow file can set for itself.
2. **`signature_scan.h`/`.cpp`** — the runtime AOB byte-pattern scanner this
   entire architecture is built on: parses wildcarded hex patterns, walks
   the game's own PE headers, fails loudly on a zero or ambiguous match.
3. New Ghidra tooling for x64 RE work, including raw-byte reference scanners
   for tracking down indirect references static analysis alone misses.
4. **Full RE trail for the x64 text-draw hook discovery**
   (`re_notes/x64_migration/drawtext_hook_x64.md`) — the `RawStringScan.java`
   → `DecompileAt.java` → `FindCallers.java` → `DumpSigBytes.java` chain
   applied to find `FUN_14029a2b0` (item 16 above) via the same
   "anchor on a real reference-key string, trace forward to the draw call"
   technique x86's own original discovery used, plus a real, independently-
   confirmed x64 `SEH_GetString` equivalent (`FUN_14029f120`).
4. **SMAW lock-on vs. aircraft (Goalpost, `known_issues.md` issue #27 Bug
   #8/task #29) closed as confirmed NOT a bug**, resolving one of this
   project's longest-open Campaign killstreak questions with zero native
   RE needed. Starting from GSC (per this project's own "start from script
   logic first" methodology, using `xensik/gsc-tool`) found the SMAW is
   never referenced by name anywhere in Goalpost's own scripts at all; the
   real answer was in the weapon's own native data file instead —
   `weapons/smaw_nolock` sets `lockonSupported\0`/`guidedMissileType\None`
   directly, a deliberately dumb-fire-only weapon configuration, distinct
   from the genuinely lock-on-capable `weapons/iw5_smaw_mp`
   (`lockonSupported\1`/`guidedMissileType\Sidewinder`) found elsewhere in
   this project's asset dumps. Applies identically on `-x86`/`-x64` and
   regardless of input device — the `.ff` zone/weapon-data files are game
   content, not part of the recompiled native binary.
5. **Real, project-wide GSC-extraction blocker found: OpenAssetTools'
   Unlinker cannot load any real-content zone from this install anymore.**
   Both the already-vendored v0.31.0 and a freshly-downloaded v0.33.0 (the
   latest public release) reproducibly crash (access violation, zero log
   output) loading `ny_harbor.ff`, `hamburg.ff`, and `so_stealth_prague.ff`
   — three different zones, three very different sizes — while a near-
   empty thin-loader zone loads cleanly with either version, ruling out a
   general tool-broken theory. Very likely the 2026-09-03 x64 recompile
   changed the zone/fastfile container format in a way neither current
   Unlinker release parses. This blocks GSC extraction from any real
   content zone project-wide, not just for the investigation that surfaced
   it — flagged here so a future session doesn't re-discover it the
   expensive way. See `re_notes/known_issues_x64.md`'s 2026-09-14 entry.
6. **Full `mw3ncp_config.ini` consumer audit — 120 keys across 15
   sections checked, not just for whether they parse (already confirmed
   arch-neutral), but whether anything on x64 actually acts on each
   value.** This is the exact bug shape today's own session already found
   repeatedly (motion blur, vibration, gyro-aim: config exists, gate
   reads fine, nothing was ever wired to consume it). Found exactly one
   genuine gap — an already-superseded, off-by-default glyph-substitution
   mechanism with no practical impact — and zero vestigial/dead keys.
   See `re_notes/known_issues_x64.md`'s 2026-09-14 entry.
7. **Predator Missile's post-fire guidance phase mapped further than
   either architecture has ever had it, real diagnostic shipped instead
   of a guess.** Independently re-confirmed the guidance script does zero
   per-frame input reads (steering is 100% native), then fully traced the
   real x64 native call chain via fresh decompile down to the exact
   struct offsets and angle-decode math. A cross-reference against the
   concurrent DPV/mortar/turret investigation found the flag this bug
   depends on is structurally distinct from the one that bug hinges on —
   real evidence the guidance phase may already receive controller look
   input correctly, just never provable statically. Shipped a safe,
   cheap, rate-limited diagnostic hook rather than a guessed fix on a
   feature that has never worked on any architecture. See
   `re_notes/known_issues.md` issue #30.
8. **`HudFontIdLoggingX64` diagnostic toggle** — an opt-in, read-only
   live-data-gathering tool for this project's two remaining genuinely
   blocked (not just unattempted) glyph-substitution gaps: buy-station
   (needs `Font_s.fontName`'s real x64 offset, unconfirmed after a
   dedicated static RE pass) and Sentry-Place (its reference string was
   never found anywhere in the x64 binary). Logs the resolved on-screen
   text plus the raw font pointer and a short hex dump at it, for every
   native text draw, so a real live session near either prompt captures
   genuine data instead of another round of static guessing. Mirrors
   x86's own established `HudFontIdLogging` technique. Default off in the
   shipped config template; turned on in this session's own live config
   so the next play session captures it automatically.
9. **`tools/iw5oat` (dev-only, never shipped): fixed a real crash-causing
   bug in `AssetInfoCollector` — two dependency-tracking functions were
   missing the same null-name guard `AssetLoader` already has, causing an
   implicit `std::string(nullptr)` construction that crashes inside
   `strlen`.** Found via a new, safe self-dump technique (a temporary
   in-process `MiniDumpWriteDump` handler, since live x64dbg attach is
   currently confirmed unsafe on this machine) rather than a live
   debugger. See `re_notes/x64_migration/fastfile_format_research.md`
   SS5.46 for the full trail — a second, different crash was found one
   layer deeper and remains open.
10. **`tools/iw5oat`: the 40+ round-old Unlinker "invalid block" parser
    bug root-caused and fixed.** Native decompile (`FUN_140096980`/
    `FUN_140096a50`) found this fork's own `MSSChannelMap`/
    `MSSSpeakerLevels` struct declarations — inherited from upstream
    OpenAssetTools' own x86-era assumptions, never updated for this
    fork's x64 target — read 416 bytes where the real native format
    reads exactly 64. Fixed in the tracked struct header; live-verified
    real progress unblocking every tracked zone. See
    `re_notes/x64_migration/fastfile_format_research.md` SS5.40.
11. **`tools/iw5oat`: forward-reference handling in the zone parser
    extended to several more sibling resolution functions**, converting
    hard `throw`s (aborting the entire zone load) to warn-and-null
    degradation, matching an already-proven-safe precedent verified
    against a full 41-zone sweep. One attempt was live-tested, found to
    segfault, and reverted the same session — a real example of this
    exact code area's own standing caution about rushed changes. See
    `re_notes/x64_migration/fastfile_format_research.md` SS5.41-5.42.
12. **`tools/iw5oat`: real short-read detection.** Every `Load()` call
    site now checks its own actual byte count against the requested
    size and throws a real, actionable exception instead of silently
    corrupting zone data on a truncated read. Permanent hardening, not
    specific to any one bug.
13. **`tools/iw5oat`: a real, VirtualQuery-backed pointer-validity check
    (`Utils/PointerSanity.h`) replaced an earlier bit-pattern
    canonical-address heuristic that had a demonstrated blind spot
    (a garbage value that still falls in the canonical 48-bit range),
    applied at every point in the fastfile parser and XModel export
    pipeline a corrupted zone reference could reach an unchecked
    dereference.** Verified via native `iw5sp.exe` cross-referencing
    (`WeaponDef`, `XModel`, `XModelSurfs`, `XSurface` structs all
    confirmed byte-exact against native, ruling out struct-shape as the
    cause) and live self-dump crash tracing. `common_survival.ff` went
    from crashing after 13 guard catches to producing 260,000+ lines of
    real output with full asset loading completing and real
    xmodel/xanim assets exporting successfully; no regression against
    `sp_dubai.ff`. A separate, distinct stack-buffer-overrun remains
    open further into the XModel/glTF export path — every plausible
    struct and the bone-weight counting logic both checked out correct,
    so this needs a live debugger trace, not more static analysis, to
    pin down.
14. **First real live GSC-VM read-access hook installed and shipped:
    `Hook_VmNotify` (`analog_input_hooks_x64.cpp`), following the
    2026-09-16 policy reversal that unblocked reading live GSC-VM state
    from the main mod.** A read-only, log-and-call-through diagnostic on
    the real x64 `VM_Notify` equivalent (`re_notes/x64_migration/
    gsc_vm_native_functions_x64.md`'s own definitive-confidence finding,
    reached by decoding the real `notify` bytecode opcode's handler
    inside the confirmed interpreter loop) — injects nothing, observes
    every real notify call (owner ID + interned string ID) as it fires
    during actual play. Unlike this codebase's own usercmd-pipeline
    functions, `VM_Notify`'s prologue confirmed a perfectly standard
    Microsoft x64 calling convention, so a plain C++ MinHook detour was
    safe here with no raw `__asm` trampoline needed. **Live-confirmed the
    same day**: 57 real fires captured during actual play, varied real
    data.
15. **`Hook_VmNotify` extended with real interned-string resolution
    (`TryResolveGscInternedString`), so the log shows readable GSC
    identifier text alongside the raw stringId, not just the raw ID.**
    Found via the same native cross-referencing methodology, applied to
    GSC's own compiler source this time: located `OP_GetString = 0x0A`/
    `OP_GetIString = 0x37` in `xensik/gsc-tool`'s own published opcode
    table, then decompiled their shared handler inside the already-
    confirmed `VM_Execute` interpreter loop, revealing a real refcounted
    string-pool table (`tableBase + (stringId << 4)`, 16 bytes/entry,
    refcount at offset 0) behind a fixed-location pointer variable
    resolved at runtime via the existing `SigScan::ResolveRipRelative`
    utility. The read itself reuses this codebase's own established
    SEH-guarded memory-read pattern (`Plugin_ReadMemory`'s convention),
    capped at 63 characters, and only ever logged if every byte up to
    the terminator is printable ASCII. **The offset+4 hypothesis is now
    LIVE-CONFIRMED CORRECT**: a real retest captured genuine, readable
    GSC identifier text — `"death"`, `"goal_changed"`, `"path_changed"`,
    `"runanim"`, `"stop_notetracks"` — real engine notify names, not
    garbage or ASCII-noise false positives, across all 57 captured
    fires (50 in full, then rate-limited heartbeats at count 2000/4000/
    6000/8000/10000). Every single fire resolved successfully, none
    fell back to raw-ID-only. This closes the one remaining open
    question from this feature's own design — the string-pool entry
    layout is confirmed, not just hypothesized. A real, practical next
    step now unlocked by working resolution: correlating notify traffic
    against known Survival actions to finally identify ready-up's real
    native trigger, the original open mystery from issue #5.
16. **Full-session `VM_Notify` dump (19,838 fires, 99.995% resolved)
    found the real native trigger chain for issue #5's own original
    mystery — never found on either architecture before now — plus the
    missing safety-net signal that had blocked buy-station detection
    since 2026-09-13/14.** Live-captured, verbatim: `armory_open` (level)
    → `armory_opened` (player) → `armory_closed` (player) →
    `survival_player_ready` (player) → `survival_all_ready` (level, x2)
    → `wave_started` (level). New, real, working accessors:
    `IsSurvivalPlayerReadyConfirmedX64()`,
    `IsSurvivalAllReadyConfirmedX64()`, `IsArmoryMenuOpenX64()` —
    read-only detection signals, matched against the already-proven
    resolved text (not the raw interned stringId, which isn't guaranteed
    stable session-to-session). **This is DETECTION-only, not a visual
    fix**: `known_issues_x64.md`'s own 2026-09-16 round already
    established both prompts' native text still draws live but never
    reaches this project's own text-draw hook (194,701 captured draws
    that session, zero matches) — the real draw caller remains unfound,
    still needs `x64dbg` (disconnected again this session). Wiring a
    custom overlay off these new signals now would draw ALONGSIDE the
    still-showing native text, not replace it — deliberately not done.
    **Correction to this file's own "Survival ready-up (F5) now shows
    its own real prompt" entry, a prior release section**: that
    `Hook_DrawTextX64` substitution branch is real code but has never
    actually fired on the current retail build, per the 2026-09-16
    finding in `known_issues_x64.md` — the "ported" framing overclaimed
    what shipping it achieved in practice. Full trail:
    `known_issues_x64.md`'s newest round.

17. **Full text draw-path enumeration (2026-09-21).** Every on-screen string reaches the real text renderer `FUN_140080840`/`FUN_140080920` through a small set of callers; only `FUN_14029a2b0` was hooked. Client script hudelems are drawn by `FUN_140046a30` (via the HUD tick, `FUN_1400455b0` and `FUN_140046c00`), which is why the ready-up prompt never reached the text hook. Full table in `re_notes/x64_migration/ui_text_flow_map.md` §9.

### Investigated, Not Yet Resolved
1. ~~**Fire and/or ADS fails — first live playtest of the x64 build,
   2026-09-13, confirmed a real bug, not just a "not yet live-tested" fix
   attempt.** Originally reported and investigated as sniper-class-
   specific (What's New item 3 above); the live test found it happens on
   the base pistol too, directly contradicting the sniper-specific
   framing that fix attempt was built around.~~ **RESOLVED, 2026-09-13,
   same session.** Real root cause found: `Hook_MovementTick` had an
   early-return meant to skip a no-op movement-byte write, but it exited
   the entire function — silently gating Fire, ADS, Reload, Weapnext,
   Melee, Lethal, Tactical, Jump, Interact, D-pad, CrouchProne, Scoreboard,
   the Pause-open poll, and `Rumble_Tick()` behind "is the stick currently
   centered," exactly the condition true the instant a player stops
   moving to aim and fire. x86's own equivalent design calls every one of
   these as fully independent functions with no such gating, confirming
   this was a pure x64 regression from fusing the per-tick functions
   together during the migration, unrelated to weapon class. Fixed by
   scoping the early-out to just the movement write. Live-confirmed
   2026-09-14. The `g_notifyBindDispatch` fix from item 3 below remains
   in place — additive, inert-if-unneeded, not the actual cause but not
   wrong to have shipped either. See `re_notes/known_issues_x64.md`
   issue #1 for the full trail.
2. **UPDATE 2026-09-19/21 — Survival ready-up's own prompt and buy-station's
   use-prompt are now both RESOLVED, full glyph+text replacements, not just
   detection.** Ready-up: the real native draw caller was found
   (`FUN_140046a30`, the client script-hudelem text draw, via a full
   text-draw-path enumeration — see item 17 above) and hooked directly,
   suppressing the native draw and substituting a real glyph + "Hold ... to
   ready up: NN" text. Buy-station/use-prompt: resolved via a structural
   template match ("Hold/Press ^N key ^7 to use/place") rather than needing
   the blocked font offset at all. **Still genuinely native/unmodified**:
   turret placement (Sentry-Place) and Campaign QTE prompts — both remain
   blocked on x64's real `Font_s` `fontName` offset, still unconfirmed
   (needed for `IsGameplayHintFont`-style filtering, since neither has a
   known reference-key template even on `-x86`, and a dedicated
   investigation could not resolve the offset via decompile); Sentry-Place's
   own reference string wasn't found anywhere in the x64 binary. See
   `re_notes/x64_migration/drawtext_hook_x64.md` for the exact scope and why
   each remaining case is blocked.
3. **UPDATE 2026-09-21 — FXAA and SMAA were both actually built and shipped
   this session, correcting the entry below.** `[Video] FxaaEnabled` (an
   edge-blur-only pass) and `[Video] SmaaEnabled` (real SMAA 1x, iryoku
   reference, MIT-licensed and credited) both shipped 2026-09-21, off by
   default. SMAA is **parked**, not viable yet: even a plain capture-and-
   redraw with no SMAA math (`SmaaDebugView=3`) looked worse than off and
   cost far more frame time than expected, meaning the shared capture/redraw
   path the full-screen passes share (also used by motion blur/FSR) is
   itself suspect, independent of the SMAA shaders. FXAA works but is
   limited to blurring jagged edges. See `re_notes/known_issues_x64.md`
   issue #2 for the staged AA/renderer roadmap.
4. **The native low-health "You are hurt, get to cover" TEXT is missing
   on the current retail x64 build — confirmed to be Activision's own
   regression, not caused by this project.** A live-reported symptom
   ("i think the x64 update removed the get to cover message present in
   x86") was root-caused through direct elimination: the red-screen
   vignette itself still shows correctly, ruling out the class of bug
   that broke both together on `-x86` (issue #100); this project's own
   text-draw substitution logic was checked and confirmed to never
   suppress unmatched text; and, decisively, the text stays missing with
   this project's own `d3d9.dll` removed entirely and the real system DLL
   loading in its place — a genuinely vanilla, zero-mod-code test. Nothing
   this project ships can be the cause of a symptom that reproduces with
   the project uninstalled. Likely connected to this same session's own
   independent finding that the 2026-09-03 update changed real zone/asset
   content, not just the executables (see `re_notes/known_issues_x64.md`'s
   matching round). A real candidate for a future RESTORED feature (not a
   fix) once UI work is next prioritized — this project already has the
   native health-ratio detection and the text-overlay infrastructure this
   would need. See `re_notes/known_issues_x64.md`'s newest round for the
   full trail.
5. **Pause-menu Back glyph still flickers.** Purely cosmetic (Back still
   functions correctly) but not yet closed despite heavy iteration
   2026-09-21/22: the pause menu's own Back glyph is now drawn from a
   stored position every frame instead of the native (flickering) draw,
   and buy-station Back settled back on a native-driven, template-text-
   matched approach after a hardcoded-position attempt was tried and
   reverted — but a residual flicker on the pause-menu Back glyph
   specifically is still present as of the latest build. Tracked for a
   follow-up pass.
