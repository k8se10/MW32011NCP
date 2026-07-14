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
flag) doesn't matter for our purposes; forcing it clear unconditionally has shown no
observed downside so far. Revisit only if some other real gameplay state (actual
legitimate pause, a real spectator mode, etc.) turns out to depend on it later.

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
