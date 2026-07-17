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
   (`toggleprone`/`weapnext`/`togglemenu`). **Attempted and retracted (2026-07-14):**
   `FUN_004d6da0` looked promising (called with a bind-name string from
   `FUN_0061f590`'s alias-expansion logic) but turned out to be a thin wrapper around
   `FUN_0057e770`, which is a HUD/UI keybind-display formatter (looks up "KEY_UNBOUND"
   style text for on-screen prompts), not a command-execution/dispatch function at all.
   Still not located — separate investigation needed.
3. ~~`+reload` (R key) doesn't have a controller input assigned yet.~~ **RESOLVED
   (2026-07-14), see "Reload — RESOLVED" below.**

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

**First attempt (struct+0xb0), CONFIRMED WRONG live ("SPRINT NOT WORKING", 2026-07-14).**
Decompiling `FUN_0057d430` explained why: the flag at `0x00A98B88` (struct base
`0x00A98AD8` + `0xb0`) genuinely does gate an extra forward/right movement summation
pass, but that pass reuses the real keyboard `+forward`/`+back` hold-time helpers
(`FUN_0057d250`/`FUN_007380e0`). Our movement hook writes `forwardmove`/`rightmove` as
raw bytes instead of driving real kbuttons, so those helpers always return 0 for us --
the flag was gating a summation of zero. Right mechanism, wrong layer, same class of trap
Prone and ADS both hit before being solved properly.

**Real mechanism CONFIRMED (2026-07-14), found via string-xref tracing, not memdiff:**
extracted ASCII strings from `iw5sp.exe` directly (no `strings` binary in this shell's
PATH -- used a small Python one-liner instead) and found real GSC-exposed cvars
`player_sprintSpeedScale`, `player_sprintUnlimited`, `perk_sprintMultiplier`, etc.
`FUN_00494310` turned out to be a giant `Dvar_Register*`-style cvar-registration dump
(not a GSC accessor); `player_sprintSpeedScale` is registered there with its dvar_t*
stored in global `DAT_01d397e4`. Tracing *reads* of that global (`DescribeRefs.java`)
led to the real consumer:

- `FUN_00643870(int* param_1)`: `iVar2 = *param_1; uVar7 = *(uint*)(iVar2+0xc);` --
  `if ((uVar7 & 0x4000) != 0) { local_8 = *(float*)(DAT_01d397e4 + 0xc); local_c = local_8 * local_c; }`
  -- bit `0x4000` of a flags dword at struct offset `+0xc` gates whether
  `player_sprintSpeedScale` gets multiplied into movement speed. This is a genuine
  `pm_flags`-style bit on a live playerState-like struct, **not** a heap struct we'd need
  to locate and hardcode.
- Traced up the call chain: `FUN_00643870` ← `FUN_00643ce0` ← `FUN_00644ed0`. All three
  pass the *same* pointer (`param_1`) straight through; `FUN_00644ed0(int* param_1)` is
  the real Pmove-equivalent top-level entry (a big switch on move-type: normal/water/
  ladder/etc, matching classic Quake3 `pmove_state_t` structure and flag usage
  throughout).
- Confirmed calling convention via raw disasm of `FUN_00644ed0`'s prologue: `SUB ESP,0x90`
  then `PUSH EBX` then `MOV EBX,[ESP+0x98]` -- relative to the function's *raw* entry
  (before its own prologue runs, i.e. exactly when a MinHook detour fires), that's
  `[ESP+0x98] - 0x94 = [ESP+0x04]`, a plain single stack argument. `*param_1` dereferences
  to the actual struct; `+0xc` is the flags dword; bit `0x4000` is sprint.

**Fix implemented (2026-07-14):** removed the disproven `struct+0xb0` write entirely.
`InjectControllerSprint()` now just polls L3 each frame into a `g_sprintHeld` bool. A
hook on `FUN_00644ed0` (`Hook_00644ed0`) grabs the live `param_1` straight off the stack
every call (no stored/hardcoded data address — same live-pointer-capture pattern already
used for `cmd`/ESI in the `FUN_0057de60` hook), dereferences it, and sets/clears pm_flags
bit `0x4000` on the live struct to match `g_sprintHeld`.

**First live test still failed** — bit forced correctly at `FUN_00644ed0`'s entry
(`before=0x0`, `after=0x4000` every tick, confirmed via temporary instrumentation), but
by the time `FUN_00643870` (the actual consumer, several calls deeper) read the same
struct address moments later in the same tick, it read back `0x0`. Neither
`FUN_00644ed0` nor `FUN_00643ce0`'s own decompiled logic explicitly clears bit `0x4000`
— it happens somewhere in one of the many sub-calls between them (never fully
identified). Separately confirmed via a comparison test that real keyboard Shift-sprint
keeps this exact bit set on every fresh per-tick struct copy, matching our understanding
of the mechanism — so the bit/struct was right, just not surviving the trip.

**Real fix: reassert one level deeper.** Added a second hook on `FUN_00643ce0`
(`Hook_00643ce0` / `ReassertSprintPmFlags`), which re-forces the same bit on the same
live struct right at that function's entry — past most of the intervening code — and
that's what actually survives through to `FUN_00643870`'s read. **CONFIRMED WORKING
live by the user (2026-07-14).**

**Crash during this investigation, root-caused and fixed:** the first version of
`Hook_00643ce0` used raw-entry `[ESP+8]` for the pml pointer, based on misreading the
function's own prologue (`FUN_00643ce0`'s `PUSH ESI` instruction executes *between* two
`MOV reg,[ESP+0x74]` reads, so they read different stack slots, not the same one twice).
This dereferenced the wrong argument (a local scratch-buffer address) as if it were the
struct pointer, and crashed the game (`0xc0000005` access violation, confirmed via
Windows Event Log Application Error → proxy `d3d9.dll` offset `0x4e` →
`ReassertSprintPmFlags`'s `MOV ECX,[EAX+0xc]`, mapped via a throwaway Ghidra import of
our own built DLL). The real call site (`PUSH EDX` for the local buffer, then `PUSH EBX`
for the pml pointer, then `CALL`) confirms the pml pointer is the *last*-pushed arg,
landing at raw-entry `[ESP+4]` — same slot/formula as every other hook in this file.
Fixed and re-verified working with no further crashes.

