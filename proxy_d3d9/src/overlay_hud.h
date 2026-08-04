#pragma once

// overlay_hud — small top-right on-screen text notifications (2026-07-31, user-
// requested QoL: a "MW32011NCP Started" message on launch, and a matching one for
// config hot-reload). Drawn via raw GDI directly onto the D3D9 backbuffer inside an
// EndScene hook -- this project had no confirmed-alive per-frame render hook before
// this (re_notes/known_issues.md issue #37 flagged this as an open blocker: Present
// is confirmed dead, EndScene was untried). No D3DX/font-atlas dependency -- GDI
// TextOut on the backbuffer's own DC is the standard lightweight technique for this,
// avoiding the whole real-glyph-rendering rabbit hole issues #23/#34/#35 are still
// stuck on for in-engine text.

// Called once from Hook_CreateDevice (d3d9_hook.cpp) with the real IDirect3DDevice9*,
// right after the real HAL device is confirmed -- same call site InstallWndProcHook
// already uses. Hooks EndScene via MinHook.
void InstallEndSceneHook(void* realDevice);

// Live-reported 2026-07-31 (critical findings file): "glyphs and text are moved to
// incorrect pos based on resolution... it should scale based on the current res."
// Root cause: every SIZE constant this project's glyph-icon overlay work uses (icon
// pixel size, gaps, margins, font height) was tuned and validated ONLY at 1920x1080 --
// the one resolution where a missing scale step is invisible. Returns (real backbuffer
// width / 1920, real backbuffer height / 1080) so callers can multiply their own
// 1080p-authored SIZE constants by the right axis. Reads the device's actual
// IDirect3DDevice9::GetViewport (falling back to the game window's GetClientRect only
// if that fails) rather than assuming the window's client rect matches the real
// backbuffer 1:1 -- live-reported 2026-07-31 (second round, 1440p): it doesn't
// necessarily, since this old engine's backbuffer/render-target size isn't guaranteed
// to equal the window's client area. deviceIn may be null (falls back to
// GetClientRect immediately) for call sites without a live device handle.
//
// POSITION is intentionally NOT derived from this scale factor (per explicit
// 2026-07-31 direction: "let's not make res a factor except for size scaling, and just
// do things proportional to the edges + centre of the screen"). Positions are computed
// directly as fractions of the real viewport width/height (screen-edge-relative) or
// centered against it -- see DrawOneGameplayHintSlot's own cursorX/Y math.
void GetResolutionScale(void* deviceIn, float& outScaleX, float& outScaleY);

// Real current backbuffer/viewport width and height in pixels (same ground-truth
// source as GetResolutionScale above) -- for positioning hints as fractions of the
// real screen edges/center, independent of any 1920x1080 reference.
void GetRealScreenSize(void* deviceIn, int& outWidth, int& outHeight);

// Live-reported 2026-07-31: the IDirect3DDevice9::Reset hook added to fix "changing
// display mode crashes the whole game" did NOT fix it -- confirmed via proxy_d3d9.log:
// Hook_Reset's own "Reset() called" line never appears anywhere after a display-mode
// change, but a SECOND real IDirect3D9::CreateDevice call does (d3d9_hook.cpp's own
// CreateDevice log line fires again, with hr=0x00000000, well after the first device's
// install). This engine doesn't call Reset() on a display-mode change at all -- it
// destroys the whole IDirect3DDevice9 and creates a brand new one. Every texture this
// project cached (toast, hint prefix/suffix, glyph icons, debug marker) was created
// against the OLD, now-destroyed device -- using those stale COM pointers against the
// NEW device on the very next EndScene is invalid cross-device resource usage, and is
// the real cause of the "Direct3DDevice9::Present failed: An undetermined error
// occurred" dialog (a graceful engine-side failure, not a raw crash -- matches the
// live report of audio continuing to run under the error dialog). Call this from
// d3d9_hook.cpp's Hook_CreateDevice whenever a HAL device is (re)created and a prior
// device already existed -- releases every cached texture (same underlying release
// path Hook_Reset already used) so they lazily recreate against the new device on next
// use, exactly like a genuine lost-device recovery.
void OnDeviceRecreated();

