# Patch Notes

All notable changes to the project, per release. See
`re_notes/known_issues_x64.md` for the full, actively-tracked issue list and
reverse-engineering trail behind each entry. The prior `-x86` line's own
patch history is preserved in
[`legacy-x86-docs/PATCHNOTES.md`](legacy-x86-docs/PATCHNOTES.md).

---

## Unreleased

**Summary:** First real work toward native Vulkan/DLSS support (see `re_notes/x64_migration/vulkan_dlss_pipeline_research.md`): a new `[Video] GraphicsApi` selector, SP-only-gated real DXVK loading, and a live-confirmed first result — the game genuinely launches and renders correctly through DXVK's D3D9-to-Vulkan translation.

### Fixed
1. **Motion blur was broken for controller (regressed by the v0.0.2-x64 "not controller-only" fix), now fixed for both controller and keyboard/mouse together.** That fix replaced motion blur's controller/gyro-only direct delta capture with a universal pre/post-native-call accumulator diff, intending to add real K+M support — it did, but broke controller: a large single-tick controller-stick delta doesn't round-trip through the native compressed usercmd angle-pack step the same cleanly as a small, natural mouse delta does. Fixed with a hybrid capture (`Hook_MovementTick`, `analog_input_hooks_x64.cpp`): the exact pre-regression direct controller/gyro values are used whenever the stick or gyro contributed this tick, and the diff technique is only used when neither did (pure mouse/keyboard motion, the case it's actually correct for). Live-confirmed fixed by the user, both input methods, on both `GraphicsApi` backends (LegacyD3D9 and Vulkan/DXVK) — see `re_notes/known_issues_x64.md` issue #2's newest round.
2. **Ruled out DXVK as the cause of the above along the way** — a real DXVK build toolchain was stood up and used to test five separate DXVK-source-level hypotheses, all ruled out; the decisive test (K+M motion blur working correctly under Vulkan while controller didn't) proved the bug was never in DXVK's own D3D9-to-Vulkan translation. See `dxvk/re_notes/known_issues.md` issue #1 (now Resolved) for the full corrected record. The "VULKAN 60 FPS 16.6ms" corner text cited in the entry below as "DXVK's own built-in indicator" is corrected here too — it's RivaTuner Statistics Server's own overlay, not DXVK's.

### Documentation
1. **`LICENSE` now separates NVIDIA's components from the project's own grant.** The Third-party components section lists the MIT-licensed Streamline SDK headers, and states that NVIDIA's signed runtime binaries (`sl.*.dll`, `nvngx_*.dll`) are not covered by this project's license: they are governed only by NVIDIA's RTX SDKs License, redistributed unmodified in object-code form, and licensed for NVIDIA GPUs only. The README credit, which still said the binaries would be vendored later, is updated to match.

### Groundwork
1. **`[Video] GraphicsApi` config selector added** (`LegacyD3D9` default / `Vulkan` opt-in) — the locked architecture decision for native Vulkan/Streamline/DLSS support. **SP-only, direct instruction**: Vulkan mode only actually takes effect under `iw5sp.exe`, enforced independently at two points (`CreateDevice` time and before `d3d9.dll` even loads) — Multiplayer holds more VAC risk for a QoL feature that isn't essential there, so this stays SP-only until real precedent exists.
2. **Real DXVK (v3.1.1) vendored and wired to load automatically** when Vulkan mode is selected — the real, maintainer-confirmed integration pattern (a wrapper calling DXVK's own `Direct3DCreate9` directly, not export-forwarding to a renamed DLL), with a validated fallback to the real system `d3d9.dll` if the vendored build is ever missing or corrupted, so a bad DXVK file can never take the whole game down.
3. **First live confirmation: DXVK's D3D9-to-Vulkan translation genuinely works against this game.** With `GraphicsApi=Vulkan` set, the game launches straight to the main menu correctly rendered, cursor working, a stable 60fps/16.6ms — RTSS's own corner overlay confirms Vulkan is actually the active backend. This is the real go/no-go milestone the rest of the Vulkan/DLSS roadmap depends on; deeper gameplay/Survival/visual-suite-compatibility testing is still needed before this is considered broadly stable, and no Streamline/DLSS integration exists yet on top of it.
4. **`MW32011DXVK` fork created** (`github.com/k8se10/MW32011DXVK`, `doitsujin/dxvk` forked and merged in as a real, history-preserving `git subtree` at `dxvk/`), with a real, working native-Windows build toolchain (MSYS2/MinGW-w64/Meson/Ninja/glslang) stood up from scratch — real groundwork for a future genuinely IW5-specific DXVK patch, not itself shipping any DXVK source change yet. See `dxvk/README.md`.
5. **A real, sustained ~30-32fps floor on one specific heavy Campaign mission, investigated and ruled a genuine content-cost characteristic, not a bug.** A live report that a specific mission runs at a hard, constant ~30fps (vs. this project's own typical 120-140fps under Vulkan) raised a real, reasonable theory — a level-scoped `com_maxfps` cap, a known technique for protecting scripted-sequence timing in this engine family. Tested via a real diagnostic (an existing `com_maxfps`-change log line was moved above `frame_pacing_x64.cpp`'s own `framePacingEnabled` early-return so it logs unconditionally, without engaging the actual limiter) and cleanly disproven: `com_maxfps` stayed at `0` for the entire session. `frametime_benchmark.csv` cross-checked the same session and showed this mod's own hooks and texture-creation cost together under 3% of the ~33ms frame budget throughout — the remaining cost is genuine native engine rendering work, holding even at a modest 200% render scale on high-end reference hardware. See `re_notes/known_issues_x64.md` issue #4's newest round.
6. **A real, public vanilla-game report tracked as a pre-release investigation lead**: a `r/mw3` post ("The 64bit campaign is broken") describes a periodic, roughly-once-per-second stutter on a mobile RTX 2060 laptop since Activision's own x64 update — not a `MW32011NCP` regression, but a real candidate mechanism (the already-shipped 3GB memory-detection-cap fix, issue #4) is on record for a future test pass. See `re_notes/known_issues_x64.md` issue #8.
7. **The real native projection-matrix-build function found and live-confirmed** (`FUN_1401e13e0`) — the jitter-injection hook point the future DLSS/Streamline and FSR 3.1 integrations both need. A read-only diagnostic hook (`Hook_ProjectionMatrixBuild`) confirms the signature resolves correctly and the real computed matrix matches the expected D3D9 perspective-projection shape during actual play; the real jitter write itself is intentionally not yet wired — the matrix's full byte layout (whether it's a plain 4x4 or a packed/compressed representation) isn't fully mapped yet, and writing to an unconfirmed offset risks corrupting live render state. See `re_notes/known_issues_x64.md` issue #2's newest round and `re_notes/x64_migration/vulkan_dlss_pipeline_research.md` item 2.
8. **NVIDIA Streamline SDK vendored and initializing (SP, `GraphicsApi=Vulkan` only, opt-in via `StreamlineEnabled`).** The SDK headers are committed under `proxy_d3d9/third_party/streamline/`; the signed runtime binaries are not (licensing) and must be vendored locally. `slInit()` runs from `Hook_CreateDevice` rather than `DllMain` (calling it from `DllMain` hung the game under the loader lock) and is live-confirmed succeeding. DXVK's Vulkan instance/device/queue are then registered with Streamline via `slSetVulkanInfo`, read through DXVK's own `ID3D9VkInteropDevice` interop interface. Registration is refused, with a log line, if DLSS reports needing extra Vulkan queues, because DXVK creates none for Streamline. **`slSetVulkanInfo` is now live-confirmed succeeding too** (updated from this item's earlier "not yet built or live-tested" wording) — see item 10 below for the full trail. Resource tagging is the next unstarted step. See `re_notes/x64_migration/vulkan_dlss_pipeline_research.md` section 2.7 and section 4.4.
9. **Stage 2 (per-object) motion-vector plan corrected: IW5 skins models on the CPU.** The `skin model` render stage selects an SSE CPU-skinning path via the `r_sse_skinning` dvar, so there are no bone matrices in shader constants to capture. Skinned-mesh velocity will come from diffing each skinned surface's CPU-skinned output against its previous frame instead. The separate `+0x164` "bone palette" lead is weakened: its only visible writer copies a view-parameter-shaped record. Also found: stock DXVK 3.1.1 disables the extensions DLSS needs on native Windows and has no device-import API, so DLSS goes through the `MW32011DXVK` fork (see `dxvk/PATCHNOTES.md`). See `re_notes/x64_migration/vulkan_dlss_pipeline_research.md` items 15-16.
10. **The full DXVK-fork/Streamline chain now works end to end, live, for the first time — through `slSetVulkanInfo`, `slGetFeatureRequirements(DLSS)`, and the start of real per-frame tracking.** A dense same-day pass: (a) the DXVK fork build and all four Streamline/NVIDIA binaries are now embedded inside `d3d9.dll` itself as resources and re-extracted fresh to `%LOCALAPPDATA%\MW32011NCP\runtime_x64\` on every launch, replacing loose files a player had to manually place in the game folder (direct instruction: "we shouldnt need to have extra dlls in the game folder. it should all be inside our dll"); (b) a real jitter-injection shadow-pass gating attempt (return-address-based) turned out to be broken and was replaced with a simpler, correct fix — see item 11; (c) a second `MW32011DXVK` fork patch, `DXVK_VULKAN_LOADER_OVERRIDE`, lets DXVK's Vulkan loader route through Streamline's own `sl.interposer.dll` (needed for its mandatory swapchain hooks); (d) a real launch-time bug (`ERROR_SHARING_VIOLATION` extracting `sl.interposer.dll` twice in one launch, once DXVK had already loaded it) was caught and fixed live; (e) **live-confirmed on a real run**: `VK_NVX_binary_import`/`VK_NVX_image_view_handle` both report `1` (previously `0`), `slGetFeatureRequirements(DLSS)` succeeds (zero extra queues needed), and `slSetVulkanInfo()` succeeds; (f) real per-frame `slGetNewFrameToken()` tracking wired into the confirmed once-per-frame `Hook_EndScene` hook point, the next required step before resource tagging. `DXVK_VULKAN_LOADER_OVERRIDE` itself is build-verified only, not yet live-tested against real swapchain interception. See `dxvk/re_notes/known_issues.md` issue #2 and `re_notes/x64_migration/vulkan_dlss_pipeline_research.md`.
11. **View-matrix RE complete and LIVE-CONFIRMED: real camera position + forward/right/up basis vectors found, mathematically verified, and now driving working camera-relative motion tracking.** A real per-frame camera-context block, wholesale-copied into the same render-state struct the already-known projection matrix lives on, was found to contain camera position and an orthonormal forward/right/up basis — confirmed not guessed: all three vectors are unit-length, mutually perpendicular, and "right"'s exact direction matches the textbook `cross(worldUp, forward)` construction. A real per-frame call-pattern investigation (a live diagnostic burst capture) found the actual per-frame structure: ~13 different callers share one identical render-state struct per real frame, followed by 2 calls with position read as exactly zero (a separate, non-camera UI pass) — the originally-assumed "two-call shadow/main split" turned out not to be the live per-frame path at all, so both the jitter shadow-pass gate and the new camera tracking were switched to a simpler, correct check (skip zero position, let real camera data through). New `streamline_camera_x64.cpp` builds a real camera-to-world matrix every frame and tracks genuine previous-frame history, calling Streamline's own real `calcCameraToPrevCamera` reference math. **Live-confirmed**: smooth, physically plausible camera-relative motion values as the player moved and looked around, zero crashes. Honest, not-yet-resolved: whether shadow-map generation shares this same struct (and would still pick up the jitter offset) remains genuinely unknown; a correctly-formed projection matrix for `slSetConstants` also isn't built yet — this engine's own native projection-matrix layout is confirmed nonstandard. See `re_notes/x64_migration/vulkan_dlss_pipeline_research.md` item 17.
12. **`slSetConstants` wired — a full, real `sl::Constants` struct is now built and sent every frame, the last piece before `slEvaluateFeature` itself.** Closed a real, previously-flagged gap first: the motion-vectors buffer is now zero-filled via `IDirect3DDevice9::ColorFill` right after (re)creation, instead of holding D3D9-undefined content — this matters now specifically because `Constants::motionVectorsInvalidValue` (set to the matching `0.0f`) is a real, consumed field once `slSetConstants` runs, not a harmless unused one. The struct itself combines everything built so far this session: the real projection matrix and camera-to-world basis, plus `clipToPrevClip`/`prevClipToClip`, built by manually replicating Streamline's own reference math (`sl_matrix_helpers.h`) against this project's own real previous-frame tracking rather than that reference function's own static, "not for production use" cache. `cameraFOV`/`cameraAspectRatio` are now correctly passed in radians (`sl_consts.h`'s own documented unit). `jitterOffset=(0,0)` is an honest value, not a placeholder — no real 3D-geometry jitter exists yet (the one confirmed jitter target found this session is a 2D compositing matrix, unrelated to the 3D scene). `cameraMotionIncluded=eFalse` (this feature's own Stage 1, camera-only design). Build-verified (0 errors), deployed; **not yet live-tested**. Near/far clip-plane values remain the same unconfirmed placeholders flagged in item 11. See `re_notes/x64_migration/vulkan_dlss_pipeline_research.md` item 17, ROUND 4.
13. **Real per-object (Dynamic Object) transform capture — groundwork for true per-object motion vectors, not just the camera-only baseline.** A dense RE pass found the engine already computes and stores exactly what this feature needs every frame — a real world-space position plus a real 3x3 rotation matrix per dynamic entity (confirmed via the engine's own literal debug string, `"R_AddDObjSurfacesCamera"`), in plain, addressable memory, with no need to hook any function or replicate any skinning math. New `streamline_object_motion_x64.cpp` resolves the real array addresses via signature scan (once, cached) and reads a live current/previous transform snapshot for every active dynamic object every frame, logging real diagnostics for verification. SP-only. Build-verified (0 errors), deployed; not yet wired into the actual DLSS motion-vectors buffer — this is the data-capture step only, real velocity computation is separate, not-yet-started follow-up work. See `re_notes/x64_migration/vulkan_dlss_pipeline_research.md` item 17, ROUND 5.
14. **CRITICAL: a real, severe pre-existing performance bug in the Vulkan/Streamline resource-tagging path found and fixed — a sustained 120fps→12fps regression, live-reported the same day item 13 shipped.** Two rounds of chasing: the first fix (moving item 13's own one-time signature scan off the render loop) was real and correct but not the actual cause. The real mechanism, found via direct log inspection: `TagColorResourceForFrame` called `QueryInterface`+`GetVulkanImageInfo` **unconditionally, on every single `SetRenderTarget(0, ...)` call** — before its own resolution-match filter even ran. DXVK's own doc comment states `GetVulkanImageInfo` "flushes outstanding commands" to report the post-flush layout; `SetRenderTarget(0, ...)` fires 9+ times per real frame (shadow maps, post-process, UI), forcing a real GPU pipeline flush that many times per frame for resources this project almost always immediately discards anyway — invisible to this project's own CPU-side timers since the real cost is a forced GPU stall, not hook-body time. Fixed with a cheap, pure-D3D9 `GetDesc` pre-filter before the Vulkan interop path ever runs. `TagDepthResourceForFrame` had the identical unconditional-flush shape with no filter at all — fixed with a same-D3D9-pointer dedup instead. Build-verified (0 errors), deployed; **not yet re-tested live**. See `re_notes/x64_migration/vulkan_dlss_pipeline_research.md` item 17, ROUND 5 FOLLOW-UP #2.
15. **CRITICAL, real cross-binary confirmation: Activision's own x64 recompile introduced a genuine draw-submission regression, unrelated to this project's own code — issue #4's real root cause, not yet fixed (a base-game issue, not this mod's).** A live stack-capture diagnostic fully explained the rare `EndScene` main-thread fallback (a real `D3DERR_DEVICELOST`/`map_restart` recovery path, `FUN_1401950a0`, confirmed via the exact two's-complement constant match) — but confirmed it's NOT the main driver (4 events vs. 2866 slow frames in the same session). The real signal: a new per-frame render-view fire-count diagnostic found a clean, monotonic ~2-4x more render-pass activations per frame during slow stretches vs. 120fps play (317 samples: 19.1/frame @100+fps → 78.4/frame @0-19fps). **Cross-referenced directly against the real x86 binary, same methodology**: the equivalent function has exactly 2 real callers on x86 vs. 40 on x64 — the same logical render-view-activation function went from 2 call sites to 40 across the recompile. This is real, structural, cross-binary evidence of a genuine Activision x64-port regression, not scene/content cost. Not this project's own bug and not yet fixed — real next step is identifying which specific render pass is being redundantly resubmitted (a sequence-capture diagnostic, logging the actual view indices fired during slow frames, is deployed and awaiting live data). See `re_notes/known_issues_x64.md` issue #4's newest round.

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
