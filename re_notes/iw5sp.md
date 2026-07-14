# iw5sp.exe — RE notes (Campaign + Survival)

## Launch/testing environment
- **Steam App ID: 42680** (`Call of Duty®: Modern Warfare® 3 (2011)`) — confirmed via `appmanifest_42680.acf` in `D:\SteamLibrary\steamapps\`.
- Launching `iw5sp.exe` directly (not through Steam) fails fast: `SteamAPI_Init` has no App ID context, process exits within a few seconds. Fix for standalone test launches: drop a `steam_appid.txt` containing `42680` in the game root (Steam client must still be running). This is a standard Steamworks dev/testing convenience, not a DRM bypass — the Steam client still enforces ownership.
- On a successful launch the process **re-execs itself under a new PID** at least once (observed: `Start-Process` PID exits, a new `iw5sp` PID takes over holding the actual game session) — don't assume the PID returned by the launcher call is the one to monitor; re-check `Get-Process -Name iw5sp` after a few seconds.
- Confirmed reaching a live, responsive main window: `Get-Process | Select MainWindowTitle` shows `"Call of Duty®: Modern Warfare® 3"`, `Responding = True`.

## proxy_d3d9.dll verification (2026-07-13)
- Log file: `proxy_d3d9.log`, written next to whichever `.exe` loaded the DLL (game root). Opened in append mode across the process's lifetime — if you need to read it while the game is still running, a plain read from another process may hit a sharing violation; simplest is to wait for the process to exit, or read after `CloseMainWindow`.
- Confirmed via log: `Direct3DCreate9` gets called **multiple times per launch** (3–4 observed) — the engine appears to probe/reinit D3D9 more than once during startup (adapter enumeration and/or windowed-mode shim behavior). Don't assume a single call.
- Full pass-through proxy (forward everything, implement only `Direct3DCreate9` as a logging wrapper around the real call) loads cleanly and the game behaves identically to vanilla through Campaign/Survival's front-end — no crash, no visible regression.

## Not yet done
- No hooking of `IDirect3D9::CreateDevice` or `IDirect3DDevice9::Present` yet (task #3 follow-up / task #4 groundwork) — proxy is currently a transparent pass-through only.

## Input/usercmd pipeline — RE findings (2026-07-14)

Method: Ghidra headless (`re_notes/ghidra_scripts/`) — `FindInputRefs.java` scans every
defined string for input/cvar-name substrings and ranks functions by how many distinct
target strings reference them; `FindGlobalRefs.java` does the same for specific resolved
global addresses (cvar storage pointers) once found; `DecompileFuncs.java` dumps
pseudo-C for a given list of function addresses. Ghidra project: `D:\Tools\ghidra_projects\MW3`.
**Note:** this Ghidra install has no PyGhidra/Jython — scripts must be Java `GhidraScript`
subclasses (`.java`), not `.py`.

### Cvar registration (bridge from string names to runtime globals)
`FUN_004292f0 @ 004292f0` is the client cvar-registration function (analogous to
`CL_InitInput`) — every input-relevant cvar is registered here and its resulting
cvar-storage global recorded:

| cvar | storage global | register fn |
|---|---|---|
| `cl_yawspeed` | `DAT_00a98ac0` | `FUN_004f9cc0` (float, min/max clamp) |
| `cl_pitchspeed` | `DAT_00a98ad4` | `FUN_004f9cc0` |
| `cl_anglespeedkey` | `DAT_00a98d08` | `FUN_004f9cc0` |
| `m_pitch` | `DAT_00aa4084` | `FUN_004f9cc0` |
| `m_yaw` | `DAT_00aa4080` | `FUN_004f9cc0` |
| `m_forward` | `DAT_00b36200` | `FUN_004f9cc0` |
| `m_side` | `DAT_00b363a4` | `FUN_004f9cc0` |
| `m_filter` | `DAT_00b363a8` | `FUN_004914d0` (bool) |
| `sensitivity` | `DAT_00aa407c` | `FUN_004f9cc0` |
| `cl_mouseAccel` | `DAT_00b36208` | `FUN_004f9cc0` |
| `cl_maxpackets` | `DAT_00b3620c` | `FUN_0048cd40` (int, min/max clamp) |

**ABI pattern confirmed everywhere:** the value returned by these register calls is a
`cvar_t*`-equivalent pointer, and the live numeric value always sits at **offset `+0xc`**
(`*(float*)(cvarPtr + 0xc)`), regardless of int/float/bool cvar type. Any future hook
code that needs a cvar's current value can dereference `+0xc` on these stored globals
directly instead of calling a getter.

### Input pipeline — function cluster at 0057d1xx–0057e3xx
Found by cross-referencing the resolved cvar globals above (not the string literals) —
this is almost certainly one source file's worth of functions (client input handling),
laid out in original-source order:

- **`FUN_0057d680 @ 0057d680`** — raw OS mouse-delta reader. Reads from a double-buffered
  accumulator (`in_EAX+4/+8/+0xc/+0x10`, toggled by a flip index at `in_EAX+0x14`), then
  applies **scriptable** scale hooks: calls `FUN_00493b80("CL_MouseMovement_ScaleX")` /
  `("CL_MouseMovement_ScaleY")` — these look like GSC-exposed script hooks for scaling
  mouse input, worth investigating later as a possibly even-lower-risk injection point
  than a binary hook. Uses a custom/register-based calling convention (relies on a
  caller-set `EAX` on entry, not a normal stack/fastcall arg) — hooking this needs care
  about calling convention, not a plain C-signature hook.
- **`FUN_0057d740 @ 0057d740`** — takes the raw delta from `FUN_0057d680`, applies
  `cl_mouseAccel` and `sensitivity`, plus what looks like an existing per-context
  zoom/ADS-sensitivity multiplier (`*(float*)(unaff_EBX + 0x48)`) — i.e. **the game
  already has ADS-sensitivity scaling wired through this exact function.** Outputs the
  final scaled (dx, dy) mouse delta for the frame.
- **`FUN_0057d7e0 @ 0057d7e0`** — consumes the scaled delta from `FUN_0057d740`. Branches
  on a per-context flag (`DAT_00a98b88`, looks like a freelook/strafe-mode toggle):
  - **freelook (normal) path:** `mouseX * m_yaw->value` and `mouseY * m_pitch->value`
    accumulate into two globals, `_DAT_00b3640c` (**yaw** delta) and `_DAT_00b36408`
    (**pitch** delta) — these are the per-frame view-angle deltas, not the final view
    angles.
  - **non-freelook path:** `mouseX * m_side->value` writes directly into a byte at
    struct-offset **`+0x1d`**, and `mouseY * m_forward->value` into offset **`+0x1c`**
    — **confirms `usercmd_t.rightmove` is at `+0x1d` and `usercmd_t.forwardmove` is at
    `+0x1c`** on the struct pointer this function receives (referred to as `unaff_ESI`
    in the decompile — register-passed, not a normal stack arg).
  - Also confirms **`usercmd_t.buttons` (a `uint`) is at offset `+4`**, OR'd with
    button-bit constants (`|= 1`, `|= 8`, `|= 0x20`, `|= 0x100000` seen so far — exact
    `BUTTON_*` bit meanings not yet mapped).
  - There's also IW-specific compressed byte encoding beyond vanilla usercmd_t — bytes
    at `+0x3e`/`+0x3f` on the same struct get written from quantized angle deltas in a
    *different* function (`FUN_0057e360`, see below) via `FUN_0057e300` (looks like an
    angle→byte quantizer, ANGLE2BYTE-style) — likely the CoD melee-lunge/knife auto-aim
    direction encoding (`meleeChargeYaw`/`meleeChargeDist`-style field), not core
    movement/look. Lower priority to fully decode right now.
- **`FUN_0057d300 @ 0057d300`** — the keyboard-hold-turn equivalent (classic
  `CL_AdjustAngles`): `cl_yawspeed`/`cl_pitchspeed` scaled by frametime and
  `cl_anglespeedkey` (when running), accumulating into the **same** `_DAT_00b3640c`
  (yaw) / `_DAT_00b36408` (pitch) globals that the mouse path (`FUN_0057d7e0`) also
  writes — confirmed: mouse and keyboard-turn share one per-frame accumulation point.
  (Indexed by a per-player stride `iVar4` here, unlike the bare-global access seen in
  `FUN_0057d7e0` — not yet reconciled, likely a calling-context difference; needs
  live-debugger confirmation, see Next steps.)
- **`FUN_0057d110 @ 0057d110`** — unrelated to angles; a `cl_maxpackets`-based
  "is it time to send another command yet" throttle check (`CL_ReadyToSendPacket`-style).
  Not needed for movement/look work, noted for completeness.
- **`FUN_0057e360 @ 0057e360`** — per-frame button-state latch (OR's `buttons` bits
  from a set of small state bytes at `DAT_00a98c00`/`c64`/`c8c`-ish addresses) plus the
  melee-lunge byte encoding mentioned above. Not yet fully understood; lower priority.

### Hook strategy implications (for task #5)
The cleanest analog-look injection point is **`FUN_0057d680`** (the raw mouse-delta
source) rather than reimplementing `m_pitch`/`m_yaw`/sensitivity/ADS-scaling ourselves —
feeding a synthetic delta in here means every existing feel/scaling system (accel,
sensitivity, the ADS zoom-sensitivity multiplier already found in `FUN_0057d740`)
applies to controller input automatically. Analog **movement** (left stick →
forwardmove/rightmove) most likely needs a different, not-yet-found hook, since
`FUN_0057d7e0`'s forwardmove/rightmove path only fires in the non-freelook branch (mouse
strafing is an edge case, not the primary movement source) — the primary keyboard-move
accumulation function (reading `+forward`/`+back`/`+moveleft`/`+moveright` kbutton state
into forwardmove/rightmove) has not been located yet.

### Next steps (superseded — see "Full per-frame usercmd pipeline" below, found 2026-07-14)

## Full per-frame usercmd pipeline — CONFIRMED (2026-07-14)

The complete per-frame call chain is now traced end to end. Top-level orchestrator:

### `FUN_0057e480 @ 0057e480` — the `CL_CreateCmd` equivalent
This is **the** per-frame usercmd-build entry point and the natural top-level hook site:
```
memset(cmd, 0, 0x40)                          // usercmd_t is 0x40 (64) bytes
FUN_0057d300()                                 // keyboard hold-turn -> angle-delta floats
[if paused/menu-open flag set: melee-charge encode path (FUN_0057e360), early return]
FUN_0057dc90()                                 // button-bit summer -> cmd.buttons (+4)
FUN_0057d430()                                 // keyboard analog movement -> forwardmove/rightmove/+0x1e/+0x1f
FUN_0057d7e0(fovScale)                         // mouse-look / mouse-strafe
[pitch-delta clamp against a max-per-frame cvar-ish value]
FUN_0057de60()                                 // finalize: angle-delta floats -> cmd.angles[3] shorts
```

### Confirmed `usercmd_t` struct layout (0x40 / 64 bytes total)
| offset | field | evidence |
|---|---|---|
| `+0x00` | serverTime (int) | `unaff_ESI[0] = DAT_01e06e88` in `FUN_0057de60` |
| `+0x04` | buttons (uint, bitflags) | OR'd throughout `FUN_0057dc90`/`FUN_0057d430`/`FUN_0057d7e0` |
| `+0x08`,`+0x0c`,`+0x10` | angles[PITCH,YAW,ROLL] (short, ANGLE2SHORT-packed) | `FUN_0057de60`'s 3-iteration loop over `_DAT_00b36408[0..2]` |
| `+0x14`,`+0x18` | weapon-related (2 fields) | `unaff_ESI[5]/[6]` in `FUN_0057de60`, also fed into `FUN_005df010` |
| `+0x1c` | **forwardmove** (signed byte) | `FUN_0057d430` (keyboard) and `FUN_0057d7e0` (mouse-strafe branch) both write here |
| `+0x1d` | **rightmove** (signed byte) | same, both functions |
| `+0x1e` | movement byte #3 (unidentified — up/down or lean?) | written by `FUN_0057d430` via the same `FUN_004c0830` clamp-to-byte helper |
| `+0x1f` | movement byte #4 (unidentified) | same |
| `+0x24`..`+0x34` | 5 more int fields | `unaff_ESI[9..13]` in `FUN_0057de60`, copied from `_DAT_00b363e4..f4` — purpose not yet identified |
| `+0x38` | a short field | written in `FUN_0057d7e0`'s tail (unrelated to the melee-lunge bytes below) |
| `+0x3e`,`+0x3f` | melee-lunge-direction bytes (tentative) | `FUN_0057e360`, quantized via `FUN_0057e300` |

### View-angle delta accumulator: `_DAT_00b36408` is a `float[3]` (PITCH, YAW, ROLL)
Not two separate globals as first thought — `_DAT_00b36408` is index 0 (pitch), `_DAT_00b3640c` is
index 1 (yaw) of one 3-float array, per-player-strided by `0xbe5c`. Both the keyboard-turn function
(`FUN_0057d300`) and the mouse-look function (`FUN_0057d7e0`) accumulate into this same array each
frame; `FUN_0057de60` converts it to the final packed `cmd.angles[3]` shorts at the end.

### `FUN_0057d430 @ 0057d430` — keyboard analog movement summer (the function that had been missing)
Uses `FUN_0057d250()`+`FUN_007380e0()` pairs (fractional-hold-time helpers — same pattern as classic
`CL_KeyState`, giving smoother analog-ish response from a digital key based on how much of the frame
it was held) to accumulate forward/back and left/right contributions, then clamps each to a signed
byte via `FUN_004c0830` (`ClampChar`-equivalent) before writing `+0x1c`/`+0x1d`/`+0x1e`/`+0x1f`. Also
handles some crouch/prone/stance-toggle flag bits unrelated to movement bytes.

### Kbutton state storage layout (confirmed via `ScanStructRegion.java`)
A flat array of kbutton-state byte-pairs starts around `00a98b00`-ish, stride **20 (0x14) bytes**
per kbutton, each entry being (at minimum) 2 relevant bytes: byte 0 = current held state, byte 1 =
one-shot "pressed since last read" latch (cleared after `FUN_0057dc90` reads it — classic
edge-triggered button latch pattern). `FUN_00438710 @ 00438710` is the bind-index dispatcher (`switch`
on bind index, handling one-shot/special binds like `+mlook` toggling `DAT_00a98bec`, "center view"
snapping `_DAT_00b36408[0]`, weapon-related binds, etc.) — this only covers bind indices roughly 1-77
(the *special-case* binds); the 32 simple hold-movement binds found in the name table (`+forward`
etc., indices likely 0-31) don't need special-case code since they're plain active/inactive kbuttons
consumed generically by `FUN_0057dc90`/`FUN_0057d430`.

### Hook strategy — CONFIRMED (task #5 groundwork)
- **Movement (left stick):** hook `FUN_0057d430`. Cleanest approach: let it run normally (keyboard
  still works), then in a post-hook ADD our own clamped stick-derived forwardmove/rightmove on top of
  whatever it wrote at `+0x1c`/`+0x1d` (re-clamping the sum through the same `FUN_004c0830` helper, or
  our own equivalent, so keyboard+stick can't overflow a signed byte).
- **Look (right stick):** hook `FUN_0057d680` (raw mouse-delta source, found earlier) — unchanged
  from the earlier finding, still the best site since it inherits `m_pitch`/`m_yaw`/sensitivity/accel/
  ADS-scale for free rather than reimplementing that feel.
- **Calling convention warning:** every function in this chain uses custom/register-passed arguments
  (Ghidra reports them as `unaff_ESI`/`unaff_EDI`/`unaff_EBX`/`in_EAX`, i.e. values the *caller*
  leaves in specific registers rather than passing on the stack or via a clean `__fastcall`). **Do
  not** write hooks assuming a normal C signature — these need either (a) a trampoline that preserves
  the exact register state Ghidra inferred, confirmed live via x32dbg before shipping, or (b) hooking
  one level up/down the call chain at a point with a cleaner signature. Static inference alone is not
  enough to trust blindly — next real implementation step should start with x32dbg confirmation of
  register contents at the `FUN_0057d430`/`FUN_0057d680` call sites during live play.

## Re-verification: is there dormant/disabled native Xbox 360 controller code? (2026-07-14)

User pushed back on the 2026-07-13 "no controller support at all" conclusion, reasonably
suspecting the console-ported codebase might have real but disabled controller-reading
code left in from the Xbox 360/PS3 build. Did a materially more rigorous re-check than
the original strings-only pass:

1. **Authoritative import table via `dumpbin /imports`** (not just a strings scan) on
   both `iw5mp.exe` and `iw5sp.exe` — confirmed no `dinput8.dll`, `xinput*.dll`,
   `hid.dll`, or `setupapi.dll` statically imported by either.
2. **Broad substring search for dynamic-loading evidence** — searched all strings in
   both binaries for `xinput`, `dinput`, `hid.dll`, `setupapi`, `xenon`, `x360`, `pad`
   (i.e. checking whether the game might `LoadLibraryA("xinput1_3.dll")` at runtime
   with a graceful-fallback pattern, which wouldn't show up in the static import table).
   **Zero matches** for any controller-API DLL name, in either binary. The only "pad"
   hits are false positives (crypto padding text, and "game pad" appearing in
   `cl_yawspeed`/`cl_pitchspeed`'s cvar *description* text — leftover flavor text from
   the shared codebase, not evidence of an actual gamepad reader).
3. **Traced every controller-sounding string found in the original pass** to its actual
   referencing code, in both binaries:
   - `attachedcontrollercount`, `splitscreenactivegamepadcount`,
     `getsplitscreencontrollerclientnum` — each sits inside a large (40+ entries seen),
     alphabetically-ish ordered, **flat array of plain `char*` strings** with no
     interleaved function pointers. Neighboring entries (`issplitscreenonlinepossible`,
     `splitscreenplayercount`, `anysplitscreenprofilesaresignedin`,
     `isguestsplitscreen`, `showFriendPlayercard`, `getgameinvitescount`,
     `isPayingSubscriber`, ...) make clear this is the **front-end menu-scripting
     system's table of callable UI-expression function names** — real, live
     infrastructure used by `.menu` files for Xbox-Live-style party/profile/splitscreen
     UI logic (show/hide elements based on signed-in profile count etc.), **not** a
     physical-hardware joystick reader. The paired native implementation (if reachable
     from here at all) is a session/profile bookkeeping function, not a controller
     hardware poll — and per points 1-2 above, it structurally *cannot* be reading real
     gamepad hardware since no controller API is linked into the process at all.
   - `Unable to IDirectInputJoyConfig_Acquire because...` — decompiled its one reference
     (`FUN_006d0f20` in `iw5sp.exe`, `FUN_006ca4b6` in `iw5mp.exe`): a large generic
     **HRESULT-to-error-string decoder** (long `if`/`else` chain over raw HRESULT
     values covering hundreds of unrelated COM/DirectX/DirectPlay/DirectInput error
     codes, for crash/log diagnostics). The DirectInput joystick string is one entry in
     that boilerplate table — not evidence the game ever calls the corresponding API.
   - `@PLATFORM_USECONTROLLER1` — a single data reference in both binaries, consistent
     with being an unused/orphaned localization key (never actually displayed on PC).
   - `setct-HODInput` — traced through a couple more table-indirection hops but hit
     unresolved data-only chains in both binaries; doesn't obviously connect to
     controller input either way, deprioritized (not worth more time given the
     structural finding below already settles the question).

**Verdict: confirmed, not dormant.** There is no disabled-but-functional native
Xbox-360-controller-reading code in either PC binary. What exists is (a) generic
Windows/DirectX error-string boilerplate that happens to mention DirectInput, and (b)
real but hardware-independent menu-scripting infrastructure for splitscreen/profile UI
state, which cannot poll physical controllers because — reconfirmed twice now, via
static imports and full binary string search — **zero controller-input API (XInput,
DirectInput, RawInput) is linked into either executable, statically or dynamically.**
The from-scratch approach (own XInput integration + the usercmd hooks mapped above)
remains correct; there's nothing to unlock.

## Calling convention CONFIRMED live (2026-07-14)

Static analysis alone (Ghidra's `unaff_ESI`/`unaff_EDI`/`in_EAX` guesses) wasn't
trustworthy enough to hook blindly — confirmed live instead via a passive MinHook
diagnostic pass (`proxy_d3d9/src/diag_hooks.cpp`, PUSHAD/log/POPAD around each target,
zero behavior change, logs to `proxy_d3d9_diag.log`). **Getting a live gameplay session
required a devmap launch, not just sitting at the main menu** — confirmed hooks don't
fire at all at the front-end (matches the "paused/menu flag early-return" branch found
in `FUN_0057e480`). Launch command that worked: `iw5sp.exe +set developer 1 +devmap
hamburg` (bypasses the front-end entirely, standard Quake3-derived-engine trick).

**`FUN_0057d680` (raw mouse-delta source) — confirmed roles:**
- `EAX == EBX == EBP == 0x00B363B0` in every capture — a **stable client-input-context
  pointer**, constant across calls and shared with `FUN_0057d430` too (see below). This
  is the "raw mouse state" context the function reads from (per the earlier static
  decompile's `in_EAX+4/+8/+0xc/+0x10/+0x14` double-buffer access).
- `ESI` and `EDI` are **output** pointers, NOT an input struct as first guessed from
  static analysis — `ESI` points to where the computed X delta (float) gets written,
  `EDI = ESI + 4` for the Y delta. Both point into the *caller's* stack frame (a local
  variable pair), confirmed by the `0x0014Fxxx`-range addresses seen live (typical
  stack range for this process).
- **Hook implication: call through to the original, then ADD our right-stick delta to
  `*(float*)ESI` and `*(float*)EDI`** — safe, since these are just plain output floats
  the rest of the pipeline (`FUN_0057d740`, `FUN_0057d7e0`) consumes afterward exactly
  like real mouse movement, inheriting all existing sensitivity/accel/ADS-scale logic.

**`FUN_0057d430` (keyboard analog movement summer) — confirmed roles:**
- `EAX = 0` at entry (in single-player) — this is the **per-player index** (matches the
  `param_1 * 0xbe5c` / `param_1 * 0x230` per-player-stride pattern used throughout every
  other function in this pipeline).
- `ESI == EDI`, both holding the same address — **this is the `usercmd_t*` pointer**
  the static decompile referred to as `unaff_EDI` when it wrote `forwardmove`/
  `rightmove` at `+0x1c`/`+0x1d`. Confirmed live: at hook-entry (right after
  `FUN_0057e480`'s `memset(cmd, 0, 0x40)`, before this function's own writes), all 0x40
  bytes at this address read as zero — exactly as expected.
- `EBP = 0x00B363B0` — same context pointer seen in `FUN_0057d680`, confirming it's a
  single shared "client input state" base used consistently across this whole source
  file (likely kept resident in a register across the whole translation unit rather
  than reloaded from a global each time — common compiler behavior for a hot, large
  file).
- **Hook implication: call through to the original, then ADD our clamped left-stick
  delta to `*(signed char*)(EDI + 0x1c)` (forwardmove) and `*(signed char*)(EDI + 0x1d)`
  (rightmove)**, re-clamping the sum to a signed byte so keyboard+stick can't overflow.

Both confirmed strategies are additive post-hooks (call original first, then add on
top) — keyboard and mouse keep working completely unaffected; controller input is
strictly additive. This is now ready to implement for real (task #5).

## Real analog movement + look hooks implemented (2026-07-14)

`proxy_d3d9/src/analog_input_hooks.cpp` + `controller_input.cpp` replace the throwaway
diagnostic hooks with the real thing, following the confirmed calling convention above:

- **Movement:** `Hook_0057d430` — naked stub saves `EAX`/`EDI` (player index, `usercmd_t*`)
  across the call to the original trampoline, then calls `InjectControllerMovement(cmd)`
  which reads the left stick (radial deadzone + `^1.6` response curve, see
  `controller_input.cpp`), scales full deflection to `+-127`, and adds it onto whatever
  `forwardmove`/`rightmove` the keyboard path already wrote, clamped to a signed byte.
- **Look:** `Hook_0057d680` — same pattern for the right stick, added onto the raw
  mouse-delta output floats, scaled by a `kLookUnitsPerSecond` constant and real
  delta-time (`Controller_DeltaTimeSeconds()`, since a stick reports a *position*, not
  an already-frame-scaled delta the way a real mouse does).
- **XInput:** loaded dynamically via `xinput9_1_0.dll` (present on every Vista+ install,
  never statically linked) — confirmed the base game links nothing controller-related at
  all (see the dormant-code re-verification section above), so this is entirely new.
- Smoke-tested live (`+devmap hamburg`, simulated W key + mouse move via `SendInput`-style
  Win32 calls): game runs the full hook chain without crashing, with no controller
  connected (graceful "not connected" path exercised, not just the connected path).

**Correction (2026-07-14):** the visual playtest below (ViGEmBus virtual pad connected)
showed a "Controller Connected — Xbox 360 controller" toast top-right. Initially
misattributed this to MW3's own HUD. **User correctly caught this — that notification
is Steam's client overlay** (fires for any XInput connect/disconnect event in any Steam
game, regardless of whether that game has its own controller support), not evidence of
a native in-game controller-detection feature. Retracted — doesn't change task #6 scope
or the earlier "no native controller support" conclusion.

**Verified live with a real physical controller (2026-07-14).** Initial testing used a
ViGEmBus virtual pad (screenshot-driven, compass-HUD-based direction inference) — that
methodology turned out unreliable (misread the compass rotation direction once) and was
superseded by the user directly playtesting with real hardware, which is authoritative.
Final confirmed sign conventions:
- **Movement: no inversion needed on either axis** (`ly`/`lx` used as-is) — the very
  first implementation was already correct.
- **Look X (yaw): no inversion.** **Look Y (pitch): inverted** (negated) relative to
  raw XInput Y.

**Architecture change (2026-07-14): look decoupled from the mouse pipeline entirely.**
User correctly pointed out that hooking `FUN_0057d680` (raw mouse-delta source) meant
controller look was still conceptually "mouse emulation" under the hood — it inherited
`sensitivity`/`m_yaw`/`m_pitch`/`cl_mouseAccel`/`m_filter`, none of which make sense for
a controller stick (mouse acceleration and smoothing are compensating for noisy physical
mouse sensor data, not relevant to a clean digital stick reading). **Switched to
pre-hooking `FUN_0057de60`** (the finalize function that packs the accumulated pitch/
yaw deltas into `usercmd_t.angles`) and writing directly to the accumulator globals
(`_DAT_00b36408` pitch / `_DAT_00b3640c` yaw, both in degrees, confirmed via the
ANGLE2SHORT-style packing math in `FUN_0057de60`) — completely bypassing every
mouse-specific cvar. Controller look now has its own independent `kLookDegreesPerSecond`
constant with no acceleration or filtering baked in. Sign for the direct-write form was
*derived*, not re-guessed: `FUN_0057d7e0` does `yaw -= mouseX * m_yaw` and
`pitch += mouseY * m_pitch` (both cvars positive by default); substituting the
confirmed-correct old mouse-pipeline values (mouseX=+rx, mouseY=-ry) through both
formulas gives yaw change proportional to `-rx` and pitch change proportional to `-ry`,
so the direct-write hook subtracts both (`*kYawAccum -= rx * ...; *kPitchAccum -= ry * ...;`).
Movement (`FUN_0057d430`) is unaffected by this change — it was already a direct
`usercmd_t.forwardmove`/`.rightmove` write, never routed through any keyboard-emulation
layer, so it was already "true" native input.

**Playtest tooling built along the way:** `tools/vcontroller_sim/` — a small dev-only
console tool (ViGEmClient, MIT license) that plugs in a virtual Xbox 360 controller via
ViGEmBus (kernel driver, installed with explicit user go-ahead since it's a system-level
change) and reads simple stdin commands (`LX`/`LY`/`RX`/`RY`/`RESET`/`QUIT`) to drive
stick axes programmatically. Useful for automated smoke-testing that the hook chain
doesn't crash, but proved unreliable for judging *directional correctness* by screenshot
alone — real playtesting caught two sign errors this virtual-pad methodology got wrong.
**Lesson for future work: prefer real-hardware playtest for anything direction/feel-
sensitive; keep the virtual pad for crash/regression smoke-testing only.**

**Still open:** deadzone size, `kCurveExponent` (currently 1.6), and
`kLookDegreesPerSecond` (currently 250) are unTuned defaults, not confirmed-good feel —
revisit once task #6's options screen exists to make this adjustable rather than a
hardcoded constant. Buttons (fire/jump/reload/ADS/sprint/etc.) are not mapped at all yet
— next concrete step.

## Official control scheme reference (2026-07-14, from `mw3 manual.pdf` in game root)

User supplied the official retail manual (PS3 edition — file had ~32 bytes of stray
header junk before the real `%PDF-` start, stripped before reading). Its "Game
Controls" page (p.3) gives the full DualShock 3 button→action mapping. Doesn't give
raw `usercmd_t.buttons` bit values (still needs empirical confirmation, see task #10),
but gives the authoritative **action list and control scheme** to map against — this
is the same for the 360/PC-equivalent layout, just different button names. PS3→Xbox
button correspondence is the standard universal positional mapping (Triangle=Y,
Circle=B, Cross=A, Square=X, L1=LB, R1=RB, L2=LT, R2=RT, L3/R3 = stick clicks):

| Action | PS3 button (manual) | Xbox/XInput equivalent |
|---|---|---|
| Fire | R1 | **RT** (trigger) |
| ADS (aim down sights) | L1 | **LT** (trigger) |
| Throw Lethal | R2 | **RB** (bumper) |
| Throw Tactical | L2 | **LB** (bumper) |
| Change Weapon | Triangle | Y |
| Change Stance | Circle | B |
| Jump | Cross | A |
| Interact/Use | Square | X |
| Move | Left stick | Left stick |
| Sprint | L3 (stick click) | Left stick click |
| Aim/Look | Right stick | Right stick |
| Melee Attack | R3 (stick click) | Right stick click |
| Pause | Start | Start |
| Scoreboard (MP only) | Select | Back |
| Pointstreak cycle / equip | D-Pad up/down cycle, right to equip | D-Pad |

**Confirmed by user 2026-07-14 (real Xbox 360 hardware/convention knowledge):**
PS3 and Xbox 360 shipped with a genuinely different physical layout for this pair,
not just cosmetic button-name differences — PS3 defaulted fire/ADS to the bumpers
(R1/L1) and grenades to the triggers (R2/L2); Xbox 360's actual default swapped this,
matching the universal Xbox FPS convention (RT/LT triggers for fire/aim, matching
Halo/Battlefield/etc. of the same era) — so **fire is RT, ADS is LT, and grenades move
to the bumpers (RB=lethal, LB=tactical)** on the Xbox-convention mapping this mod
targets. Table above already corrected to reflect this.

## "Needs a click" issue — SOLVED (2026-07-14)

Full arc of this investigation, kept for the record since two earlier theories were
wrong and it's a good example of why live verification beats static-only guessing:

1. **First theory (wrong):** bit 0x80000 of a state field at `DAT_00b37444`, gating a
   later branch of `FUN_0057e480`. Diagnostic logging showed this bit already reads 0
   even while the bug is present — not the real gate.
2. **Second theory (wrong):** the in-engine cursor-visibility flag (`DAT_01c00474`,
   set by `FUN_005385d0` based on mouse position bounds, read by the cursor-draw call
   in `FUN_00478540`). User's own diagnosis pointed here (visually, a leftover cursor
   sprite really does linger after level load, and a click really does dismiss it) —
   but forcing this flag to 0 didn't unblock movement either. Correlated symptom, not
   the actual code-level input gate.
3. **Real fix:** raw disassembly (not just decompile, which drops constant call
   arguments the decompiler can't resolve) of `FUN_0057e480` shows the true first gate:
   `FUN_00416150(EBX, 0x10)` right at function entry, before even the keyboard-turn
   call (`FUN_0057d300`) runs. `FUN_00416150(idx, mask)` is a generic per-player
   bitmask test: `(*(uint*)(DAT_00b36210 + idx*0x188) & mask) != 0`. If bit `0x10` is
   set, the entire function short-circuits to just the always-running finalize call —
   skipping buttons, movement, and mouse/stick look entirely. This is a **third,
   distinct state block** from both earlier guesses (`DAT_00b37444` gates a different,
   less-restrictive branch further down; the cursor flag is UI-only, not a code gate
   at all). Force-clearing this bit every frame (`*(uint32_t*)0x00B36210 &= ~0x10`,
   offset 0 since SP is always player index 0) in `InjectControllerLookAngles` (which
   fires in every branch, including the one this bit gates) fixed it — **confirmed
   working by the user, real hardware, full normal menu flow, no click needed at any
   point.**

Whatever this bit actually represents semantically (not identified — plausibly
something like "no local client input source yet" or an initial-connect/spectator
flag) doesn't matter for our purposes.

**Correction (2026-07-14): forcing it clear unconditionally DID have a downside.**
Survival's buy stations wouldn't open their purchase menu until the game was fully
paused — the same bit is legitimately used by the game for interactive-menu contexts,
not just the loading-screen cursor, and permanently suppressing it broke that. Fixed by
only clearing the bit for a 3-second window starting on a rising edge of the in-level
flag (`0x00A98ACC`, rising-edge-detected the same way `tools/memdiff` detects level
load), then letting the game's normal gating resume. **Confirmed working by the user,
real hardware: both the original no-click-needed behavior AND normal buy-station menu
access now work correctly together.**

## Button mapping — approach change and real bind data (2026-07-14)

**Two wrong turns before landing on the right approach, kept for the record:**
1. Tried mapping raw `usercmd_t.buttons` bits directly (13 known bit values from
   `FUN_0057dc90`) to XInput buttons with a guessed/arbitrary test assignment, meaning
   to correlate real actions empirically afterward. Real-hardware playtest showed the
   guessed mapping was "not even close" — abandoned before completing the correlation.
2. Pivoted to synthetic keyboard/mouse simulation (`keybd_event`/`mouse_event`) driven
   by XInput state, using a user-supplied Steam Controller config as the button→key
   mapping. This would likely have worked (this game already correctly processes
   synthetic key events — proven earlier this session testing W-key movement before
   any hooks existed), but **user explicitly rejected it**: they want true native
   in-engine calls, matching the same principle already applied to movement/look, not
   OS-level input emulation. Reverted.

**Real plan, now in progress:** the user-supplied key list was actually meant to help
*identify which bind command* each action uses (not to be emulated) — cross-reference
against the game's own actual default bind file. Found it: `players2/config.cfg` (and
`players2/config_mp.cfg`, identical apart from MP-only chat binds) in the game root —
these are real Infinity-Ward-generated files ("generated by Infinity Ward, do not
modify"), the authoritative source of truth for bind→command mapping, not a guess:

| Key | Command | Controller (per user's Steam config) | Held (`+`) or one-shot? |
|---|---|---|---|
| SPACE | `+gostand` | A = Jump | held |
| CTRL | `toggleprone` | B = Crouch/Prone | **one-shot command** |
| F | `+activate` | X = Use | held |
| R | `+reload` | (not yet assigned a controller input) | held |
| 1 / 2 | `weapnext` | Y = Switch Weapon | **one-shot command** |
| G | `+frag` | LB = Special Grenade | held |
| Q | `+smoke` | RB = Equipment | held |
| SHIFT | `+breath_sprint` | Left Thumb = Sprint/Hold Breath | held |
| E | `+melee_zoom` | Right Thumb = Melee | held |
| ESCAPE | `togglemenu` | Start = Pause | **one-shot command** |
| TAB | `+scores` | Back = Player List/Scoreboard | held |
| N | `+actionslot 1` | D-Pad Up = Night Vision/Stopwatch | held |
| 3 | `+actionslot 3` | D-Pad Down = Weapon Attachment | held |
| 4 | `+actionslot 4` | D-Pad Left = Inventory/Killstreak | held |
| 5 | `+actionslot 2` (**not** slot 5 — real quirk in the default binds) | D-Pad Right = Secondary Inventory | held |
| MOUSE1 | `+attack` | RT = Fire | held |
| MOUSE2 | `+toggleads_throw` | LT = ADS | held |

**All the `+`-prefixed held-kbutton binds above are confirmed present in the same
32-entry kbutton name table** already mapped in this file (`0092a014` region) —
`+gostand`, `+activate`, `+reload`, `+frag`, `+smoke`, `+breath_sprint`,
`+melee_zoom`, `+scores`, `+actionslot 1-4`, `+toggleads_throw`, `+attack` are all
literal entries in that table. This means the native path for these is the same
table-driven kbutton mechanism real keypresses already use — need each one's specific
per-bind memory address, the same way `FUN_0057dc90`'s known bit-to-address pairs were
found, just not yet correlated to WHICH named bind owns which address.

**`toggleprone`, `weapnext`, `togglemenu` are NOT held kbuttons at all** — they don't
appear in the 32-entry table, confirming they're plain one-shot console commands
(executed once per press, not tracked as a held down/up state). Native equivalent:
call the engine's own command-execution function (a `Cbuf_AddText`/
`Cmd_ExecuteString`-equivalent) directly with the literal command string, once per
press-edge — not yet located, separate investigation from the kbutton-address work.

**Next RE steps for task #10:**
1. ~~Correlate kbutton table position/order against `FUN_0057dc90`'s disassembly order
   to resolve each named held-bind's real memory address.~~ DONE, see below (2026-07-14).
2. Find the console-command-execution function for the one-shot commands
   (`toggleprone`/`weapnext`/`togglemenu`).
3. `+reload` (R key) doesn't have a controller input assigned yet in the user's config
   — needs a spot (D-Pad is full; consider whether X should be reload instead of use,
   or find another free input).

### Kbutton table position ↔ `usercmd.buttons` bit correlation — CONFIRMED (2026-07-14)

Resolved the "second field" of each 8-byte table entry (`0092a014` region): it is **not**
a `kbutton_t*` and **not** a handler function pointer — it's a pointer to the matching
`-<bindname>` string (e.g. `+attack`'s entry is `{"+attack"*, "-attack"*}`). Both strings
have exactly one code reference each (the table itself), meaning the table is purely a
registration list consumed generically by whatever walks it — individual binds are
**not** looked up by name/string anywhere else in the binary.

The real per-bind runtime state lives in a **per-player context struct** at
`playerIndex*0x230 + 0x00A98AD8` (player 0 for SP ⇒ bare `0x00A98AD8`). Found via raw
disassembly (`DumpDisasm.java`) of `FUN_0057dc90` (buttons-summer): it computes exactly
this base (`LEA EAX,[EAX*0x230 + 0xa98ad8]`, `EAX`=player index) then tests a **10-entry
contiguous sub-array at struct offset `0x128`, stride `0x14` (20) bytes**, each entry
being a `{down:byte, pressedLatch:byte}` pair (latch cleared after read — classic
edge-triggered pattern, matches the "kbutton state storage" note above). Confirmed the
array's slot order **exactly matches the 32-entry name table's position order** (not
just plausible — directly verified against known-good empirical results, see below).

**Bit ↔ struct-offset ↔ table-index ↔ bind name (all confirmed together):**

| usercmd.buttons bit | struct offset | table idx | bind name | empirical real action |
|---|---|---|---|---|
| `0x1` | `+0x128` | 0 | `+attack` | Fire (confirmed exact match) |
| `0x2000` | `+0x13c` | 1 | `+melee` | dead/no visible effect (real melee is `+melee_zoom`, not this) |
| `0x4000` | `+0x150` | 2 | `+frag` | Frag grenade (confirmed exact match) |
| `0x8000` | `+0x164` | 3 | `+smoke` | Tactical/smoke (confirmed exact match) |
| `0x4` | `+0x178` | 4 | `+breath_sprint` (per table-order correlation) | **RETRACTED** — user confirmed live, 100% certain: bit `0x4` is really Melee, not sprint. This directly contradicts the "array slot order == name-table order" assumption at idx 4 (idx 0/2/3 matching `+attack`/`+frag`/`+smoke` was likely coincidental — those are simply the first few, most universally-needed actions, not proof of a strict 1:1 order). **The table-order-correlation technique is unreliable beyond idx ~3 and should not be trusted without empirical confirmation.** Sprint's real bit/address is still unknown; `struct+0xb0` (the derived "is sprinting" locomotion flag found in `FUN_0057d430`, see below) is a separate, still-unconfirmed lead. |
| `0x8` | `+0x18c` | 5 | `+usereload` | Use (bind name literally means combined use/reload — explains why it's context-sensitive) |
| `0x10` | `+0x1a0` | 6 | `+speed_throw` | logged as "Reload" empirically — name mismatch not yet reconciled, may be a quick/cook-throw variant whose visual tell was misread during manual testing |
| `0x20` | `+0x1b4` | 7 | `+actionslot 1` | dead in test — plausibly just contextual (night-vision goggles bind, no visible effect without NVGs equipped) |
| `0x100` | `+0x1c8` | 8 | `+actionslot 2` | logged as "Prone/holdstate" — plausible reinterpretation: this is likely opening the inventory/killstreak wheel (a hold-to-browse stance), not literal prone |
| `0x200` | `+0x1dc` | 9 | `+actionslot 3` | logged as "Crouch" — real bind is weapon-attachment select; needs re-test |
| `0x2` | `+0x204` | 11 | `+stance` | **newly found, never tested** — no XInput button was ever mapped to raw bit `0x2` in the diagnostic pass. Needs a fresh diagnostic build assigning an unused button to this bit and testing. |

(Table idx 10, `+actionslot 4`, and idx 12, `+gostand`, were not observed being tested in
`FUN_0057dc90`'s disassembly — `+gostand`/jump's real bit `0x400` and the `0xd8`/`0x100`/
`0x218`/`0x1f0`-offset bits come from a separate, non-contiguous part of the same struct,
see the raw disasm dump `iw5sp_0057dc90_disasm.txt` for the exact instructions.)

**`FUN_0057d430` (movement summer) touches the same struct independently:**
`LEA ESI,[ESI+0xa98ad8]`, same `playerIndex*0x230` scaling as `FUN_0057dc90` — confirms
this is genuinely one shared per-player struct, not coincidence. It reads struct offset
`0x204` (bit `0x2` in the table-order-correlation guess above — **name/identity not
confirmed**, only that it's an untested bit worth trying) to gate a branch, and separately
reads a flag at struct offset `0xb0` (not part of the `0x14`-stride array) that gates an
*extra* forward/right movement summation pass reusing the same `+forward`/`+back`
hold-time helpers — plausibly a client-side "is currently sprinting" locomotion flag, but
**this is now an unconfirmed lead, not a finding** (per the `0x4` retraction above, don't
trust struct-offset-to-bind-name correlation without a live test backing it).

**ADS (`+toggleads_throw`, table idx 29) — ruled out of this struct entirely.** The
uniform 10-entry array (offset `0x128`, stride `0x14`) cannot physically extend to idx 29
— at that stride it would land at struct offset `0x36C`, past the `0x230`-byte per-player
slot boundary (i.e. into the *next* player's data). Confirmed empirically too: a whole-binary
scalar scan (`FindConstantRefs.java`, ~1.17M instructions) for both the raw base `0xa98ad8`
and the fused address `0xa98e44` (`0xa98ad8+0x36C`) found no reference to the fused
address inside any of the four usercmd-pipeline functions (`FUN_0057d430`, `FUN_0057d7e0`,
`FUN_0057de60`, `FUN_0057dc90`) — the only hits on `0xa98e44` are in `FUN_0057e690`/
`FUN_0057e710`, which are UI/config-side (a 256-entry, `0xd28`-stride keybind-config-row
array — matches the already-documented `DAT_00a98e4c` UI-side detour above, not gameplay).
This is a coincidental address collision, not a real lead — ADS's held-state is **not**
part of this per-player kbutton array at all, same as `+mlook` (whose flag,
`DAT_00a98bec`, is an individually-placed single-byte global elsewhere in the same larger
per-player struct, handled by a dedicated case in the special-bind dispatcher
`FUN_00438710`, not by array indexing). `FUN_00438710`'s ~77-case switch was checked for
a similar dedicated single-flag write near `+toggleads_throw`'s or `+sprint`'s identity
and found none obviously — either it's handled by the default/generic case (same
mechanism as ordinary held binds, meaning it likely *does* have a scattered individual
address somewhere, just not yet located), or it's consumed entirely outside the input-
registration code, most plausibly inside the weapon-state machine that reads the
ADS-sensitivity multiplier already found at `unaff_EBX+0x48` in `FUN_0057d740` (that
`EBX` is a caller-supplied per-weapon/view context, not a global — tracing it means
finding `FUN_0057d740`'s caller next, not more table work).

**ADS found — CONFIRMED (2026-07-14), combining live differential testing + static
verification.** Built `tools/memdiff/` (dev-only diagnostic, not part of the mod): a
manual-mode tool that watches `GetAsyncKeyState(VK_RBUTTON)` and snapshots full process
memory (~500-700 committed regions, ~400MB cap) on every real press/release edge the user
makes in-game, then narrows to bytes that are consistently one value while ADS is held
and consistently a different value while released. Across 24 real transitions (12 full
press/release cycles), narrowed from ~11M initial diff bytes down to **10 stable
candidates**, all reading `0x00` when released:

| address | held value | notes |
|---|---|---|
| `0x00A98CB8` | `0xC9` (varies) | struct offset `+0x1E0` from the kbutton-context base `0xA98AD8` |
| `0x00A98CC8` | `0x01` | struct offset `+0x1F0`, clean boolean |
| `0x00A997B0` | `0x01` | clean boolean |
| `0x00A997B4` | `0x01` | clean boolean, +4 from the above |
| `0x0094D346` | `0x08` | |
| `0x00A86B02` | `0x08` | part of a repeating array pattern also seen in earlier automated runs |
| `0x00A86B1B` | `0x7F` | paired with the above, +0x19 offset |
| `0x01CD942C` | `0x02` | different region entirely — plausibly a HUD/reticle-visibility flag |
| `0x0094D346`, `0x003A7888` | varies | lower confidence, different regions |

**Static confirmation on the strongest candidate:** whole-binary scalar scan
(`FindConstantRefs.java`) for `0xA98CB8` found it referenced **inside
`FUN_00438710`** — the exact special-bind dispatcher already confirmed to own `+mlook`'s
dedicated flag (`DAT_00a98bec`). The specific case (disassembly around `004388d1`-
`00438928`) updates **two** kbutton pointers together in the same case —
`LEA EAX,[ESI+0xa98b8c]` then `LEA EAX,[ESI+0xa98cb8]`, each followed by
`CALL 0x0057d1c0` (down-transition handler) or `CALL 0x0057d200` (up-transition
handler) — i.e. **one physical bind driving two separate kbutton_t structs
simultaneously**. This is exactly the shape you'd expect for `+toggleads_throw`, whose
name literally means "toggle ADS **or** throw" (context-dependent on whether a grenade
is primed) — a single bind that needs to feed two different downstream systems. The
`0xA98CB8`/`0xA98CC8` pair (16 bytes apart) is consistent with one `kbutton_t` struct's
two relevant fields (a classic Quake3-style `kbutton_t` is `down[2]`, `downtime`, `msec`,
`active`, `wasPressed` — `0xA98CB8`'s varying byte value fits a hold-duration/`msec`-style
field, `0xA98CC8`'s clean 0/1 fits `active`).

**Recommended implementation path (not yet built):** rather than hand-writing raw bytes
into this struct (fragile — the exact field layout/size isn't 100% pinned down), call the
real engine functions the game itself uses: `FUN_0057d1c0(kbutton_ptr, time)` on the
ADS-press edge and `FUN_0057d200(kbutton_ptr, time)` on the release edge, using
`kbutton_ptr = playerContext + 0xCB8` (and likely also the paired `+0xB8C` kbutton, per
the dispatcher case above) — this goes through the same code path a real `+toggleads_throw`
keypress would, so hold-time/msec bookkeeping stays correct automatically instead of us
having to reverse-engineer and replicate it by hand. Matches the project's "call real
engine functions, don't synthesize state" principle better than a raw memory write would.
**Calling convention CONFIRMED via decompile (2026-07-14), after a real bug from the
first guess.** First implementation only set `EAX`/`ECX` for `FUN_0057d1c0` (KeyDown) and
was missing a third argument passed on the stack — result: ADS "activated once then got
stuck / stopped responding," confirmed live by the user. Decompiling both functions
(`DecompileFuncs.java`) revealed the real classic-Quake3 `kbutton_t` layout and exact
signatures:
```c
struct kbutton_t {
    int down[2];      // +0x00, +0x04 -- up to two key-identifiers currently holding this
    int downtime;      // +0x08
    int msec;           // +0x0c
    char active;         // +0x10
    char wasPressed;      // +0x11 (latch, cleared on read elsewhere)
};