// Loads Barlow Condensed SemiBold (regular + italic) as a PRIVATE, in-process-only
// font via AddFontMemResourceEx, from the .ttf data embedded directly in this DLL
// (proxy_d3d9.rc/resource.h) -- so overlay text no longer depends on the real font
// being installed system-wide (previously requested from GDI by face name only,
// which silently substitutes a default font if missing). Call once from DllMain's
// DLL_PROCESS_ATTACH, before any overlay text is ever drawn. Returns false (and logs
// why) if either resource/AddFontMemResourceEx call fails -- CreateFontA's own
// system-font fallback still applies in that case, same graceful-degradation
// behavior as before this change, just no longer the expected path.
bool LoadOverlayFonts(void* selfModuleHandle);

// Call once from DllMain's DLL_PROCESS_DETACH, before closing the log file. Releases
// the private font resource(s) loaded by LoadOverlayFonts (RemoveFontMemResourceEx)
// so GDI never holds a reference into this DLL's own mapped memory after it unloads.
// Safe to call even if LoadOverlayFonts was never called or failed.
void UnloadOverlayFonts();

// Visual flourishes for ShowOverlayMessage, added 2026-07-31 as a "vibes" homage to
// WaW's real documented hidden dev clan tags (re_notes/known_issues.md issue #37 --
// GOLD: solid gold tag; RAIN: animated rainbow scrolling through the tag; CYLN: a red
// "laser"/highlight sweep through the tag, letter by letter). Cheap to add now that
// overlay_hud draws a real textured quad: color variants are just the quad's diffuse
// color (modulated with the white-alpha text texture), and Sweep is one extra
// additive-blended pass of the same texture with a narrow scrolling UV window --
// no new texture or font work needed for any of them.
enum class OverlayAnimStyle { Plain, Gold, Rainbow, Sweep };

// Queues a message to draw top-right for durationMs milliseconds, starting now.
// Replaces any message currently showing (only one at a time -- these are meant to
// be brief, infrequent notifications, not a log feed).
void ShowOverlayMessage(const char* text, unsigned long durationMs, OverlayAnimStyle style = OverlayAnimStyle::Plain);

// STRICTLY A TESTING AID (2026-07-31, [Overlay] TestCycleAllVariants in
// mw3ncp_config.ini, default off). Call every tick (analog_input_hooks.cpp's
// InjectMenuInputTick already does) -- a no-op unless that config toggle is on, in
// which case it continuously cycles through every known message/style variant every
// few seconds, each held for its full interval with no early timeout, so every
// variant can actually be inspected on demand instead of waiting on the real 1-in-20
// startup RNG roll. Never enable the underlying config toggle for normal play.
void TickOverlayTestCycle();

// ---- Controller-glyph icon overlay (issue #48, 2026-07-31) -----------------------
//
// Requests a controller-glyph icon (one of the real PNGs in assets/button_glyphs/,
// e.g. "xbox360_x") be drawn THIS FRAME as a screen-space quad at (x, y), sized
// (w, h) -- all already in real screen pixels, computed by the caller
// (analog_input_hooks.cpp's Hook_DrawGlyphText) from the real draw call's own
// position/scale params plus the font's own glyph-advance metrics, so the icon lands
// exactly over the button-name character(s) it's replacing visually (not literally
// replacing the underlying text draw -- this draws ON TOP of it, same layering the
// startup/config-reload toast already uses). Safe to call every frame a hint is on
// screen: state is a single "current request" that must be re-requested every frame
// to keep showing (drawn once per EndScene, then cleared) -- if a hint stops being
// resolved, its icon disappears within one frame with no separate teardown call
// needed. Loads and caches the PNG texture (via WIC, decoded once per assetName,
// kept for the DLL's lifetime) the first time each distinct assetName is requested.
void RequestGlyphIconOverlay(float x, float y, float w, float h, const char* assetName);