**Diagnostic logging added during this investigation (hook install status, first-call
confirmation, raw-button edge logging, per-tick before/after snapshots) has since been
removed** now that Sprint is confirmed working — see git history if it's ever needed
again for a similar investigation. The permanent hook install/enable status logging in
`InstallAnalogInputHooks()` was kept (required by CLAUDE.md's logging rules).

### Reload (`+reload`) — RESOLVED (2026-07-15)

User reported X interacts/uses fine but never reloads. Checked the real keybind config
(`players2/config.cfg`): `bind R "+reload"` and `bind F "+activate"` — confirming
`+usereload` (bit `0x8`'s old label) doesn't exist as a real bind anywhere in the
default config. That label was almost certainly another table-order-correlation guess
(same unreliable technique already retracted once for Sprint/Melee at idx 4) — bit
`0x8` empirically does trigger Use/Interact, just probably isn't literally named
`+usereload`.

**First attempt, CONFIRMED WRONG live (2026-07-14 → 2026-07-15).** Used
`FindStringRefs.java` to find `"+reload"`'s table entry at `0092a074`, then computed a
table index using an approximate base address (`0092a014`) quoted in older notes above,
getting idx 12 → struct `+0x218` → bit `0x40000` (extending the known contiguous
kbutton array pattern). **This was wrong on two counts:**
1. The base address itself was imprecise. Cross-checking against two *other*
   already-confirmed binds' real string-table addresses (`+actionslot 4` at `00929ff4`,
   `+stance` at `00929ffc`) against the true base (`+attack`'s entry, confirmed at
   `00929fa4`) gives `+reload`'s *real* table index as **26**, not 12.
2. Bit `0x40000` (whatever idx 12 actually is) was wired to X anyway and confirmed live
   to trigger a color-grading/visual-tint toggle, not reload. Reverted immediately.

Idx 26 is well past the last offset `FUN_0057dc90` (the buttons-summer) actually checks
— its full disassembly ends at idx 10's check (`+0x1f0`). So `+reload` isn't part of
the compact usercmd-bit array at all, meaning it needs the same kind of individually-
placed-kbutton treatment ADS and `+mlook` needed, not another array-index guess.

**Second attempt: the special-bind dispatcher, INCONCLUSIVE.** `FUN_00541020` (the real
`CL_KeyEvent` handler, confirmed via `CL_KeyEvent_Add/Sub/Mul` cvar-name references near
its top) contains a lookup table, `DAT_00a98e4c`, indexed by raw key code (stride 3 per
code, `param_1 * 0x34a` per-player row): if the table entry for a given key is nonzero,
that value is the dispatch index passed to `FUN_00438710` (the same special-bind
dispatcher ADS's kbuttons were found through); if zero, the key isn't specially
dispatched at all. This looked like a clean way to find `+reload`'s real dispatch index
directly from R's key code — no guessing needed.

Read this table live (one-shot diagnostic fired from inside `InjectAllControllerInput`,
since the table is populated at runtime from the user's keybind config, not static
data) for both candidate key-code guesses for 'R' (`0x72` = ASCII `'r'`, matching
memdiff's earlier Reload hit; `0x52` = `VK_R`). **Both read back 0** — meaning `+reload`
is *not* handled through the special dispatcher either, for either key-code
interpretation tried.

**Third attempt: `g_reloading` GSC flag, dead end.** Broadened the string search beyond
the literal `+reload`/`-reload` bind names and found `+usereload`/`-usereload` DO exist
as real strings in the binary (contradicting the earlier claim they don't) — table idx
5 with the corrected base, i.e. exactly the slot already mapped to bit `0x8`. So bit
`0x8` really is `+usereload`, not a mislabeled `+activate` as guessed — it's real
context-sensitive use/reload behavior that (like ADS/Sprint before it) must depend on
some condition our raw bit-forcing doesn't satisfy. Also found `g_reloading` (a
GSC-exposed "is reloading" dvar) with 3 code references — all three turned out to be
dead ends: two are cvar registration (`FUN_005c2230`, `FUN_0047d680`), and the third
(`FUN_0041fb00`, a reload-state-reset utility) is only ever called from main-menu/
matchmaking code (banned/beta-closed checks, friend-join popups) — nothing in the
actual gameplay trigger path.

**Fourth attempt, CONFIRMED WORKING (2026-07-15):** went back to memdiff with a cleaner
test protocol (stand still, don't fire, just toggle R — avoids shooting/ammo-count
churn producing spurious candidates) and added a pointer-scan feature to `memdiff`
itself (see `tools/memdiff/main.cpp`): after narrowing to final candidates, it now finds
each candidate's containing memory region and scans all other readable memory for a
4-byte value matching that region's base, surfacing any static reference. This run's
final candidates included **`0x00A98C68`** (`held=0x72` 'r' ASCII, `released=0x00`) — a
**static** address in the same per-player struct region already used for the ADS
kbuttons, not a moving heap address like the earlier pass caught. `0x00A98C78` (exactly
`+0x10` from it) also correlated (`held=0x01`/`released=0x00`), matching `kbutton_t`'s
already-confirmed `active` field offset exactly — strong confirmation this is a real
`kbutton_t`, same struct layout as ADS's, not a coincidental correlate.

**Fix:** `InjectControllerReload()` calls the real `CallKbuttonDown`/`CallKbuttonUp`
(same engine functions used for ADS) on kbutton `0x00A98C68`, edge-triggered on X,
using a distinct bind index (15) from ADS's (13). Kept alongside the existing `0x8`
(Interact) bit-OR. **CONFIRMED WORKING live by the user (2026-07-15).**

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

## One-shot command dispatch and the real ESCAPE/pause-menu path (2026-07-15)

Investigated to unblock Y (`weapnext`) and Start (pause/`togglemenu`) — both are
one-shot commands, not held kbuttons, so neither the ADS/Reload technique nor the
Sprint pm_flags technique applies directly. Full narrative and dead ends are in
`re_notes/known_issues.md` issue #2; this section records the confirmed function
signatures for future reference.

**`FUN_00457c90` = `Cbuf_AddText(int clientIndex, const char* text)`.** Found via the
classic CoD-RE technique of searching for a hardcoded `"screenshot"` command string as
an anchor (the real call site is `FUN_00457c90(*param_1, "screenshot\n")` inside
`FUN_004dfd30`, itself gated behind the `developer` cvar). Lock-protected
(`FUN_00428af0`/`FUN_00528fe0` acquire/release critical section `0x1f`) per-client
text-buffer append: special-cases a `"p0 "` prefix to redirect the target client index
(irrelevant for SP, always client 0), then appends the string (including its null
terminator) into a per-client buffer at `&DAT_017507e4/e8/ec + clientIndex*0xc`
(base/capacity/writeOffset triplet), bounds-checked against capacity (`0x4000` /
16384 bytes — exactly Quake3's classic `MAX_CMD_BUFFER`). Write-offset advances by the
string length only (not length+1), so the caller must supply its own trailing `\n`.
Plain `__cdecl`, confirmed via the real call site's disassembly (push string, push
client index, call, caller cleans 8 bytes).

**`FUN_00605f60` = `Cbuf_Execute`, `FUN_004d6960` = `Cmd_ExecuteString`.**
`FUN_00605f60` is gated by a `DAT_017507e0` "wait" counter (classic Quake3 `cmd_wait`),
splits the buffer into lines (respecting quoted strings, splitting on `;`/`\n`/`\r`),
and hands each extracted line to `FUN_004d6960(clientIndex, contextArg, lineText)`.
That function tokenizes the line and walks a linked list at `DAT_017507d8` (nodes
shaped `{next, namePtr, callbackPtr}`), doing a case-insensitive name compare
(`FUN_00463bb0`) before calling the matched callback directly (with a move-to-front
cache reorder on hit) — falls through to a cvar-set check and a "forward to server"
call if no match. **Confirmed this is real and live** (append/drain telemetry all
correct: writeOffset advances by the exact string length on append, resets to 0 the
following frame). A one-time live dump of the full `DAT_017507d8` list (132 entries)
proved `weapnext`/`togglemenu`/`screenshot` are **not** registered there — the list
skews almost entirely toward UI/profile/social/debug commands (`closemenu`,
`openmenu`, `profile_toggleAutoAim`, `coopLaunch`, etc.), essentially no core gameplay
verbs. So this whole real mechanism is confirmed working, just not how weapnext/pause
are actually triggered.

**The real ESCAPE/pause-menu path: hardcoded directly in the key-event handler,
`FUN_00541020`.** (Ghidra's decompile mis-detects this function's parameter count as 3;
the real call site in `FUN_0054b9f0` passes 4 args — `FUN_00541020(0, local_38[3],
local_28, local_38[1])` — trust the disassembly over the decompile here.) ESCAPE
(`0x1b`) is special-cased entirely separately from the generic command dispatcher:
```
gate  = *(uint32_t*)(0x00B36210 + playerIndex*0x188)   // same gate the buy-station fix uses
state = *(int32_t*)(0x00B36218 + playerIndex*0x188)    // per-player game-state
if (gate & 0x10)              -> FUN_004d9850(playerIndex, 0x1b, isDown)  // forward
                                   // ESC to the currently open menu (real "close" action)
else if (state == 1 || 2)     -> FUN_004d6620(playerIndex)                // open pause menu
else if (state == 6)          -> FUN_004396d0(playerIndex, 2)   // (after extra guard
                                   // checks, skipped in our first live implementation) --
                                   // this is the branch real SP/Survival gameplay hits
```
For SP, `playerIndex` is always 0 (all three addresses collapse to flat constants). All
three callees confirmed plain `__cdecl` via their real call sites' disassembly:
- `FUN_004d6620(playerIndex)` — 1 arg (`0x0054126e`: `PUSH ECX; CALL; ADD ESP,4`).
- `FUN_004d9850(playerIndex, keyCode, isDown)` — 3 args (`0x00541281`: pushes EDX
  (playerIndex) last, so it's the first declared arg; `ADD ESP,0xc` after).
- `FUN_004396d0(playerIndex, mode)` — 2 args (`0x00541259`: `PUSH 0x2; PUSH EAX; CALL;
  ADD ESP,8`).

**Confirmed live:** Start (wired to this exact logic) genuinely opens the pause menu
via the `state==6` branch. Closing/unpausing it is blocked on a separate, unresolved
issue — the mod's whole per-frame injection normally lives inside the gameplay-
simulation pipeline, which halts while paused (confirmed via a heartbeat diagnostic).
A real `IDirect3DDevice9::Present` hook was implemented for this (`d3d9_hook.cpp` —
hooks `IDirect3D9::CreateDevice` via its vtable slot, filtered to `DeviceType ==
D3DDEVTYPE_HAL` to avoid a documented D3D9-hooking pitfall where games create a
throwaway probe device first) but its detour doesn't fire live for reasons not yet
found, even though `MH_CreateHook`/`MH_EnableHook` both report success on what's
confirmed to be the real HAL device. See `re_notes/known_issues.md` issue #2 for the
full account, including a dead-end side investigation into whether this build has a
working developer console (it does not appear to — no `toggleconsole` string exists,
and the one promising-looking `"monkeytoy"` console-restriction dvar is never read
anywhere in the binary besides its own registration).

## Start unpause, Y/weapnext solved, Back reverted (2026-07-15, later session)

Picked back up after the previous session's docs/push. Three outcomes this session:
Start's unpause finally solved, Y/weapnext solved cleanly, and a Back-button attempt
that regressed live and was caught/reverted immediately — kept here for the record
since the root cause (an unvalidated assumption) directly informed how weapnext was
then solved correctly. Full narrative in `re_notes/known_issues.md` issues #2-3; this
section is the technical reference.

### Start unpause: the `Present` hook was dead, not just pause-specific

Added a fire counter (`g_presentFireCount`, incremented inside `Hook_Present`, logged
on change from the gameplay tick) to settle the previous session's open question. Live
result: the counter stayed at **exactly zero** through a full normal, UNPAUSED play
session with dozens of confirmed gameplay-tick frames elapsing — conclusive proof the
detour never fires at all, not just during pause. This rules out every pause-specific
theory from the prior session and points at an external hook on the same vtable slot
(Steam Overlay is the leading suspect: well documented to hook `Present` itself, active
by default for any Steam-launched title). Not worth fighting a third party's hook.

**Real fix: abandoned `Present`, subclassed the game's own `WndProc` instead.**
`d3d9_hook.cpp`'s `Hook_CreateDevice` now calls `InstallWndProcHook(hFocusWindow)` once
the real HAL device is confirmed (the same `hFocusWindow` parameter `CreateDevice`
already receives — no need to touch `D3DPRESENT_PARAMETERS` at all). This is a plain
Win32 API (`SetWindowLongPtr(hwnd, GWLP_WNDPROC, ...)`), not a COM vtable, so nothing
D3D9-related can silently steal it. A `SetTimer`-driven ~60Hz `WM_TIMER` on the
subclassed window guarantees the hook keeps ticking even during totally idle periods
with zero other window messages arriving (e.g. the mouse motionless over an idle paused
menu) — without it, `HookWndProc` would only fire as often as real messages happen to
arrive, not reliably frequent enough to catch a quick Start press/release. Runs on the
game's own thread (whichever thread owns/pumps the window), matching every other hook
in this project — deliberately not a separate free-running thread, which would call
real engine functions unsynchronized with the game's own thread and risk exactly the
kind of corruption CLAUDE.md's hook-safety rules warn against.

**The actual unpause call:** decompiling `FUN_004396d0` fully (previously only its
`mode == 2` case, "open," had been read closely) revealed a genuine `mode == 0` case:
```c
case 0:
  FUN_0053ada0(param_1, 0xffffffef);
  thunk_FUN_0057e710(param_1);
  FUN_005396b0("cl_paused", 0);   // clears cl_paused -- this IS resume/unpause
  FUN_004a1280(0);
  FUN_004ae120(&DAT_01c00458);
  return 1;
```
`InjectControllerPauseMenu` now tracks its own `g_paused` bool (set/cleared on the same
physical Start press that opened/closed the menu) and calls
`SetMenuState(kLocalClientIndex, 0)` on the second press instead of re-opening. Both the
gameplay tick and the new `WndProc`-driven `InjectMenuInputTick` call it (safe/idempotent,
debounced by the shared `g_startHeld`/`g_paused` state) — redundancy kept from the prior
session's architecture in case one hook point ever misses an edge.

**CONFIRMED WORKING LIVE across multiple full cycles** — `proxy_d3d9.log` shows clean
`Start pressed (opening): state=6` → `Start pressed (closing): calling SetMenuState(0,
unpause)` → `Start pressed (opening): state=6` transitions repeating cleanly.

### Y/weapnext: solved via the real raw-keycode dispatch table

First attempt (WRONG, see the Back section below for the shared root cause): computed
`weapnext`'s index in the 8-byte-stride bind-name string table (base `0x00929fa4`) and
fed it straight into `FUN_00438710` as a case number. Should have been a red flag on its
own — `weapnext`'s hit in that table (`0x0092a0a8`) didn't even land on a clean multiple
of 8 (offset 260, `260/8 = 32.5`), unlike every genuinely-confirmed entry (`+reload`=26,
`+actionslot4`=10, `+stance`=11, `pause`=33, all exact).

**Real fix:** live-read `FUN_00541020`'s own raw-keycode dispatch table (`DAT_00a98e4c`)
for weapnext's actual bound keys (`'1'`=0x31, `'2'`=0x32 per `players2/config.cfg`) — the
same lookup the game performs on a real keypress, so it can't be wrong by construction.
Confirmed exact formula from `FUN_00541020`'s disassembly:
```
LEA ECX,[ESI + ESI*0x2]              ; ECX = keyCode*3
MOV EAX,[EBP + ECX*0x4 + 0xa98e4c]   ; table[keyCode] -- 12 bytes/entry (3 dwords)
```
`EBP = playerIndex*0xD28` (from the function's prologue), collapsing to 0 for SP's
player 0. So for SP: `value = *(int32_t*)(0xA98E4C + keyCode*12)`. A one-shot diagnostic
read both `'1'` and `'2'` live: **both returned 66 (0x42)** — expected, since both keys
bind to the identical command.

`FUN_00438710`'s decompile shows case `0x42` (66 decimal) calling
`FUN_004a5f70(param_1, 1)`, paired with case `0x46` calling `FUN_004a5f70(param_1, 0)` —
a clean next/prev-direction pair, structurally different from ADS/Reload's down/up
kbutton pairs (this is a genuine one-shot call with no held state to track). Decompiled
`FUN_004a5f70`:
```c
void FUN_004a5f70(undefined4 param_1, undefined4 param_2)
{
  if (DAT_0096f0b8 != 0 && FUN_005791d0() && !FUN_00579240()) {
    DAT_009945c4 = DAT_00984b78;
    FUN_0049e350(param_1, 1);
    FUN_0057a670(param_1, param_2, 0, 0);
  }
}
```
`FUN_0057a670(playerIndex, direction, 0, 0)` is unambiguous once decompiled: modulo-15
weapon-inventory-slot cycling (`uVar6 = (uVar6 + 0xf + local_4) % 0xf`, where `local_4 =
(param_2 != 0)*2 - 1` gives a `+1`/`-1` step from the direction argument), ending in a
real `FUN_0042d6b0(playerIndex, weaponIndex, uVar8)` weapon-SET call. This is genuinely
`weapnext`/`weapprev`, not a guess.

`InjectControllerWeaponNext()` calls `WeaponNext(kLocalClientIndex, 1)` on Y's rising
edge only (a true one-shot, no release action, unlike every held kbutton in this file).
**CONFIRMED WORKING LIVE by the user.**

### Back regression: bind-name-table index ≠ `FUN_00438710` case number

Wired Back to `0x00A98B14` using the SAME flawed technique weapnext's first attempt
used: `"+scores"`'s bind-name-table index (31, a clean `idx*8` fit, unlike weapnext's
messy hit) fed directly into `FUN_00438710` as a case number (`0x1f` = 31). Statically
traced case `0x1f`'s disassembly (`0x438aff`: `MOV EAX,ESI; IMUL EAX,EAX,0x230; ADD
EAX,0xa98b14; CALL 0x0057d1c0`) and case `0x20`'s (`-scores`, same `0xa98b14` address,
`CALL 0x0057d200`) — a clean down/up kbutton pair, structurally identical to ADS/Reload,
which made the wrong case number look validated when it wasn't.

**CONFIRMED WRONG LIVE:** holding Back made the player walk backward. `0x00A98B14` is
almost certainly the real `+back` (move-backward) kbutton, not `+scores` — the
bind-name table and `FUN_00438710`'s switch are apparently numbered independently of
each other. The methodological error: ADS's and Reload's real case numbers were each
found by searching `FUN_00438710`'s disassembly for an address **already confirmed
independently** (via memdiff or an xref chain from a known-good lead) — never by
trusting a bind-name-table index as if it were the switch's own case numbering. That
assumption was never actually validated for ADS/Reload either; it just happened not to
come up, since their real case numbers were derived a different way from the start.

Reverted immediately (`InjectControllerBack` and its call site removed) before it could
ship. Three earlier live `memdiff` attempts on TAB itself had also failed to find a
trustworthy candidate: two runs collapsed all the way to zero candidates after
narrowing promisingly (37→1→0 and 5→5→5→5→0 patterns, both dying on the very last
transition — possibly a timing/polling issue, never conclusively diagnosed); a third
produced a stable 6-candidate cluster (`0x0304xxxx` region, held/released values fitting
a plausible `kbutton_t`-style pattern) that looked promising, but `FindGlobalRefs.java`
against all 57 of the pointer-scan's "low/static, likely dereferenceable" hit addresses
came back with **zero real code references** for every single one — confirming the
whole cluster was a heap-region coincidence, not a real kbutton, despite passing
memdiff's own address-range heuristic.

Back remains unassigned, deprioritized per explicit user call (scoreboard isn't
gameplay-defining, unlike D-pad/killstreaks). Next attempt should use the same
live-keycode-table technique that correctly solved weapnext, applied to TAB (`0x09`),
instead of another bind-name-table-index guess.

### D-pad / `+actionslot 1-4`: solved with the same live-keycode-table technique

Applied the lesson from the Back regression directly: live-read `FUN_00541020`'s real
raw-keycode table for the actual keys bound to `+actionslot 1-4` (`N`=slot1, `3`=slot3,
`4`=slot4, `5`=slot2 per `players2/config.cfg`), rather than trusting the old
table-order-guessed bit identities (already flagged unreliable in the 2026-07-14 section
above, and doubly suspect here since two of the guessed bits, `0x100`/`0x200`, are
already claimed by the confirmed-working B-button crouch/prone system).

**Gotcha caught mid-investigation:** the first live read used uppercase `'N'` (0x4E) and
got back `0` (unhandled) — but the other three keys, all digits (`'3'`/`'4'`/`'5'`, ASCII
0x33/0x34/0x35), read back clean values forming an obvious arithmetic pattern:
```
'5' (slot2) = 17 (0x11)
'3' (slot3) = 19 (0x13)
'4' (slot4) = 21 (0x15)
```
Each exactly 2 apart — screaming for a slot1 value of 15 (0x0f) to complete the pattern,
but `'N'` didn't fit. Recalled the earlier Reload finding (memdiff matched lowercase
`'r'` ASCII, not uppercase `'R'`/`VK_R`) and re-read with lowercase `'n'` (0x6E) instead:
**got back exactly 15**, completing the pattern perfectly. Letter keys use lowercase
ASCII in this dispatch table; digit keys are unambiguous either way since digits have no
case.

`FUN_00438710`'s decompile confirms a clean, uniform case pattern for all four:
```c
case 0xf:  FUN_00410ad0(param_1,0); return;   // slot1 down
case 0x10: FUN_0044ec40(param_1,0); return;   // slot1 up (note: FUN_0044ec40 only takes 1 arg)
case 0x11: FUN_00410ad0(param_1,1); return;   // slot2 down
case 0x12: FUN_0044ec40(param_1,1); return;   // slot2 up
case 0x13: FUN_00410ad0(param_1,2); return;   // slot3 down
case 0x14: FUN_0044ec40(param_1,2); return;   // slot3 up
case 0x15: FUN_00410ad0(param_1,3); return;   // slot4 down
case 0x16: FUN_0044ec40(param_1,3); return;   // slot4 up
```
Both `FUN_00410ad0(playerIndex, slotIndex)` and `FUN_0044ec40(playerIndex)` decompile as
plain, simple `__cdecl` — no special implicit-register convention needed at all, unlike
ADS/Reload's `KeyDown`/`KeyUp` (`FUN_0057d1c0`/`FUN_0057d200`).

**`FUN_00410ad0` is genuinely data-driven, not a fixed per-slot action:**
```c
void FUN_00410ad0(undefined4 param_1, int param_2 /* slotIndex */)
{
  ...
  iVar4 = *(int *)(&DAT_00985064 + param_2 * 4);  // "what type of thing is in this slot"
  if (iVar4 == 1) {
    // weapon in this slot -- either FUN_0057a670(param_1,1,0,0) (the SAME weapon-cycle
    // function weapnext uses) or a direct FUN_0042d6b0(playerIndex, weaponIndex, ...)
    // weapon-set call, depending on context
  } else if (iVar4 == 2) {
    FUN_0057a930(param_1);       // a distinct action -- likely equipment/killstreak use
  } else if (iVar4 == 3) {
    DAT_009a19ec = DAT_009a19ec | 0x40000;   // ORs a persistent flag -- likely an
                                               // NVG-style toggle
  }
}
```
This matches the user's own real-world expectation exactly: D-pad correlates to
killstreaks and attachment toggles (e.g. a grenade-launcher underbarrel) that vary by
loadout/context, not one fixed action per direction — the engine itself resolves the
actual behavior per-slot at runtime based on what's equipped. `FUN_0044ec40(playerIndex)`
(the "up" case) is nearly a no-op — it just repeats the same `FUN_00416040(playerIndex)`
guard/permission check `FUN_00410ad0` itself starts with.

Wired all four D-pad directions per the user's own reference Steam Controller mapping
(Up=slot1/`+actionslot1`, Right=slot2/`+actionslot2`, Down=slot3/`+actionslot3`,
Left=slot4/`+actionslot4`), calling `ActionSlotDown(kLocalClientIndex, slotIndex)` on
each direction's rising edge and `ActionSlotUp(kLocalClientIndex)` on release.
**CONFIRMED WORKING LIVE by the user** (at least half the directions explicitly tested;
all four share the identical confirmed mechanism, so high confidence on the untested
ones too).

### Survival ready-up (hold Y): exhaustive native search, solved via a documented exception

The feature: Survival shows "press F5 to ready up" between waves (F5 executes `"skip"`,
shortening the 30-second prep timer once everyone's ready). Wanted: hold Y ~1s, gated to
Survival only. This took the longest of any single feature this project has tackled, and
is the only one that ended in a deliberate workaround rather than a real native call.

**Attempt 1: `+gostand`, wrong system entirely.** `maps/_specialops.gsc` (Plutonium's
public GSC decompile, github.com/SkyN9ne/Plutonium-IW5-GSC) has a generic "wait for
players" utility (`_id_1814`/`_id_1817`) that does
`notifyoncommand(unique_id+"_is_ready", "+gostand")` behind `self freezecontrols(1)` +
`self disableweapons()`. Found `+gostand`'s real two-kbutton pair (`0xA98BC8`/`0xA98BA0`,
via `FUN_00438710` case `0x19`/`0x1a`, dispatch value 25 for SPACE) and called
`CallKbuttonDown`/`Up` on both. **CONFIRMED WRONG LIVE:** made the player jump mid-round
with zero ready-up effect. Re-examined the GSC source: this specific wait loop is a
different, generic pre-mission system (used by regular SP missions too), not Survival's
own between-wave mechanic at all -- `_id_1814` isn't even called from anywhere in the
whole Plutonium dump.

**Attempt 2: `+stance`, real mechanism per the dump but the specific call didn't work.**
Survival's actual per-session init script (`_unnamed/1571_0623.gsc` -- identifiable by
`level.loadout_table = "sp/survival_waves.csv"` and `"SO_SURVIVAL_READY_UP"` strings)
does `self notifyonplayercommand("survival_player_ready", "+stance")` in `_id_3F83`,
called from `_id_3F51`'s real 30-second wave-transition wait
(`common_scripts\utility::waittill_any_timeout(var_0, "survival_all_ready")`, requiring
ALL players ready via `level._id_3F85 == level.players.size`). `"+stance"` isn't
directly bound to any key in `players2/config.cfg` (only `togglecrouch`/C and
`toggleprone`/CTRL appear). `Cbuf_AddText("togglecrouch\n")` had zero effect (same dead
end class as weapnext's first attempt -- not a registered `Cmd_ExecuteString` command).
Found togglecrouch's REAL dispatch (`FUN_00438710` case `0x48`, value 72 for lowercase
`'c'`) -> `FUN_0057d2c0(playerIndex, mode=1)`, confirmed `__fastcall` via Ghidra's
decompile. Calling it directly: **zero observable effect** -- likely blocked by its own
internal guard (`(&DAT_00a98ca0)[playerIndex*0x230]==0 &&
(&DAT_00a98bc4)[playerIndex*0x230]==0` must both hold).

**User pushback, correctly skeptical of the external dump.** Searched `iw5sp.exe`'s own
GSC method-name table (a flat array of builtin-name strings around `0x0092d8a0`,
stride 4, e.g. `"isintermission"`, `"spectatingclient"`, `"keybinding"`) and found
`"coopready"` and `"isUsingIntermissionTimer"` -- exactly on-topic names. **Neither is
referenced by any script in the whole Plutonium dump** -- strong evidence that dump is
missing Survival/SP-specific compiled scripts entirely (Plutonium is primarily an MW3
*multiplayer* revival project). `coopready`'s own native dispatch couldn't be located --
only one reference to each name exists (its own table slot), meaning the GSC VM
dispatches by a numeric ID baked in at compile time, not by string comparison, and the
method table's base address isn't found as a direct LEA immediate anywhere in the binary
via a whole-program scalar-constant scan.

**Researched Plutonium's own native plugin source** (github.com/alicealys/iw5-gsc-utils,
"adds useful functions to IW5's GSC VM") and found the real GSC notify primitives:
`game::VM_Notify(int id, unsigned int stringValue, VariableValue* top)` and
`game::SL_GetString(name, 0)` (interns a C string into the VM's string table, giving the
`stringValue` `VM_Notify` needs). This is architecturally correct -- calling
`SL_GetString("survival_all_ready")` then `VM_Notify(levelEntityId, thatId, ...)` would
satisfy the exact `waittill` the wave-transition code is blocked on -- but `VM_Notify`
needs the live GSC VM's own execution stack in a consistent state
(`scr_VmPub->top`/`inparamcount`/`outparamcount`), real engineering risk to call safely
from an independent, asynchronous per-frame hook rather than from within a genuine GSC
callback context. The published addresses are for Plutonium's MP client target anyway,
not `iw5sp.exe`.

**Attempt 3: hunting F5 itself directly, since gostand/stance were reached indirectly.**
F5's Windows VK code (`0x74`) reads back `0` in the fast dispatch table -- unhandled.
Guessed Quake3-lineage function-key codes (`0x84`, `0x88`, based on a since-corrected
misremembering of Quake3's real `keys.h` constants) also read `0`. Widened to a full
scan of `0x80`-`0xA5` and found exactly two nonzero entries:
- `0x99` = 67 (case `0x43`) -> `FUN_0047da10(playerIndex)`. Decompiling it revealed a
  real pause-toggle: it calls `FUN_004396d0(playerIndex, uVar4)` with `uVar4` computed
  from the SAME gate bit (`DAT_00b36210 & 0x10`) our Start-button pause/unpause code
  already uses, toggling between mode 2 (open) and mode 0 (close). **This is the real
  PAUSE key's `"pause"` command**, confirmed by its own decompiled logic matching our
  already-verified Start mechanism exactly -- not F5 at all.
- `0x9F` = 73 (case `0x49`) -> `FUN_0057d2c0(playerIndex, mode=2)`. Assumed BY
  ELIMINATION this must be F5, since `players2/config.cfg` only binds two non-default
  keys in this range (F5 and PAUSE), and PAUSE was just confirmed to be `0x99`. **WRONG,
  confirmed live**: calling it put the player in a genuine, stuck PRONE state (had to
  hold Y again -- toggling the same case a second time -- to stand back up), with zero
  ready-up effect. The elimination logic doesn't hold: some other, unidentified default
  or hardcoded key (not present in `config.cfg`'s overridable bind list at all) must
  occupy the `0x9F` slot instead. F5's real dispatch value remains genuinely unknown
  after this whole search.

**Resolution: an explicit, user-approved, narrowly-scoped exception.** With every
native avenue exhausted, the user explicitly authorized ONE deliberate departure from
this project's "no OS-level input emulation" rule, specifically and only for this
button: synthesize a real F5 keydown/keyup via `PostMessage` at the game's own window.
Implementation:
- `d3d9_hook.cpp` already captures the game's real `HWND` (`g_gameHwnd`, set inside
  `InstallWndProcHook`) -- exposed a new `extern "C" HWND GetGameWindow()` getter for
  `analog_input_hooks.cpp` to call.
- `IsInSurvivalMode()`: `FUN_00498ec0("mapname")` -- confirmed via disassembly to be a
  plain, single-stack-argument `Dvar_GetString`-equivalent (`PUSH EDI; MOV EDI,[ESP+8];
  CALL 0x0062abe0`, no special register convention needed despite Ghidra's decompile
  showing it as taking no formal parameters) -- checked against the `"so_survival_"`
  prefix, the same 12-character check `FUN_00526b30` itself performs natively.
- `SendSyntheticF5()`: `PostMessageA(hwnd, WM_KEYDOWN, VK_F5, ...)` then
  `PostMessageA(hwnd, WM_KEYUP, VK_F5, ...)`.
- Fires once per Y-hold crossing a 1-second threshold (debounced, same pattern as every
  other hold-timer in this file), gated behind `IsInSurvivalMode()`.

Justified as safe even without a precise "is the ready-up wait specifically active"
context check (which was never found): IW5 has no DirectInput import at all (confirmed
in `CLAUDE.md`'s own original findings), meaning keyboard input is genuine
`WM_KEYDOWN`/`WM_KEYUP` messages -- a synthetic F5 outside the one moment it matters is
simply ignored by the game, the same as a real, misplaced F5 press would be.
**CONFIRMED WORKING LIVE by the user** ("works pretty flawlessly"). This was the sole
exception to real-engine-calls-only input in this mod until a second, narrower one was
added for D-pad Left's squadmate call-in (see `known_issues.md` issue #14) -- every
OTHER button, including every core movement/look/combat action, still drives the
engine's actual internal state directly. To be replaced with a real native call if one
is ever found -- see `re_notes/known_issues.md` issue #5 for the complete trail.

---

## Sprint stamina/cooldown (2026-07-15, later session)

**The gap.** Sprint (L3) forces the real `pm_flags` bit (`0x4000`) every Pmove tick via
`InjectControllerSprintPmFlags`/`ReassertSprintPmFlags`. This gets real movement-speed
scaling for free (see `FUN_00643870` below) but bypasses whatever native
duration/recovery timer normally limits sprint entirely, giving infinite sprint --
unlike real vanilla keyboard play, which has a limited sprint with a recovery window.

**Confirming this is a real gap, not intended.** `player_sprintUnlimited` is a real
dvar (`FUN_004914d0("player_sprintUnlimited",0,0xc4,"Whether players can sprint forever
or not")`, default `0`). Cross-referencing Plutonium's public GSC dump shows it's
live-set to `1` only inside specific Campaign mission scripts (`dubai_code.gsc`,
`intro_code.gsc` confirmed so far, likely a few others), not universally -- meaning
Survival and most Campaign missions genuinely run with a limited-by-default stamina
system on real hardware that our forcing hook was silently bypassing everywhere.

**Traced the real speed consumer, not the timer.** `FUN_00643870` is the confirmed real
consumer of `player_sprintSpeedScale` -- decompiled fully. It is pure speed
calculation: reads the `pm_flags` sprint bit, multiplies base movement speed by the
scale dvar when set. No duration counter, no recovery timer, nothing time-based at all.
The actual native clock that limits real sprint duration and gates its recovery lives
elsewhere in the Pmove chain (candidates considered: `FUN_00644ed0`, `FUN_00643ce0`)
but was not located this session -- deprioritized once the user redirected to
implementing stamina in the mod's own layer instead of continuing the native hunt.

**Real dvars found alongside `player_sprintUnlimited` (from `FUN_0053b960`'s
registration, same discovery pass):** `perk_sprintMultiplier` ("Multiplier for
player_sprinttime") and `perk_sprintRecoveryMultiplierActual`/`Visual` ("Percent of
sprint recovery time to use"). `"player_sprinttime"` itself is not a native dvar
anywhere in the binary -- only referenced in that description string -- so it's likely
a GSC-side script constant (not found in the Plutonium dump either). These three real
dvars are almost certainly how the Extreme Conditioning perk (community-documented to
double sprint duration to 8 seconds) is implemented natively -- a mechanism entirely
separate from `player_sprintUnlimited`'s simple on/off flag. Not yet investigated
further; flagged as an open override in `re_notes/known_issues.md` issue #6/#7.

**Web research for real values.** User directed researching real MW3 (2011) sprint
timing rather than continuing native RE. Web search confirmed base sprint duration is
4 seconds (Call of Duty Wiki, corroborated), but could not reliably confirm the
recovery/cooldown time (results kept conflating MW3 2011 with the 2023 remake). User
then supplied the real value directly: 2 seconds recovery.

**Implementation: our own timer layer, decoupled from the engine.** Added state to
`analog_input_hooks.cpp`'s sprint section: `g_sprintStamina` (a continuous float,
`kSprintMaxStaminaSeconds = 4.0f`), `g_sprintWinded` (bool lockout), and
`g_sprintCooldownRemaining` (`kSprintRegenSeconds = 2.0f` when set). `IsSprintActive()`
gates the existing `pm_flags`-forcing hooks: sprint is only allowed if the player is
holding the button, standing, and NOT currently winded (or unconditionally, if
`player_sprintUnlimited` is live).

**Bug #1 (live-confirmed by user): regen-flicker defeated the cooldown.** First version
cleared `g_sprintWinded` the instant continuous regen ticked `g_sprintStamina` back
above zero. Since regen begins adding back every frame, the stamina float crossed back
above zero (a tiny fraction) within a single frame of hitting empty -- the lockout
cleared almost instantly and sprint resumed right away, defeating the whole point of a
cooldown. User's diagnosis, verbatim: "it tries to stop it but our calls keep firing
thats why i said gate it on our end not directly in the engine we can literally block
it ourself." **Fix:** replaced the continuous-float threshold check with a real,
fixed-duration cooldown timer (`g_sprintCooldownRemaining`) fully decoupled from the
stamina float -- once winded, sprint is unconditionally blocked for the entire 2
seconds regardless of what the float is doing, and only after the timer fully elapses
does `g_sprintWinded` clear and `g_sprintStamina` reset to max. **Confirmed working
live** by the user after the fix ("trt now]" / fixed).

**Bug #2 (caught before shipping, not live-observed): shared per-frame timer
collision.** `Controller_DeltaTimeSeconds()` (used by `InjectControllerLookAngles()`
for stick-to-look-delta scaling) turned out to use a single **process-wide shared**
static timer internally, despite its own doc comment previously claiming "for this call
site." Adding sprint as a second caller of the same function in the same per-frame tick
would have starved whichever call runs second to a near-zero delta every frame (each
call resets the shared clock; the next call that frame reads almost no elapsed time).
Avoided entirely by giving sprint its own independent `GetTickCount()`-based timer
(`g_sprintLastTickMs`) instead of reusing the shared helper. Corrected the misleading
doc comment in `controller_input.h` to accurately describe the shared-timer behavior
for future callers.

**Bug #3 (caught before shipping): stale timer baseline during the unlimited-sprint
bypass.** Original code early-returned out of `InjectControllerSprint()` before
updating `g_sprintLastTickMs` whenever `player_sprintUnlimited` was live -- if the dvar
later toggled back off mid-session (e.g. leaving the mission area), the next real tick
would compute `dt` across the entire bypassed interval (potentially minutes) instead of
one frame, corrupting the stamina/cooldown math with a huge bogus jump. Fixed by moving
the `GetTickCount()`/`dt` computation and the `g_sprintLastTickMs` update to occur
BEFORE the `player_sprintUnlimited` early-return check, so the baseline stays fresh even
while bypassed.

**`player_sprintUnlimited` read via a new raw dvar-value getter, not the existing
string getter.** `FUN_00498ec0` (`GetDvarString`, used elsewhere for `mapname`) blindly
returns `*(char**)(dvarPtr+0xc)` interpreted as a string pointer -- only valid for
actual string-type dvars. `player_sprintUnlimited` is a boolean/int dvar, so `+0xc`
holds a raw `0`/`1`, not a valid pointer; reusing `GetDvarString` on it would crash
dereferencing that value as a string. Added a separate `GetDvarInt(const char* name)`
helper that calls the same underlying `Dvar_FindVar`-equivalent, `FUN_0062abe0` (name
passed in the `EDI` register, the same custom convention `FUN_00498ec0` itself uses
internally -- confirmed via disassembling `FUN_00498ec0`), then reads the raw int at
`dvarPtr+0xc` directly instead of treating it as a pointer.

**Status: production-ready, confirmed live**, except for the still-open Extreme
Conditioning override (see `re_notes/known_issues.md` issue #6/#7) -- not a workaround
pending a native fix like ready-up is, since the native timer genuinely couldn't be
located and this is the intentional design going forward.

---

## Real native sprint timer -- candidate found, but our own hook masks it from observation (2026-07-16)

Went looking for the native sprint duration/cooldown clock again (last search gave up
after `FUN_00643870`, the `player_sprintSpeedScale` consumer, turned out to be pure
speed-math with no timer logic at all). This time started from the HUD instead of the
movement code: string search for `sprint`/`stamina` found the real sprint-meter dvars
(`cg_sprintMeterFullColor`/`EmptyColor`/`DisabledColor`, `hud_fade_sprint`, all
registered in `FUN_004b5a90`) and, via `DescribeRefs.java` on those dvar caches, the
real sprint-meter RENDER functions: `FUN_005696d0` and `FUN_005695a0`.

Both compute the meter's fill fraction as:
```
fVar4 = (float)FUN_004b9350(&DAT_00984b88, DAT_00984b78) / (float)FUN_007380e0();
```
— exactly the shape of a genuine current/max sprint-time ratio, which is precisely
what a real stamina-fraction calculation should look like.

**`FUN_004b9350(playerStructAddr, currentTimeMs)`** confirmed via disassembly to be a
plain `__cdecl` (both args pushed on the stack, no custom register convention) --
safe to call directly ourselves. Internally reads several time-tracking fields off the
player struct (`+0x1d0`/`+0x1d4`/`+0x1d8`/`+0x1dc`) and, on one branch, calls
`FUN_007380e0()` itself.

**`FUN_007380e0()`** takes NO arguments and reads an ambient value already sitting in
the x87 FPU register (`ST0`) at the moment it's called -- i.e. it depends on its
*caller* having set that register up first. Deliberately NOT calling this one directly
ourselves (same class of risk already investigated and ruled out the hard way for the
ADS-FOV bug, see issue #8 in `known_issues.md`) -- `FUN_004b9350` already calls it
internally, in the correct context, on the branch where it's needed, so we get its
effect safely by only calling `FUN_004b9350`.

Added rate-limited diagnostic logging (`[sprint-diag]`, every 250ms while sprinting)
of `FUN_004b9350`'s live return value alongside our own tracked `g_sprintStamina`/
`g_sprintWinded`. **Live result: `realValue` stayed flat at exactly `4000` across an
entire full deplete/winded/recover cycle** -- never moved, regardless of
`g_sprintHeld`/`g_sprintWinded`/elapsed time.

**Why, per the user's own read of this (correcting an initial "dead end" read of
the same data): this isn't the real function failing or returning nonsense -- it's
our OWN sprint implementation preventing its normal branch from ever running.**
`FUN_004b9350` early-exits its whole time-based calculation (returning a flat
baseline instead) whenever a flag bit (`0x4000` at `param_1+0x4b0`) is set --
and this mod forces the real sprint `pm_flags` bit (also `0x4000`, same value,
likely the same underlying mechanism/mirror) unconditionally every tick sprint is
active, via a direct Pmove-entry hook rather than the game's own normal input path.
That's the same bypass this project's own sprint implementation is already
documented as doing (see the original sprint stamina writeup above) -- "sprint ==
sprint" (movement speed is correctly boosted) "but no settings" (the real
governing timer/duration system, which presumably only runs when sprint is
engaged through its own normal trigger path, never actually gets to evaluate,
because we short-circuit straight to the pm_flags bit instead of going through
whatever real mechanism would normally set it).

**Implication:** this diagnostic can't observe the real timer AS LONG AS our own
pm_flags-forcing hook is what's driving sprint. Confirming the real values would
need either (a) temporarily disabling our own forcing and reading this same
function while sprinting via real KEYBOARD input instead, to see the genuine
values without our own bypass in the way, or (b) finding the REAL native
sprint-engage trigger (a kbutton_t or command, the same class of mechanism
already found for ADS/Reload/crouch-prone) and switching to driving THAT instead
of forcing pm_flags directly -- which would also make our own sprint naturally
subject to the real timer, perk multipliers, and Extreme Conditioning without
needing to replicate any of it ourselves. Not attempted yet -- a real
architecture change, not a quick tweak, and current sprint behavior is already
confirmed working well, so this needs a deliberate decision before touching it.

---

## Real native aim-assist infrastructure found (2026-07-16)

String search for `aim_assist`/`AimAssist` (much more direct than searching for
generic "aim" terms) found `"AimAssist_GetTagPos: Cannot find tag [%s] on entity
%i.\n"` -- a genuine internal function name in an error string, confirming a real,
dedicated native aim-assist subsystem exists (this project's confirmed end-goal per
`CLAUDE.md`, previously not started).

**Full call chain now traced end-to-end, entirely via static analysis (Ghidra
decompile + raw disassembly), 2026-07-16 -- no live debugging was needed or used:**

```
FUN_0057e480 (per-frame CL_CreateCmd-equivalent orchestrator)
  ESI = in_EAX (the usercmd_t* being built, memset 0x40 bytes) -- held live,
        unclobbered, through the whole function
  EBX = client index; EBP = &DAT_00b363b0 + EBX*0xbe5c (per-client struct,
        0xbe5c bytes -- kPitchAccum/kYawAccum live at +0x58/+0x5c of this struct,
        i.e. DAT_00b36408/0c ARE EBP+0x58/0x5c for client 0)
  -> FUN_0057d430   (keyboard/stick movement summer -- existing hook target)
  -> FUN_0057d7e0   (mouse/look pipeline -- existing hook target; confirmed by
                      disasm this function ONLY ever touches ESI+0x1c/0x1d/
                      0x20/0x21/0x38/0x3a, i.e. strictly the usercmd_t fields --
                      it never reaches beyond the 0x40-byte struct itself)
       builds a small LOCAL stack struct (own weapon index hardcoded to 0,
       current DAT_00b36408/0c, DAT_00aa41a4 scale) and an output pointer,
    -> FUN_004a07a0 (copies raw yaw/pitch baseline from the local struct,
                      looks up `weaponCurveIndex = ctx[0x2c] * 0xe60`, gate-checks
                      DAT_0094d340[index], then)
       -> FUN_0055bac0 (param_1=weapon-context ptr from FUN_004a07a0, param_2=
                         output pitch/yaw delta ptr; EDI = the caller's param_1,
                         reloadable from the stack whenever needed -- NOT an
                         implicit/lost register, contrary to earlier assumption)
          ESI (local to FUN_0055bac0) = &DAT_0094d290 + weaponCurveIndex*0xe60
               -- the per-weapon AIM-ASSIST STATE entry (curve config AND live
               per-frame state mixed in the same 0xe60-byte record: locked
               target id @+0xe48, candidate array @+0x134 (stride 0x34, count
               @+0xe34), cached view origin @+0xd0/0xd4/0xd8, output correction
               @+0xe50/0xe58)
          -> FUN_0055b8b0 (finds/returns a candidate target entity pointer)
          -> FUN_0055b990 (validates it)
          -> FUN_004f4bc0 (lock-time byte + off-target-distance magnitude)
          state machine (curve mode 0xd/0xe/0xf) once locked:
          -> FUN_0055b9d0 (param_1 = the SAME ESI weapon-state entry, param_2 =
                            the found target entity)
             EAX = *(param_1+0xe48)  -- locked target id, sentinel 0x7ff=none
             if id already cached in the candidate array (@+0x134, stride 0x34):
                 read cached tag position straight from the candidate slot
                 (+0x14/0x18/0x1c), skip the entity/tag lookup below entirely
             else (cold path):
                 ESI (reassigned, LOCAL to this function) =
                     0x9ac010 + id * 0x194        <-- REAL ENTITY ARRAY,
                                                       base 0x9ac010, stride 0x194
                 EDI = *(WORD*)0x015c608c          -- a global word, likely the
                                                       local client/entity index
                 -> FUN_0055b7d0(&local_tag_pos)  -- "AimAssist_GetTagPos"; reads
                     a tag ID off *this* entity pointer at ESI+0x150 (this is
                     the "unaff_ESI" Ghidra couldn't name -- it is simply the
                     entity pointer computed one instruction above, held live
                     across the call, not a separate/mysterious context)
             delta = tagPos - viewOrigin(param_1+0xd0/0xd4/0xd8)
             FUN_004f4ee0(delta) -> angles, stored at param_1+0xe50/+0xe58
          back in FUN_0055bac0: two FUN_00420950 spring/lerp interpolations,
          FUN_0043e6a0 clamp, then ADDS the result onto *param_2/param_2[1]
          (the real aim-assist pitch/yaw CORRECTION for this frame)
    back in FUN_0057d7e0: writes the corrected local struct's two floats back
    to DAT_00b36408/DAT_00b3640c == kPitchAccum/kYawAccum
```

**Open question from the previous session is now CLOSED, by static analysis
alone, per the user's explicit "static got us most this way so" call to abandon
the `regbreak` live-breakpoint approach (parked indefinitely -- see
`known_issues.md` issue #12 for the crash that tool caused and why it was
abandoned rather than hardened):** `unaff_ESI` in `FUN_0055b7d0` is not a
usercmd_t, not a per-client struct, and not any mysterious implicit context --
it is a plain entity pointer computed two instructions earlier in
`FUN_0055b9d0` as `0x9ac010 + lockedTargetId * 0x194`, and simply left live
(never respilled/reloaded) across the intervening call. `FUN_0057d7e0`'s own
`unaff_ESI` (a separate, unrelated usage in a different function) is
confirmed-by-disassembly to always be the usercmd_t pointer and nothing else --
it never dereferences past +0x40.

**New, independently-useful finding for the eventual Survival debug menu (task
#20):** the real live entity array in *our own vanilla* `iw5sp.exe` sits at
`0x9ac010`, stride `0x194` bytes per entity. This exact stride (0x194) matches
the cragson/mw3-surviv0r reference repo's `centity` struct size -- strong
structural cross-validation from a completely independent code path, even
though that repo's base address doesn't apply here (different, AlterWare-
patched binary, see the dead-end note above). This address still needs its own
signature-scan (per `CLAUDE.md` -- no hardcoded addresses in shipped code) before
any debug-menu code reads it, but it's a real, validated starting point rather
than a guess.

**Real external data-driven curve system also found:** `FUN_004cb280` loads multiple
`"aim_assist/view_input_N.graph"` files at startup into the same fixed-size table
(`DAT_0094d290`, `0xe60` bytes per entry, per weapon). These are almost certainly real
response curves (stick-input vs. assist-strength, possibly per weapon-type/difficulty).
**Not present in any plain-zip `.iwd` archive** (confirmed via the full `.iwd` file
listing already compiled this session) -- like weapon defs, these are compiled into
`.ff` fastfiles. Getting the actual curve data needs a working IW5 fastfile unpacker --
same blocker already flagged in `re_notes/ui_assets.md` for controller-icon assets,
not yet resolved.

**CORRECTED CONCLUSION (2026-07-16, user correction after the read path above was
already fully traced): this chain is NOT a dormant player-facing aim-assist feature to
invoke.** The user pointed out the actual, well-known fact that MW3 PC has no mouse
aim-assist at all -- this system is real, but it's the shared math BOTS use to compute
their own aim toward a target (the player), reusing the SAME generic per-entity
view-angle-update plumbing (`FUN_0057d7e0`) that also processes the human player's real
mouse input each frame. That's *why* it was reachable from what looked like "the mouse
pipeline" -- `FUN_0057d7e0` is a shared, per-controlled-entity function, not
exclusively player-specific, and the aim-assist/target-lock portion of it is simply
inert/gated off for real player input (the whole reason "mouse has no aim assist" is
true in practice) while still firing for AI-controlled entities that share the same
code path. Calling `FUN_004a07a0`/`FUN_0055bac0` ourselves for the PLAYER'S aim would
mean invoking bot-aiming logic in a context it was never meant for -- wrong direction,
not just architecturally risky.

**Also worth recording, separately still useful:** hunting for what populates the
per-weapon-state candidate array (`+0x134`, count `+0xe34`) that `FUN_0055b8b0` scans
came up empty -- none of the 11 functions that reference the weapon-state table base
(`0x94d290`, found via a whole-binary `FindConstantRefs` scan) populate it, meaning the
real population function receives the weapon-state pointer as an argument rather than
recomputing it from the literal base, and lives in a not-yet-mapped per-frame
entity/AI-perception system. Not worth continued digging given the corrected
conclusion above -- this was bot-aiming infrastructure anyway.

**Revised plan, per the user's own direction ("we could honestly make our own version
we dont need native aim assist"):** build aim assist entirely ourselves, using:
- The entity array (`0x9ac010`, stride `0x194`) directly -- position confirmed at
  `+0x10` (independently, via a second float-typed alias into the same array,
  `DAT_009ac020`, discovered in `FUN_0055c650`), and a per-entity state/type byte at
  `+0xcc` (values `1`/`0xd`/`0xf` observed meaningfully differentiated in code already
  traced) as the likely primary filter for "valid living AI enemy" -- Survival has no
  neutral AI to exclude besides a co-op partner, so this may be sufficient without a
  separate team field.
- Pure, non-targeting-specific native math helpers, still safe to reuse: `FUN_004f4ee0`
  (direction vector -> pitch/yaw/roll angles) and possibly `FUN_0055b7d0`/
  `FUN_00421b20` (entity+tag-handle -> world tag/bone position, a generic animation
  query, not bot-AI-specific) for a more accurate aim point than raw entity origin.
- Our own target scoring (nearest to crosshair within a configurable FOV cone) and our
  own friction/magnetism curves, added directly onto `kPitchAccum`/`kYawAccum` -- the
  same globals our own controller-look injection already writes every frame, so no
  native call chain is needed for the actual correction, only for data lookup.

**`+0xcc` live investigation, 2026-07-16 -- inconclusive, entity-type-byte guessing
abandoned in favor of fastfile extraction (see below).** Passive `memdiff dump` polling
(pure `ReadProcessMemory`, no breakpoints, same safe class of tool used throughout) of
`+0xcc` across 48-64 entity slots, in three separate live sessions:
- Session 1 (game instance A): stable values `0x0F` (7 slots) and `0x0D` (1 slot) held
  constant across 8 snapshots over ~20 seconds.
- Session 2 (game instance B, restarted): all-zero across 30 snapshots over ~90 seconds
  -- turned out the player wasn't actually in a wave yet at the time.
- Session 3 (same instance B, confirmed live enemies + confirmed several kills during
  the window): `0x05` dominated (42 of 64 slots) and stayed **completely static** --
  identical slot indices, zero churn -- across 40 snapshots over ~2 minutes, despite
  confirmed kills happening. `0x0F` was entirely absent this time.

Slot `[0]` read `0x01` consistently in every session (array resolves correctly, not an
addressing/ASLR issue), but the type-byte value that dominates evidently differs
between game sessions/wave states in a way that doesn't cleanly correlate with "is
this an enemy" via blind value-watching alone -- the large, static, zero-churn block of
`0x05` entities looks more like static level geometry/props than actively-dying AI.
**Concluded this blind byte-guessing approach isn't converging fast enough** -- pivoted
to getting real asset data instead (see next section), which settles curve data
questions directly rather than needing to guess field semantics from raw memory.

## Real asset access unlocked: OpenAssetTools fastfile extraction (2026-07-16)

The `.ff` fastfile unpacker blocker (flagged repeatedly this project for aim-assist
curve data and controller-glyph assets) is now resolved. **OpenAssetTools**
(github.com/Laupetin/OpenAssetTools, actively maintained, explicit IW5/MW3 support)
provides an `Unlinker.exe` that loads a real retail `.ff` zone and dumps its assets to
disk in readable form. Downloaded the prebuilt Windows release (`v0.31.0`,
`oat-windows.zip`) to `D:\Tools\OpenAssetTools\extracted\` (external tool, same
convention as Ghidra/x64dbg -- not part of this repo). Output is dumped to
`D:\Tools\OpenAssetTools\zone_dump\` (also outside the repo; `.gitignore` additionally
covers `zone_dump/`/`ff_dump/`/`*.gdt` as a defensive safety net in case any dump ever
lands inside the repo -- extracted game assets are Activision's copyrighted data,
never ours to redistribute).

Usage: `Unlinker.exe -o <outputDir> <pathToZoneFf>`, run from the extracted release
folder (auto-detects the game install directory and loads IWDs/search paths from the
`.ff`'s own location). Confirmed working against real retail zones:
`zone/english/common.ff` (core shared assets -- physpresets, xanims, weapons/, sound/,
etc.) and `zone/english/code_post_gfx.ff` (leaderboards, and critically the
`aim_assist/` folder).

**Real aim-assist curve data recovered** (the exact `aim_assist/view_input_N.graph`
files `FUN_004cb280` loads at startup, finally readable): plain-text `GRAPH_FLOAT_FILE`
format, each a list of `(X Y)` control points from `(0,0)` to `(1,1)`:
- `view_input_0.graph`: 15 points, gentlest curve (Y=0.173 at X=0.515).
- `view_input_1.graph`: 13 points, moderate.
- `view_input_2.graph`: 12 points, steeper.
- `view_input_3.graph`: 7 points, steepest (Y=0.351 at X=0.512 -- over 2x view_input_0
  at a similar X).

All four are classic ease-in analog-stick response shapes (fine control near center,
progressively faster response toward full deflection), just at four different
steepness levels -- almost certainly difficulty-tier or curve-preset variants of the
same base "view input" (stick response) curve, not separate friction/magnetism-
specific curves (no `friction`/`magnet`-named assets found anywhere in either zone).
These are genuine console-authentic response-curve shapes we can now replicate
directly in our own stick-response math, instead of guessing at a formula.

**GSC decompilation pipeline completed same session.** The "1518 errors" on
`common_survival.ff` turned out to be entirely shader-technique dump failures
(missing `.hlsl` data), unrelated to scripts -- all 184 real `.gscbin` files in that
zone dumped cleanly (just numerically named, since the tool couldn't resolve their
real path names). **xensik/gsc-tool** (github.com/xensik/gsc-tool, prebuilt Windows
release `1.4.10`, downloaded to `D:\Tools\gsc-tool\extracted\`) decompiles these
directly: `gsc-tool.exe -m decomp -g iw5 -s pc <file.gscbin>` -> readable (if
hash-named, e.g. `_id_43FF`) GSC source in `decompiled/iw5/`. Bulk-decompiled all 184
successfully. Function/variable names are opaque hashes for anything not in a
recognized standard-library namespace (`common_scripts\utility::`, `maps\_audio::`,
etc. resolve fine; this project's own custom `maps\_specialops`/survival functions
don't) -- but control flow, string literals, and call structure are all genuine and
readable.

**Real findings from decompiled Survival scripts (`common_survival.ff`, script
`1571.gsc`, function `_id_3F83`):**
- **Ready-up's real trigger, independently confirmed**: `self notifyonplayercommand(
  "survival_player_ready", "+stance" )`. This upgrades an earlier lead (previously
  sourced from a third-party Plutonium GSC dump with an open doubt about Survival
  coverage) to a directly-confirmed fact from our own retail zone. Still a dead end
  for direct exploitation, though: `+stance` has no default PC keybind at all
  (confirmed absent from `players2/config.cfg`) -- genuine console-only leftover (real
  console MW3 readies up via holding B). See `known_issues.md` issue #5 for the full
  updated trail, including why the existing F5-synthesis workaround is being kept
  as-is rather than chasing F5's exact native dispatch further.
- **Turret cancel/un-toggle, independently confirmed** (script `1553.gsc`):
  `self notifyonplayercommand( "controller_sentry_cancel", "+actionslot 4" )` and
  `"controller_sentry_cancel", "weapnext"` -- matches our own live-confirmed fix for
  task #13/issue #14 exactly (D-pad Left's key-synthesis approach naturally covers
  this real cancel path too, which is why turret un-toggle started working for free).
  The killstreak-crate table in that same script (`sentry`/`remote_missile`/
  `precision_airstrike`/`stealth_airstrike`/`carepackage_c4`/`carepackage_ammo`) has no
  separate "squadmate"/"reinforcement" entry, so that item is likely a different buy
  category entirely -- not investigated further, since it already works via the same
  D-pad Left fix.
- No exact `"skip"` string anywhere in any decompiled script -- confirms `+stance` is
  the only GSC-level ready mechanism, and F5/`skip` is a separate, native-only PC path
  (see `known_issues.md` issue #5).

**Entity team/health fields still not found.** Grepped all 184 decompiled scripts and
the raw entity struct fields identified so far (`+0xcc` type byte, `+0x150` tag
handle) don't have an obvious GSC-side cross-reference in what's dumped here (this
zone is mostly wave/economy/HUD logic, not the actual AI-perception/entity-classification
code, which likely lives natively rather than in GSC anyway). Aim assist's target-
validity question remains open per the status note below.

**First implementation landed, 2026-07-17 (`analog_input_hooks.cpp`, right before
`InjectControllerLookAngles`), not yet live-tested.** Rather than keep chasing
`+0xcc`'s exact semantics via live memory polling (two sessions of that were
inconclusive -- see above), settled on the `0x0d`/`0x0f` values already confirmed by
static analysis to route through the native tag/bone-lookup path (i.e., animated
skeletal actors, not props) as the target-validity filter, on the reasoning that
Survival has no neutral AI to exclude besides a co-op partner (who'd show up as type
`1` like the local player, not `0xd`/`0xf`). Full pipeline:

- `FindBestAimAssistTarget()`: entity index `0` = local player position (+ an
  approximate stance-based eye-height offset -- real per-stance constants weren't
  independently found, so this uses reasonable Quake/IW-derived approximations),
  current view angle read straight from `kPitchAccum`/`kYawAccum` (confirmed to be the
  real, always-current view angle in degrees, not just a per-frame delta -- see that
  global's own declaration comment). Scans entities 1-63, filters to type `0xd`/`0xf`,
  computes yaw/pitch error via `atan2`, tracks the smallest-angular-error candidate
  within `[AimAssist] Range`/`ConeDegrees`.
- `GetAimAssistFrictionScale()`: multiplies the look-rate scale (alongside the
  existing ADS slowdown) using the real `view_input_0.graph` curve shape as a falloff,
  0 = no target near crosshair, up to `FrictionStrength` at dead-on-target.
- Magnetism: a capped (`MagnetismDegreesPerSecond`) pull of `kPitchAccum`/`kYawAccum`
  toward the target's exact angle each frame, applied independently of stick input
  (so it holds a target even while the stick is centered, like real console assist).

**Known unknowns going into live testing** (flagged in code comments, not hidden):
the yaw/pitch `atan2` sign convention was never independently confirmed against real
hardware (if targets pull the wrong direction, the fix is flipping that math, not the
overall approach); eye-height-by-stance is approximated, not RE'd; and the curve's
application here (distance-to-target falloff) is a repurposing of a curve that's only
confirmed to be a stick-response shape in the original game, not proven to be the
"correct" application for this exact purpose. All standard territory for a first pass
in this project -- expect a tuning round same as every other feature.

---

## Sprint's real kbutton -- PARKED, exhaustive search came back negative (2026-07-16)

Following the corrected sprint-timer finding above (our own pm_flags-forcing masks
the real timer from observation), went looking for the real sprint-engage trigger
instead -- the same class of fix already used for ADS/Reload (drive the real
`kbutton_t` KeyDown/KeyUp, not force a flag ourselves). **Conclusion after three
independent approaches: not found. Parked. Controller sprint keeps the existing
`pm_flags`-forcing implementation; see the k+m caveat at the end of this section.**

**Real bind confirmed:** `players2/config.cfg`: `bind SHIFT "+breath_sprint"` --
NOT `+sprint` (that name only showed up as a bind-ALIAS-EXPANSION target inside
`FUN_0061f590`, mapping composite input names to real sub-commands, unrelated to
sprint's actual trigger). `"+breath_sprint"`/`"-breath_sprint"` sit in a data table
(`0x00929fc4`/`0x00929fc8`, 4 bytes apart) in the exact same layout as ADS's
confirmed-real `"+toggleads_throw"`/`"-toggleads_throw"` pair -- strong static
evidence sprint has a genuine dedicated kbutton somewhere. Static analysis of
`FUN_00438710`'s ~77-case special-bind dispatcher (the same dispatcher that owns
ADS's and `+mlook`'s dedicated flags) was checked for a similar dedicated write near
sprint's identity and **found none obviously** -- first hint this might not be a
classic kbutton at all, or is handled somewhere this dispatcher doesn't cover.

**Approach 1 -- whole-process heap correlation (live memdiff, first session).**
Real keyboard Shift, 18 press/release transitions, narrowed to 31 stable
candidates, split into two behavioral groups: Group A (`0x000AC202`-`0x000AC23A`,
`held=0x00`/`released=`varying, backwards from a real "active" flag, looked like
coincidentally-timed counter/timestamp noise) and Group B (`0x02F88782`,
`0x02F88783`, `0x02F88788`, `0x02F8879A`, `0x04A7F0F0`, `0x04A7F0F1`,
`held=nonzero`/`released=0x00`, the exact pattern ADS's/Reload's real kbutton
"active" bytes show). The tool's automatic pointer scan found **no stable pointer
path** to any of the 31 candidates at the time (32MB per-region / 400MB total scan
caps were silently excluding the heap arena these addresses live in).

**Cap-widening + x64 rebuild.** Raised `ShouldScan`'s per-region cap 32MB->256MB and
`TakeSnapshot`'s total cap 400MB->1.5GB (`tools/memdiff/main.cpp`). This crashed the
tool immediately on the next run (deterministically, right after the first
snapshot) -- `memdiff.exe` was still a 32-bit build, and the widened caps pushed it
against its own ~2GB address-space ceiling. Since `memdiff` is a standalone
diagnostic tool, not the injected proxy DLL, it doesn't need to match the game's
32-bit-ness at all (`ReadProcessMemory` works fine cross-bitness) -- rebuilt it as
x64 (`build_memdiff.bat` now calls `vcvarsall.bat x64`), which fixed the crash.

**Approach 1, re-run with the x64 build.** Fresh session (game relaunched, new PID,
so all heap addresses shifted from the first run -- expected). 9 final candidates
this time, and the pointer scan actually resolved (unlike the first run): two
clusters --
- `0x02F08782`/`0x02F08783`/`0x02F08788`/`0x02F0879A` (region base `0x02F00000`,
  same relative offsets and value pattern as the first session's Group B, just a
  different absolute base -- consistent, reproducible shape, not random noise)
- `0x048E45F8`/`0x048E45F9` (region base `0x04810000`)
- Plus `0x0B8E0F8B` (`held=0xFF`/`released=0x00`) and `0x0B91410C`
  (`held=0x40`/`released=0x80`)

Every "low, likely-static" backreference the pointer scan found (e.g. `0x0095F858`,
`0x00D6EBA4`) **shifted by inconsistent amounts (0x40-0x60) between the two
sessions** -- a genuine compile-time `.data`/`.bss` global would be byte-identical
across launches (no ASLR on this binary otherwise). That inconsistency means these
"low" addresses are themselves early heap allocations that merely *look* static,
not real global pointers -- undermining the pointer-scan step's own "low = probably
dereferenceable" heuristic for this codebase. All 9 final candidate bytes also sit
above `0x02000000`, i.e. nowhere near the confirmed-real static addresses ADS/Reload
actually live at.

**Live behavioral write-test (`memdiff.exe poke`, new tool feature this session).**
Added a `poke <addr> <value> [holdMs] [startDelayMs]` mode: writes one byte to a
live address, holds it, restores the original -- used to force each candidate to
its observed "held" value while the real key was up, watching for the matching real
effect (a visible sprint speed increase). All 4 plausible candidates tested
negative (`0x02F08782`, `0x048E45F8`, `0x0B8E0F8B`, `0x0B91410C`) -- **no visible
effect from any of them.** `0x0B91410C` additionally revealed itself as pure
noise mid-test: its "original" byte was already sitting at `0x40` (its supposed
"held" value) despite the real key being up the whole time, proving it doesn't
track real key state at all. Skipped `0x02BCA761` (`held=0x65`/`released=0x6E` --
ASCII `'e'`/`'n'`, clearly text data, not a boolean flag).

**Approach 2 -- targeted static-range scan (`memdiff.exe rangewatch`, new tool
mode).** Runs the same held/released narrowing loop as the default mode, but reads
a single fixed address range directly via `ReadProcessMemory` -- no heap-region
enumeration, no caps, no pointer-scan false positives possible, since every byte in
range is already known to be genuine per-player static state. Scanned
`0xA98A00`-`0xA99200` (2048 bytes), the neighborhood containing every confirmed-real
kbutton found so far: the per-player struct base (`0xA98AD8`), `+back`'s real
kbutton (`0xA98B14`), sprint's already-ruled-out `pm_flags`-adjacent flag
(`0xA98B88`), ADS's paired kbutton (`0xA98B8C`), the `+gostand` pair
(`0xA98BA0`/`0xA98BC8`), Reload's kbutton (`0xA98C68`/active at `0xA98C78`), and
ADS's primary kbutton (`0xA98CB8`/active at `0xA98CC8`). Result: **9 initial
candidates collapsed to 0 by the very next transition, and stayed at 0 through all
24 transitions.** A clean, decisive negative -- not noise (noise wouldn't vanish
instantly and stay at zero) -- meaning sprint's kbutton, if it's a classic
`kbutton_t` at all, is definitively **not** in the same neighborhood as every other
confirmed kbutton in this struct.

**Overall conclusion:** three independent techniques (whole-heap correlation twice,
live write-testing the strongest candidates, and a targeted scan of the one region
we know for certain holds real kbutton state) all came back negative or
unconfirmable. Combined with the static dispatcher search already turning up
nothing, sprint's real trigger mechanism remains genuinely unlocated. **Parked** --
not a small remaining step, a real dead end for now.

**Next idea, not yet attempted:** a live x32dbg write-breakpoint on the real
`pm_flags` field (`ps+0xc`, bit `0x4000`) while holding real keyboard Shift with our
own controller-forcing inactive, to catch whatever code writes the bit in the act
and trace backward from a known-real write site, instead of continuing to guess
forward from bind names/heap correlation. Needs interactive, human-paced live
debugging (not a background scan), so it's a different order of effort than
everything tried above -- deliberately not started without discussing it first.

**Practical consequence, decided 2026-07-16:** since sprint (and by extension any
future keyboard-side investigation touching this same class of per-tick engine
hook) has already produced one real, live-shipped regression this session (see
`known_issues.md` #10 -- our own pm_flags-forcing hooks broke vanilla keyboard
sprint entirely until fixed with bit-ownership tracking), keyboard/mouse should be
treated as a **secondary, best-effort input path for the time being**, not a
first-class target on equal footing with controller. The mod's core purpose is
native controller support; k+m compatibility is a "must not break" constraint, not
a feature under active development. See the new caveat in `known_issues.md` and
`README.md`.

## Aim assist entity classification -- `game_entity`-equivalent array found and
partially confirmed, cross-link disproven, PARKED (2026-07-17)

Continuation of task #16 after the movement-based target filter (implemented
2026-07-17, see the code comments in `analog_input_hooks.cpp`) was live-tested and
the user rejected patching it further ("no genuinely this is bad way to pick up
what is and what isnt an ai entity") -- real classification data was needed instead
of a movement heuristic.

**Reference-repo cross-check, per explicit user direction** ("so why not check that
alterware mod we downloaded for how aimbot was done by them"). Read
`references/mw3-surviv0r/mw3-surviv0r/ft_aimbot.cpp` and `game_structs.hpp` in full
(gitignored, targets a different AlterWare-patched binary -- struct *shapes* still
useful as conceptual reference, not addresses). Their aimbot's actual validity check
(`m_iType != 13 || m_iHealth <= 0`, skip if either) reads from a SEPARATE
`game_entity` struct (size `0x270`), not from `centity` (size `0x194`, matching our
own entity array at `0x9ac010` exactly, including the confirmed position field at
`+0x10`). Neither of `centity`'s three type-like fields (`m_eType1@0x64`,
`m_eType2@0x88`, `m_eType3@0xDC`) is the field their aimbot actually checks -- the
whole session's difficulty with the `+0xcc` type byte may have been reading the
wrong struct entirely, not just the wrong offset.

**Confirmed a real 0x270-stride array exists in OUR OWN vanilla binary too.**
`FindConstantRefs.java` for the literal `624` (0x270 decimal) found 380 raw
instruction matches across 130+ distinct `IMUL reg,reg,0x270` sites -- not just an
AlterWare-binary artifact. Wrote a new script, `FindStrideArrayBase.java`
(`re_notes/ghidra_scripts/`), to isolate genuine `arrayBase + index*stride` idioms
(`IMUL reg,reg,<stride>` immediately followed by `ADD (same reg),<constant>`) from
the noisier raw-constant hits. Run for stride `624`: **92 clean matches, 89 of which
resolve to the exact same base, `0x01197AD8`** (a handful of outliers at
`0x1197bc4` = `0x1197ad8 + 0xEC`, almost certainly just a different function
computing a pointer directly to one field rather than the struct base). This level
of agreement across ~40 distinct calling functions is about as strong a static
confirmation as this kind of analysis gets.

**Field-by-field static confirmation, via `DecompileFuncs.java` on ~25 of the 92
call sites:**
- `+0xd4`/`+0xd8`/`+0xdc` -- confirmed real Vec3 (`FUN_004ea2b0`, a splash-damage-
  radius function, reads per-axis position + a per-axis bounds float at
  `+0xe0`/`+0xe4`/`+0xe8`). Matches the reference's `m_vecPositionUp@0xd4` exactly.
- `+0xec`/`+0xf0`/`+0xf4` -- confirmed real Vec3 (same function, and independently in
  `FUN_004278c0`/`FUN_004956f0`). Matches the reference's
  `m_vecWritablePosition@0xec` exactly.
- `+0x150` -- an int, checked `0 < value` in `FUN_00546f00` (a nearby-threat/danger
  detection function iterating a spatial query's results) -- matches the reference's
  `m_iHealth@0x150` exactly, both in offset and in being used as a literal alive
  check.
- `+0x110` -- a pointer, null-checked for validity in `FUN_004e9ab0` (which also
  dereferences its `+0x13a0` field) and explicitly zeroed during entity teardown in
  `FUN_0043e240` (right alongside other real cleanup steps) -- a strong, independent
  "is this entity still alive/active" signal.
- `+0x0` -- a type byte, checked `== 3` in `FUN_00422b60` (a function that resolves
  an entity to "the player responsible for it" -- reads an owner-link field at
  `+0x104` when type is 3, otherwise reads a clientnum-style `ushort` directly at
  `+0x84`). Consistent with an idTech-style `ET_` enum where `ET_MISSILE == 3`
  (missiles need an owner lookup; players/actors don't). The specific numeric value
  for "AI actor" in OUR binary was NOT determined statically -- this needed a live
  read, hence the diagnostic below. `+0x84` matches the reference's
  `m_iClientNum@0x84` in position.
- A consistent 6-float block at `+0xb8/+0xbc/+0xc0/+0xc4/+0xc8/+0xcc` (written by
  `FUN_00521340`, read by `FUN_00422b60` and `FUN_004278c0` identically) -- likely a
  bounding-box or vision-cone extent, not yet needed for aim assist so not chased
  further.

**Diagnostic-only live test, cross-link disproven.** The open question after static
analysis was how to go from an entity in OUR existing `centity`-equivalent array
(the one the movement filter already walks) to the matching slot in this new
`0x01197AD8` array. Hypothesis: `centity`'s own `+0x150` field (previously
identified in an earlier session as "a handle passed to `FUN_00421b20`") is a
clientnum that indexes directly into `0x01197AD8`, matching the reference's
`centity.m_clientnum@0x150` field name/position exactly. Built a diagnostic-only
build (no gameplay/behavior change -- pure extra logging, gated behind the same
300ms rate limit as the existing `[aimassist-diag]` line) that read this field for
whichever entity the movement filter currently considers the best target, then used
it to index into `0x01197AD8` and log `type`/`health`/`+0x110` validity.

**Result: disproven live.** For the real AI-cluster indices (334, 337, 338, 356,
357, 361, 371, 389 -- the same indices already known from this session's earlier
movement-based work to be real, moving entities), the "clientnum" read back as
multi-million garbage (e.g. idx 337 -> 134152529, idx 338 -> 134152530 -- adjacent
centity indices producing adjacent huge values, i.e. some kind of monotonic
counter/address-shaped data, not a small clientnum). For the handful of reads that
happened to fall in a plausible small range, the "clientnum" value was suspiciously
always exactly equal to the centity index itself (58->58, 23->23, 371->371,
390->390, 363->363) -- coincidence, not a real cross-reference. **Conclusion:**
`centity+0x150` is NOT the clientnum link into this array (or is not read
correctly at that offset/size) -- the assumed cross-link is wrong. This does not
disprove the `0x01197AD8` array itself, which still has strong independent static
support -- only the specific mechanism assumed to connect it to `centity`.

**Decision: PARKED, not abandoned.** Per explicit user instruction, further work on
this stopped here for now rather than continuing to the next diagnostic (sampling
`0x01197AD8`'s own position field, `+0xec`, for movement directly -- sidestepping
the broken cross-link entirely by not needing one). That remains the natural next
step whenever this is picked back up. The diagnostic-only logging added to test the
disproven hypothesis was removed from `analog_input_hooks.cpp` after this session
(dead code testing a wrong theory shouldn't linger in the shipped source); this
document is the durable record instead. `FindStrideArrayBase.java` was kept and
committed -- it's a genuinely reusable general-purpose tool independent of this
specific investigation's outcome.

**Aim assist itself: disabled in the shipped config** (`[AimAssist] Enabled=0` in
`mw3ncp_config.ini`) pending this classification work, since the movement-based
filter's known oscillation issue (flip-flopping between simultaneously-valid movers)
was never fixed and shouldn't be left live-enabled against unsuspecting players.