// EAX = kbutton_t* (implicit, not a numbered param), ECX = key identifier, THIRD arg
// (time in ms) is pushed on the stack before the call, caller cleans up after --
// confirmed by "PUSH EDI ... CALL 0x57d1c0 ... ADD ESP,0x8" bracketing both calls in
// FUN_00438710's case block.
void KeyDown(int keyId /*ECX*/, int timeMs /*stack*/);

// EAX = kbutton_t*, ECX = time in ms, EDX = key identifier -- both register args, no
// stack arg (this one WAS correct in the first implementation).
void KeyUp(int timeMs /*ECX*/, int keyId /*EDX*/);
```
Fixed in `analog_input_hooks.cpp`'s `CallKbuttonDown` to push the timestamp on the stack
before the call, matching the real signature. `CallKbuttonUp` was already correct.

**Second bug, fixed same day:** down/up calls used different "key identifier" values (13
for the down-case, 14 for the up-case -- the two separate jump-table dispatch indices for
the `+`/`-toggleads_throw` command pair). `KeyUp` only clears a `down[]` slot if its
`keyId` argument matches what `KeyDown` stored there, so 13 vs 14 never matched -- `KeyUp`
silently no-op'd every time, confirmed live as "activates, can never be released." The
dispatch index and the `down[]`-slot identifier are two different concepts that just
happen to reuse the same `EBX` register value at the real call sites -- since we call
`FUN_0057d1c0`/`FUN_0057d200` directly rather than through the real command dispatcher,
both calls now use the same constant (13) so our own claim/release stay self-consistent.

**ADS is now CONFIRMED WORKING (2026-07-14)** -- true hold-to-aim on the left trigger,
clean release every cycle, verified live by the user across multiple hold/release
cycles. Fully native: drives the real engine's own `KeyDown`/`KeyUp` kbutton handlers,
no OS-level input emulation, no raw memory-state guessing.

Sprint (`+breath_sprint`) remains unresolved — the same manual live-diff methodology
that found ADS should work for it too, just watching Shift/sprint-key transitions instead
of the right mouse button.

**First attempt wired up (2026-07-14), EXPERIMENTAL/NOT YET LIVE-VERIFIED:** rather than
run the memdiff process first, tried the existing struct+0xb0 lead directly (user's
explicit call, prioritizing speed over certainty). `InjectControllerSprint()` in
`analog_input_hooks.cpp` writes a byte at `0x00A98B88` (per-player struct base
`0x00A98AD8` + `0xb0`) to 1 while left stick click (L3) is held, 0 otherwise — this is
the flag `FUN_0057d430` reads to gate its extra forward/right movement summation pass.
Bound to L3 per user request (console reference table above says L3 = Sprint by
default, so this also matches the intended control scheme, not just a free slot).
**Needs live playtest to confirm or retract** — if holding L3 while moving forward
doesn't visibly increase speed / trigger sprint animation/stamina, this flag is likely a
downstream reflection of some other state (same trap the Prone bit-forcing hit), and
Sprint should get the same live memdiff treatment ADS got instead of more guessing.

**ADS must be true hold-to-aim, not toggle (user requirement, 2026-07-14):** PC
keyboard/mouse ADS binding on this game may default to (or support) toggle-style aim,
which is not the desired feel for a controller — the trigger should only aim while
physically held, matching console behavior. If usercmd's ADS bit is interpreted as a
simple "currently held" state by the underlying game logic (classic Quake3-style
+/- semantics), just setting the bit only while the trigger is actually pressed each
frame should naturally give correct hold-to-aim behavior with no extra work. If real
testing shows it toggling instead, look for an `ads_toggle`-style cvar to force off
rather than trying to fight it with edge-detection logic on our end.

### Remaining open items (lower priority, not blocking task #5 start)
- Purpose of `usercmd_t+0x1e`/`+0x1f` (movement bytes #3/#4) and `+0x24`..`+0x34` (5 int fields) not
  yet identified — likely upmove/lean or vehicle-related, not needed for basic ground movement/look.
- Menu/UI controller navigation is a separate investigation (task #6), not covered by this pipeline
  at all — the UI reads keyboard/mouse binds through an entirely different system (see the
  `FUN_00539530`/`DAT_00a98e4c` key-binding-table detour above, which turned out to be UI-side, not
  gameplay-side).
