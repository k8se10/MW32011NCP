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

### Next steps
- Find the per-frame top-level caller that invokes `FUN_0057d7e0` and `FUN_0057d300`
  each frame (likely `CL_CreateCmd`-equivalent) — that caller is where usercmd
  construction is orchestrated and is the natural place to also inject left-stick
  movement.
- Find the keyboard-kbutton-sum function that writes forwardmove/rightmove from
  `+forward`/`+back`/`+moveleft`/`+moveright` state (separate from the mouse-strafe
  edge case in `FUN_0057d7e0`).
- Confirm calling convention on `FUN_0057d680`/`FUN_0057d740`/`FUN_0057d7e0` precisely
  (register-passed struct pointers, not normal stack args) using x64dbg live tracing
  before writing any hook — Ghidra's decompiled `unaff_ESI`/`unaff_EAX`/`unaff_EDI`
  register guesses need runtime confirmation, not just static inference.
- Map the confirmed `usercmd_t` struct layout so far: `+0` (unconfirmed, likely
  serverTime), `+4` buttons (uint), `+0x1c` forwardmove (byte), `+0x1d` rightmove
  (byte), `+0x38` a short (unconfirmed field, written in `FUN_0057d7e0`'s tail),
  `+0x3e`/`+0x3f` melee-lunge-direction bytes (tentative).