// Requests THIS project's own complete replacement rendering for an in-game hint
// (2026-07-31 pivot, issue #48/#49): prefixText, then the real controller-glyph icon,
// then suffixText, drawn sequentially at (x, y) using this project's own embedded
// font -- NOT an overlay on top of the game's own text draw. The caller
// (Hook_DrawGlyphText) is expected to suppress the real draw call entirely when it
// uses this, since there's no game-drawn text left underneath to align against.
// Empty prefixText/suffixText are valid (a hint that's ALL icon, or has nothing after
// it). Safe to call every frame a hint is on screen; expires if not refreshed, same
// "current request" model as RequestGlyphIconOverlay above.
//
// centerOnScreen (live-reported 2026-07-31): if true, x is IGNORED for the final
// horizontal position -- the whole composite (using its real measured content width)
// is centered on the real screen width instead, since most of these hints read
// better centered than left-anchored at the game's own raw coordinate. If false, x is
// used as the literal left edge (e.g. the mantle/jump hint, which is instead nudged
// to sit near a separately-drawn native sprite at a specific screen position, not
// screen-center).
//
// flashIcon (live-reported 2026-07-31, the Reload prompt): the real native reload
// reminder has no button-name text at all -- it just flashes the bare word "Reload"
// -- unlike every other hint's real "^N...^7"-highlighted phrasing. Matching
// console's own look for this one (button glyph blinks, the surrounding text stays
// solid) needs actual per-frame animation, not just a static composite -- when true,
// the icon's own opacity pulses over time (GetTickCount()-driven); prefix/suffix text
// always draws at full opacity regardless.
//
// BUG-004 follow-up (2026-08-02, stream co-op report): this used to be a SINGLE
// shared slot for every gameplay hint (interact/pickup/mantle/ready-up/reload),
// under the same "only one is ever on screen at once" assumption the menu-hint pool
// comment above already documents being wrong for menu UI -- live co-op testing
// showed it's ALSO wrong here: Survival's ready-up prompt and a real interact hint
// (or the Reload reminder) CAN legitimately be on screen the same frame, and
// whichever happened to be the last Hook_DrawGlyphText call that frame silently won
// the one slot, making the other's custom text/glyph vanish. Fixed the same way the
// menu-hint bug was: named, independent slots (GameplayHintSlotId) that each keep
// their own state and draw every frame they're requested, by default coexisting --
// NOT a "pick one winner" priority scheme. The one genuinely-wanted suppression
// (hide the Reload reminder specifically while ready-up is showing, since the two
// together are redundant clutter) is applied explicitly at draw time, replacing the
// old, unreliable "was an interact hint active in the last 100ms" heuristic (itself
// a source of "Reload occasionally fails to display" per the same report) with a
// same-frame check that can't race.
enum class GameplayHintSlotId { Interact = 0, ReadyUp = 1, Reload = 2 };
constexpr int kGameplayHintSlotCount = 3;

// topLineText (2026-08-02): some real native hints turn out to be a SINGLE draw call
// combining two logically separate lines with an embedded '\n' -- confirmed via a live
// log capture of Survival's ready-up prompt when a teammate has already readied up:
// "Teammate ready\nPress ^3F5^7 to ready up: 23" is ONE string, ONE color-highlight
// span, not two separate hints. This project's own hint renderer has no concept of an
// embedded newline (it draws prefix/icon/suffix as one horizontal line), so passing
// that raw text straight into prefixText either garbled the line (rendering the
// newline as part of the text) or, after the "Hold Y" fix, silently dropped
// "Teammate ready" entirely. topLineText (optional, empty = no top line) draws as its
// own plain text line directly above the main hint row, so the caller can split on the
// embedded newline instead of losing or garbling either half -- kept generic (not
// ready-up-specific) in case another hint turns out to have the same combined-string
// shape.
void RequestCustomHintOverlay(float x, float y, const char* prefixText, const char* suffixText,
                               const char* assetName, bool centerOnScreen, bool flashIcon = false,
                               GameplayHintSlotId slotId = GameplayHintSlotId::Interact,
                               const char* topLineText = "");

// Appends extraText to the CURRENTLY pending hint's suffix in the given slot (e.g. a
// weapon name that draws as its own separate, unhighlighted continuation right after
// the interact hint's own text -- see analog_input_hooks.cpp's continuation-matching
// logic for how that's detected). No-op if that slot wasn't requested this frame, so
// a stray/unrelated call can't corrupt an unrelated later request. Reads the real
// string live -- nothing about the appended text is hardcoded, it's whatever the
// caller actually observed on screen.
void AppendCustomHintSuffix(const char* extraText, GameplayHintSlotId slotId = GameplayHintSlotId::Interact);

