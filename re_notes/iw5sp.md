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

### Remaining open items (lower priority, not blocking task #5 start)
- Purpose of `usercmd_t+0x1e`/`+0x1f` (movement bytes #3/#4) and `+0x24`..`+0x34` (5 int fields) not
  yet identified — likely upmove/lean or vehicle-related, not needed for basic ground movement/look.
- Menu/UI controller navigation is a separate investigation (task #6), not covered by this pipeline
  at all — the UI reads keyboard/mouse binds through an entirely different system (see the
  `FUN_00539530`/`DAT_00a98e4c` key-binding-table detour above, which turned out to be UI-side, not
  gameplay-side).