// Menu-hint counterpart to RequestCustomHintOverlay above (2026-08-01, live-reported
// bug: "Friends doesn't show on some screens" / "Friends stays on screen when it
// should say Back"). Unlike a gameplay interact hint (confirmed all session to only
// ever have ONE on screen at a time), MW3's menu UI shows a persistent legend bar --
// multiple hints (e.g. "Back" AND "Friends") drawn simultaneously, every frame, as
// separate calls. RequestCustomHintOverlay's single slot could only hold one of
// them per frame, silently losing whichever call didn't happen to run last. This
// APPENDS to a small pool of slots instead (up to 4 simultaneous menu hints per
// frame) so multiple menu hints can coexist. Always left-anchored at (x, y) --
// menu hints never center on screen or pulse (no Reload-style prompt exists in menu
// UI), unlike the gameplay version above.
void RequestMenuHintOverlay(float x, float y, const char* prefixText, const char* suffixText,
                             const char* assetName);

// TEMPORARY debug aid (2026-07-31, issue #48 position-tuning round) -- draws a small
// solid-colored 8x8 marker centered exactly at (x, y), no further offset/scale
// applied by the drawing code itself, so a live screenshot can show what a given
// position HYPOTHESIS actually lands on relative to the real text on screen. Slot is
// 0-3 (up to 4 simultaneous markers, each a different color, so multiple position
// hypotheses can be tested in ONE live round instead of guessing sequentially).
// Remove once the real coordinate-space convention is confirmed and this no longer
// needs visual verification.
void RequestDebugPositionMarker(int slot, float x, float y);

// ---- Custom in-game options overlay (2026-08-04) ----------------------------------
//
// A fully custom-drawn settings screen, entirely in this project's own overlay layer
// -- NOT native menu content injection (that path is confirmed unsafe for real
// content outside the engine's own controlled load context, see
// re_notes/known_issues.md issue #23).
//
// REWORKED same day (live feedback: "the button should be called from the native
// options button we no longer need the individual mw32011ncp options seperate") --
// the original design (an extra "MW32011NCP OPTIONS" row appended below the real
// OPTIONS_LIST tab bar, reached by scrolling past its last item) is GONE. Invocation
// is now the real pause menu's own "Options" button itself: itemDef `PAUSE_LIST_1`
// (confirmed via `pausedmenu.menu` -- index 0 is Resume, index 1's real action opens
// `pc_options_video_ingame`). The caller (`InjectControllerMenuNav`) detects real
// focus on that exact button (group "PAUSE_LIST", index 1) and a real A press, and
// passes that as `openRequestedEdge` -- claimed here (opens the custom menu, returns
// true) BEFORE the real pause menu ever runs its own "open Options" action, so the
// real Options screen is never entered at all in this flow. There is nothing left to
// append a row to; the pause menu's own "Options" IS this screen now.
//
// Called once per InjectControllerMenuNav tick (analog_input_hooks.cpp) with this
// tick's raw edge states (true = rising edge, pressed this tick). Returns true if it
// claimed this tick's input entirely -- the caller must skip its own
// ForwardKeyToMenu() calls for this tick when true, since the real native menu
// should see nothing while this system owns the D-pad.
//
// tabPrevEdge/tabNextEdge (issue #66 full-scope pivot): LB/RB rising edges,
// switching between the replacement screen's own tabs (Controller/Look/Voice/... --
// see overlay_hud.cpp's UnifiedTab). Only meaningful while the full menu is open;
// ignored otherwise.
bool CustomOptionsMenu_TickInput(bool openRequestedEdge,
                                   bool upEdge, bool downEdge, bool leftEdge, bool rightEdge,
                                   bool selectEdge, bool backEdge, bool tabPrevEdge, bool tabNextEdge);

// Call whenever the real menu system closes entirely (IsMenuActive() goes false) --
// resets the menu-open state so a later menu-open always starts fresh rather than
// reopening mid-interaction.
void CustomOptionsMenu_ResetOnMenuClose();

// Draws the custom Options menu if open -- the real per-frame draw call, normally
// only reached via Hook_EndScene inside the game. Exposed here so tools/ui_harness
// (a standalone D3D9 window with no game/hooks at all) can call the EXACT same
// drawing code the shipped game uses, guaranteeing zero drift between what's
// previewed there and what actually ships. Safe to call every frame; no-ops
// entirely when the menu isn't open.
void RunCustomOptionsMenuHarnessFrame(void* device);

// True while the full custom menu is open. Used by InjectControllerMenuBack
// (analog_input_hooks.cpp) to skip its own real ESC-forward while this overlay owns
// B -- since the real pause menu was never told to close in this flow (see above),
// a single B press must close only this overlay and reveal the still-open pause
// menu underneath, not also forward a real ESC.
bool CustomOptionsMenu_IsOpen();
