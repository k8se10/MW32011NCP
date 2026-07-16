# Known Issues — `iw5sp.exe` (Campaign/Survival)

Tracked as tasks in the working session; this file is the standalone reference so they
don't stay buried in `iw5sp.md`'s investigation log. Update status here as each is
resolved. Last updated 2026-07-15 (later session, same day: Start unpause, Y/weapnext,
and D-pad all resolved, Back reverted after a live regression, and Survival ready-up
solved via a deliberate, narrowly-scoped keypress-synthesis exception).

---

## 1. Buy-station + pause menu completely breaks movement — RESOLVED (2026-07-15)

**Symptom:** After using a buy station (requires mouse/keyboard per the existing
menu-navigation limitation), opening the pause menu and then exiting it left the
player completely unable to move. Damage/death still processed normally (confirmed by
dying afterward) — only movement input stopped responding. Confirmed non-controller-
specific: real mouse/keyboard input also stopped registering once broken, meaning the
game itself was left thinking a menu/cursor state was still active.

**Root cause:** `InjectAllControllerInput` unconditionally cleared gate bit `0x10` at
`0x00B36210` every single frame ("SETTLED", the fallback chosen 2026-07-14 after
several other attempts). Diagnostic logging confirmed the bit itself always read
`0x00000000` throughout the broken window — so the bug wasn't "the bit ends up wrongly
set," it was the opposite: permanently forcing this bit to 0 likely interfered with the
buy station's own closing sequence, which may need the bit to legitimately become 1
briefly to detect "menu fully closing, finish cleanup." With that transition
permanently suppressed, the game's own menu-depth/state tracking got stuck desynced,
blocking all input (ours and real) until level reload.

This exact scenario had already been solved once: a **3-second rising-edge window**
fix (only force-clear the bit for 3 seconds after entering a level, then leave it
alone) was found and confirmed working for buy stations on 2026-07-14 — but a same-day,
unrelated architecture change (moving the hook to `FUN_0057de60`) led to it being
replaced with the unconditional clear, without the window fix ever being re-tested
against real buy-station use. The revert, not a new bug, was the real culprit.

**Fix:** reinstated the 3-second rising-edge window, keyed off the same in-level flag
(`0x00A98ACC`) `tools/memdiff` uses to detect level load. **Confirmed working live by
the user** across the full test matrix: no click needed at level start, ADS/cursor
normal during general gameplay, buy station opens/works, and buy station → pause →
resume no longer breaks movement.

---

## 2. No real one-shot command dispatcher for weapnext / Start unpause — BOTH RESOLVED
    (2026-07-15, later session)

**Status:** Fully resolved. Start now genuinely opens AND closes the pause menu via
controller, and Y/weapnext works live. Kept below for the full investigation trail.

**`Cbuf_AddText`/`Cmd_ExecuteString` are real and confirmed, but were the wrong
mechanism for these buttons.** Found `FUN_00457c90` (a genuine, confirmed
`Cbuf_AddText(int clientIndex, const char* text)` — lock-protected per-client
text-buffer append, base/capacity/writeOffset triplet at
`&DAT_017507e4/e8/ec + clientIndex*0xc`, plain `__cdecl`) via the classic "search for a
hardcoded `screenshot` command string" CoD-RE anchor technique (found via external
research, per explicit user direction to use it instead of continuing blind RE — the
anchor call site, `FUN_00457c90(*param_1, "screenshot\n")` in `FUN_004dfd30`, had
already been captured hours earlier without being recognized as the anchor). Also found
the real drain/dispatch pair: `FUN_00605f60` (`Cbuf_Execute` — splits the buffer into
lines, respecting quotes) and `FUN_004d6960` (`Cmd_ExecuteString` — tokenizes each line
and walks a linked list at `DAT_017507d8`, nodes shaped `{next, namePtr, callbackPtr}`,
case-insensitive name match, calls the matched callback directly).

Both mechanisms checked out structurally and were confirmed live (append/drain
telemetry all correct: `writeOffset` advances by the exact string length on append,
resets to 0 the following frame, proving something genuinely processes the buffer every
frame) — but calling `CbufAddText(0, "weapnext\n")`, `"togglemenu\n"`, and (as an
isolation control) the literal `"screenshot\n"` anchor string all had **zero observable
effect** live (no weapon switch, no menu, no screenshot file on disk). A one-time live
dump of the full `DAT_017507d8` list (132 entries, code added temporarily to
`analog_input_hooks.cpp` then removed once its job was done) proved why: **none of
those three strings are registered there at all.** The list skews almost entirely
toward UI/profile/social/debug commands (e.g. `closemenu`, `openmenu`,
`profile_toggleAutoAim`, `coopLaunch`) — essentially no core gameplay verbs.

**Start/pause-menu: SOLVED.** Traced the real key-event handler, `FUN_00541020`
(confirmed live-relevant via its disassembly — Ghidra's decompile mis-detected the
parameter count, since `FUN_0054b9f0` calls it with 4 args but Ghidra only inferred 3).
ESCAPE (`0x1b`) is hardcoded there as a special case, entirely bypassing the generic
command dispatcher:
```
gate  = *(uint32_t*)(0x00B36210 + playerIndex*0x188)   // same gate our buy-station fix uses
state = *(int32_t*)(0x00B36218 + playerIndex*0x188)    // per-player game-state
if (gate & 0x10)              -> FUN_004d9850(playerIndex, 0x1b, isDown)  // forward ESC
                                   // to the currently open menu (real "close" action)
else if (state == 1 || 2)     -> FUN_004d6620(playerIndex)                // open pause menu
else if (state == 6)          -> FUN_004396d0(playerIndex, 2)   // (after extra guard
                                   // checks we skipped for the first live test) — this is
                                   // the branch REAL SP/Survival gameplay actually hits
```
For SP, `playerIndex` is always 0, so all three addresses collapse to flat constants.
All three callees confirmed plain `__cdecl` via their real call sites' disassembly.
**Confirmed live: Start now genuinely opens the pause menu** via `FUN_004396d0`
(state==6 branch) — first attempt assumed state would be 1 or 2 based on a surface
reading of the disasm; live testing showed real gameplay reports state 6 instead, which
uses a different callee.

**Start: still can't close/unpause via controller.** Root cause found: the entire
injection hook (`InjectAllControllerInput`) lives inside `FUN_0057de60`, part of the
per-frame *gameplay simulation* pipeline — confirmed via a heartbeat diagnostic that
this hook **completely stops firing while genuinely paused** (pausing halts simulation
by design). So Start's second press could never be detected: the code path needed to
notice it doesn't run while paused. Attempted fix: implemented a real
`IDirect3DDevice9::Present` hook (`d3d9_hook.cpp` — hooks `IDirect3D9::CreateDevice` via
its vtable slot to reach the device, then hooks `Present` on it via MinHook) and moved
Start handling to a new `InjectMenuInputTick`, driven by `Present` instead of the
gameplay tick, since Present keeps firing every rendered frame regardless of pause
state. Both hooks installed successfully live (`MH_CreateHook`/`MH_EnableHook` both
returned `MH_OK`) — but Start still didn't unpause, and no `InjectMenuInputTick` log
output appeared at all after the hooks installed.

Researched the obvious theory (external web research, per user direction): many D3D9
games create a throwaway `D3DDEVTYPE_REF`/`NULLREF` probe device (different vtable/
Present implementation from the real `D3DDEVTYPE_HAL` device) before the real one,
and a naive "hook whichever CreateDevice call succeeds first" guard can lock onto the
probe's dead `Present` instead. Added a `DeviceType == D3DDEVTYPE_HAL` filter — but a
retest showed the hook was already targeting the real HAL device both times (same
`Present` address, `DeviceType=1` confirmed in the log) — so that wasn't the actual
cause here, and the detour still never fired post-install. **Regression discovered and
fixed (same day):** moving Start handling to be driven *exclusively* by
`InjectMenuInputTick` (Present) broke even the previously-working "open pause menu"
behavior — fixed by calling `InjectControllerPauseMenu()` from *both* the gameplay tick
and `InjectMenuInputTick` (safe/idempotent, debounced by a shared `g_startHeld`).
Confirmed live after that fix: Start reliably opened the pause menu again; unpause still
didn't work. This was accepted as a deferred/parked state for the remainder of that
session.

**RESOLVED properly in a later session (2026-07-15).** Added a fire-counter diagnostic
inside `Hook_Present` (`g_presentFireCount`, incremented on every real call, logged from
the gameplay tick which reliably fires). Live test: the counter stayed at **exactly
zero** through an entire normal, UNPAUSED play session with dozens of confirmed
gameplay-tick frames elapsing in between. This is the key finding — it rules out a
pause-specific timing issue entirely; the detour never fires *at all*, even during
ordinary rendering. Root cause almost certainly an external hook on the same vtable slot
(Steam Overlay is the leading suspect — it's well documented to hook `Present` itself and
runs by default for any Steam-launched title; a GPU-driver overlay is the other usual
suspect). Not worth fighting a third party's hook for this.

**Real fix: abandoned `Present` entirely, subclassed the game's own `WndProc` instead**
(`d3d9_hook.cpp`'s `InstallWndProcHook`, called from `Hook_CreateDevice` once the real
HAL device's `hFocusWindow` is known). This is a plain Win32 API
(`SetWindowLongPtr(hwnd, GWLP_WNDPROC, ...)`), not a COM vtable, so nothing D3D9-related
can silently steal it. Windows keeps pumping window messages even while the game's own
simulation is paused — proven by the fact vanilla keyboard ESC can still unpause the game
today, which only works because *some* message-pump-adjacent code path keeps running
throughout the paused state. Added a `SetTimer`-driven ~60Hz `WM_TIMER` on the subclassed
window so the hook keeps ticking even during totally idle periods with no other window
messages arriving (mouse motionless over an idle paused menu, etc.). Runs on the game's
own thread (whichever thread owns/pumps the window), same as every other hook in this
project.

Also found and used `FUN_004396d0`'s real **mode 0** case while fixing this (the same
function already used for "open," case 2) — decompiling it fully revealed a genuine
resume/unpause path: `case 0: FUN_0053ada0(...); thunk_FUN_0057e710(...);
FUN_005396b0("cl_paused", 0); ...` — clears the `cl_paused` dvar and resumes simulation.
`InjectControllerPauseMenu` now tracks its own `g_paused` bool and calls
`SetMenuState(kLocalClientIndex, 0)` on the second press instead of just re-opening the
menu. **CONFIRMED WORKING LIVE by the user across multiple full open/close cycles** —
`proxy_d3d9.log` shows clean `opening` → `closing` → `opening` transitions with `state`
correctly returning to 6 each time.

**Also investigated (2026-07-15): does the shipped build have a real developer
console, which would let us type commands directly and settle several open questions
at once (does `weapnext` work if typed manually, is `screenshot` really dev-gated,
etc.)?** No `"toggleconsole"` string exists anywhere in the binary. Backtick (`0x60`)
and tilde (`0x7e`) — the classic Quake3-family console-toggle keys — DO appear in
`FUN_00541020`, but only inside the same rapid-repeat debounce guard ESC uses, not in
any dedicated handling path; they get no special hardcoded treatment beyond that,
suggesting a console toggle (if bound at all) would go through the ordinary bind-index
dispatcher (`FUN_00438710`), same as any other key. Found a promising-looking dvar
registration: `FUN_00538c80` registers a dvar literally named `"monkeytoy"` (a
deliberately obscure internal name) with default value `1` and description
`"Restrict console access"` — but cross-referencing shows this is the **only**
reference to this dvar anywhere in the binary, both by its cached handle
(`_DAT_00a959f4`) and by the literal string `"monkeytoy"` itself. Nothing else reads
it back. Either this build's console-restriction check is vestigial/dead code, or it's
consulted through some other generic mechanism (e.g. a dvar-flag-based check) that
string/address tracing can't reveal — a dead end for now, not worth pursuing further
without a more concrete lead.

**Y/weapnext: RESOLVED (2026-07-15, later session).** Cross-referencing against the real
key-event handler confirmed this engine resolves core gameplay actions through the SAME
bind-index/kbutton mechanism already used for ADS and Reload (`FUN_00438710`'s jump
table), not through `Cbuf_AddText`/`Cmd_ExecuteString` at all.

A first attempt tried a shortcut: find `weapnext`'s index in the same 8-byte-stride
bind-name string table already used to confirm `+reload`=idx26/`+actionslot4`=idx10, and
feed that index straight into `FUN_00438710` as a case number. This is the SAME wrong
assumption that caused the Back regression below — never validated, and `weapnext`'s hit
in that table didn't even land on a clean multiple of 8 (a red flag ignored at the time).

**Real fix:** live-read `FUN_00541020`'s own raw-keycode dispatch table (`DAT_00a98e4c`)
for weapnext's REAL bound keys (`'1'`=0x31, `'2'`=0x32 per `players2/config.cfg`) — the
exact lookup the game itself performs on a real keypress. Confirmed formula from
`FUN_00541020`'s disassembly (`EBP = playerIndex*0xD28`, collapsing to 0 for SP's player
0): `value = *(int32_t*)(0xA98E4C + keyCode*12)`. Both `'1'` and `'2'` read back the
identical value **66** live (expected — both keys bind to the same command).
`FUN_00438710`'s case `0x42` (=66) calls `FUN_004a5f70(playerIndex, 1)`, paired with case
`0x46` calling `FUN_004a5f70(playerIndex, 0)` — a clean next/prev-direction pair (a
genuine one-shot call, no held state, unlike ADS/Reload's down/up kbutton pairs).
Decompiled `FUN_004a5f70` → `FUN_0057a670(playerIndex, direction, 0, 0)`: modulo-15
weapon-inventory-slot cycling stepped by `direction`, ending in a real
`FUN_0042d6b0(playerIndex, weaponIndex, ...)` weapon-SET call — unambiguously
`weapnext`/`weapprev`. **CONFIRMED WORKING LIVE by the user.**

**Dead ends ruled out along the way (kept for the record):**
- `FUN_004d6da0` → `FUN_0057e770`: HUD/UI keybind-display formatter, not a dispatcher.
- `FUN_00567a00`: stance-hint icon/text lookup for HUD display, not a dispatcher.
- `FUN_00541020`/`FUN_0057e710`/`FUN_0054b9f0`: real key-event message-pump handlers —
  turned out to be exactly the right place to look (see Start/pause-menu above), just
  not for a generic string-command lookup as first assumed.
- `FUN_00478ad0` (`"Reliable command buffer overflow"` string): looked exactly like a
  classic `Cbuf_AddText`, but is `SV_AddServerCommand`-equivalent — a server→client
  reliable-command *queue*, not the local client-side dispatcher.
- **memdiff on `togglemenu` (ESC), false lead:** edge-sequence mode narrowed to 218
  candidates all pointing to the same 2MB block, which turned out to be Steam API's own
  internal protobuf message data — a coincidental background-activity correlation, not
  game state. General risk for this methodology: background OS/Steam processes can
  produce consistent-looking but spurious correlates.

---

## 3. Back (`+scores`) regression — bind-name-table index ≠ `FUN_00438710` case number
    (2026-07-15, later session)

**Symptom (live, real hardware):** wired Back to `0x00A98B14` via `CallKbuttonDown`/
`CallKbuttonUp`, using `FUN_00438710` case `0x1f` (31 decimal). Holding Back made the
player **walk backward** instead of showing anything scoreboard-related.

**Root cause:** the case number was never independently confirmed — it was computed by
finding `"+scores"`'s entry in the same 8-byte-stride bind-name string table already used
to confirm `+reload`=idx26/`+actionslot4`=idx10/`+stance`=idx11 (base `0x00929fa4`),
landing cleanly on idx 31 with zero remainder, then ASSUMING that table index is the same
numbering `FUN_00438710`'s switch dispatches on. That assumption was never validated for
ADS/Reload either — their real case numbers were each found by searching
`FUN_00438710`'s disassembly for an address *already* confirmed independently (via
memdiff or an xref chain), never by trusting the bind-name table's index as if it were
the switch's case numbering. `0x00A98B14` is almost certainly the real `+back` (move
backward) kbutton, not `+scores` — the two tables are apparently ordered differently.

**Fix:** reverted immediately (`InjectControllerBack`/`InjectControllerBack()` call site
removed) before it could ship as a regression. Back is a no-op again. Three earlier live
`memdiff` attempts on TAB also failed (two collapsed to zero candidates after narrowing
promisingly; one produced a stable 6-candidate cluster that Ghidra's `FindGlobalRefs`
confirmed has **zero real code references** anywhere in the binary — a heap-region
coincidence, not a real kbutton). **Lesson applied immediately to weapnext** (see issue
#2 above): never trust a bind-name-table index as a `FUN_00438710` case number without
independent confirmation — live-read `FUN_00541020`'s own raw-keycode dispatch table
(`DAT_00a98e4c`) for the actual bound key instead, which is what actually resolved
weapnext correctly.

**Next step:** apply the same live-keycode-table technique to TAB (`0x09`) to get
`+scores`'s real dispatch value directly, the same way weapnext's was found. Deprioritized
per explicit user call — scoreboard is "nice to have, not gameplay-defining" compared to
D-pad/killstreaks.

---

## 4. D-pad (`+actionslot 1-4`) — RESOLVED (2026-07-15, later session)

Applied the exact lesson from issue #3: never trust a bind-name-table index as a
`FUN_00438710` case number — live-read `FUN_00541020`'s real raw-keycode dispatch table
(`DAT_00a98e4c`) for the actual keys bound to `+actionslot 1-4` (`N`=slot1, `3`=slot3,
`4`=slot4, `5`=slot2 per `players2/config.cfg`) instead.

**Gotcha caught mid-investigation:** uppercase `'N'` read back `0` (unhandled); the other
three (digit keys `'3'`/`'4'`/`'5'`) read back clean values (19/21/17) forming an obvious
arithmetic pattern 2 apart, but slot1 didn't fit until switching to **lowercase** `'n'`
(0x6E), which read back `15` — exactly completing the pattern. Letter keys use lowercase
ASCII in this table, matching the earlier Reload memdiff finding (`'r'` not `'R'`).

All four values (15/17/19/21) map to a clean, uniform `FUN_00438710` case pattern:
```
case 0xf/0x10  (slot1, 'n'): FUN_00410ad0(playerIndex,0) / FUN_0044ec40(playerIndex)
case 0x11/0x12 (slot2, '5'): FUN_00410ad0(playerIndex,1) / FUN_0044ec40(playerIndex)
case 0x13/0x14 (slot3, '3'): FUN_00410ad0(playerIndex,2) / FUN_0044ec40(playerIndex)
case 0x15/0x16 (slot4, '4'): FUN_00410ad0(playerIndex,3) / FUN_0044ec40(playerIndex)
```
Both plain, simple `__cdecl` — no special register convention needed, unlike ADS/
Reload's `KeyDown`/`KeyUp`. Decompiling `FUN_00410ad0` shows the real slot behavior is
**data-driven**: it reads `DAT_00985064[slotIndex]` (a runtime "what's assigned to this
slot" type) and either switches weapon (via the same `FUN_0057a670` weapon-cycle
function weapnext uses, or a direct `FUN_0042d6b0` weapon-set), calls `FUN_0057a930`
(a distinct action, likely equipment/killstreak use), or ORs a flag (`DAT_009a19ec |=
0x40000`, likely an NVG-style persistent toggle) — matching the user's own expectation
that D-pad maps to killstreaks/attachments that vary by loadout, not one fixed action
per direction. `FUN_0044ec40(playerIndex)` (the "up" case) is nearly a no-op.

Wired all four directions per the user's own reference Steam Controller mapping
(Up=slot1, Right=slot2, Down=slot3, Left=slot4). **CONFIRMED WORKING LIVE by the
user** (at least half the directions explicitly tested; all four share the identical
confirmed mechanism, so high confidence on the untested ones too).

---

## 5. Survival ready-up (hold Y) — SOLVED via a temporary keypress-synthesis workaround
    (2026-07-15, later session)

**The feature:** Survival shows "press F5 to ready up" between waves — F5 executes
`"skip"`, shortening the 30-second prep timer once everyone's ready. Mapped to holding
Y for ~1 second, gated to Survival maps only.

**Extensive native search, all dead ends** (see issue #2's pattern — same class of
investigation, this time for F5 specifically):
- F5's Windows VK code (`0x74`) reads back `0` in the fast raw-keycode dispatch table
  (`DAT_00a98e4c`) — unhandled.
- Guessed Quake3-style function-key codes (`0x84`, `0x88`) also read back `0`.
- A wider scan (`0x80`-`0xA5`) found exactly two nonzero entries: `0x99`=67 (case `0x43`
  → `FUN_0047da10`, **confirmed live to be the real PAUSE key**, calling the same
  `FUN_004396d0` toggle Start already uses) and `0x9F`=73 (case `0x49`, mode 2 via
  `FUN_0057d2c0`). Assumed by elimination that `0x9F` must be F5 (only two
  non-default keys are bound in `players2/config.cfg`) — **wrong**, confirmed live:
  calling it put the player in a genuine, stuck PRONE state (had to hold Y again to
  toggle back out), with zero ready-up effect. The elimination logic doesn't hold; some
  other, unidentified default/hardcoded key (not in config.cfg's overridable list at
  all) occupies that slot instead. F5's real dispatch value is still unknown.
- Researched Plutonium's public GSC decompilation (github.com/SkyN9ne/Plutonium-IW5-GSC)
  and found the real per-wave ready mechanism is a GSC `notifyonplayercommand`
  hook listening for the `"+stance"` bind command — but the user correctly flagged this
  dump may not include Survival-specific compiled scripts at all (two GSC builtins found
  directly in `iw5sp.exe`'s own method-name table, `coopready` and
  `isUsingIntermissionTimer`, are never referenced by any script in that dump).
  `togglecrouch`'s real dispatch (`FUN_00438710` case `0x48` → `FUN_0057d2c0` mode 1) is
  a real function call but produces zero observable effect, likely blocked by its own
  guard bytes (`DAT_00a98ca0`/`DAT_00a98bc4`).
- `coopready`'s native dispatch (a GSC-VM method-table entry, not a keycode) has no
  locatable code reference via a simple scalar-constant scan — the method table's base
  address isn't found as a direct LEA immediate anywhere in the binary.
- Found `VM_Notify`/`SL_GetString` (the real GSC notify primitives) via Plutonium's
  public `iw5-gsc-utils` source (github.com/alicealys/iw5-gsc-utils) — real, but
  requires live GSC-VM stack manipulation (`scr_VmPub->top`/`inparamcount`) to call
  safely, and the published addresses are for the MP binary, not SP.

**Workaround (explicit, narrowly-scoped user exception, 2026-07-15):** synthesize a real
F5 keydown/keyup via `PostMessage` at the game's own window (`GetGameWindow()`, exposed
from `d3d9_hook.cpp`'s WndProc hook), gated behind `IsInSurvivalMode()` (the
`"so_survival_"` mapname-prefix check, via `FUN_00498ec0("mapname")` — a plain
single-stack-arg `Dvar_GetString`-equivalent). This is the **sole deliberate exception**
to this project's "no OS-level input emulation" rule in the entire mod — every other
button drives the engine's real internal state directly. Justified here because: (1) the
real native call is provably unresolved after an extensive, multi-session search: (2) IW5
has no DirectInput import at all (confirmed in `CLAUDE.md`'s own findings), so keyboard
input is genuine `WM_KEYDOWN`/`WM_KEYUP` messages, making this indistinguishable from a
real keypress; (3) it's safe even without a precise "is the ready-up wait active"
context check, since a synthetic F5 outside that one moment is simply ignored by the
game, same as a real, misplaced F5 press would be. **CONFIRMED WORKING LIVE by the
user** ("works pretty flawlessly"). To be replaced with a real native call if/when one
is found — see task #8 in the working session's tracker for the full dead-end trail.

---

## 6. Sprint stamina/cooldown — base implemented, mission/perk overrides still open
    (2026-07-15, later session)

**The bug:** Sprint (L3) forces the real `pm_flags` bit (`0x4000`) every Pmove tick,
which bypasses whatever native duration/recovery timer normally limits sprint entirely
— giving infinite sprint, unlike real vanilla keyboard play. Confirmed this is a real
gap, not intended behavior: `player_sprintUnlimited` (a real dvar, default `0`) only
gets live-set to `1` by specific Campaign mission scripts (`dubai_code.gsc`/
`intro_code.gsc` confirmed so far via Plutonium's public GSC dump, likely others), not
universally — meaning Survival and most Campaign missions genuinely have a
limited-by-default stamina system in real play that our own forcing hook was bypassing.

**Investigated the real native duration/timer function.** Traced `FUN_00643870` (the
confirmed real consumer of `player_sprintSpeedScale`) fully — it's pure speed
calculation (multiplies movement speed when the `pm_flags` bit is set), with no
duration/timer logic anywhere in it. The real native clock that naturally clears the
sprint bit after N seconds (and gates recovery) lives elsewhere in the Pmove chain and
wasn't located.

**Implemented as our own timer layer instead** (not the game's), using real MW3 values
supplied directly by the user: 4 seconds of continuous sprint to fully deplete, then a
**fixed 2-second cooldown** before it can resume. First version had a live-confirmed
bug: clearing the "winded" lockout the instant the continuous stamina float ticked back
above zero let sprint resume almost immediately after depleting (regen starts adding
back every frame, so the float crossed back above zero within a single frame of hitting
empty) — user caught this live ("it tries to stop it but our calls keep firing").
**Fixed** with a real, fixed-duration cooldown timer fully decoupled from the
continuous stamina float, so hitting empty unconditionally blocks sprint for the whole
2 seconds regardless of anything else. **CONFIRMED WORKING LIVE by the user** after the
fix.

**Real dvars found along the way (from `FUN_0053b960`'s registration dump):**
`perk_sprintMultiplier` (scales `player_sprinttime`, itself not a native dvar — likely
a GSC-side script constant, not found in the Plutonium dump either) and
`perk_sprintRecoveryMultiplierActual`/`Visual` (scale sprint recovery time). These
imply the Extreme Conditioning perk (community-documented to double sprint duration to
8 seconds) works via `perk_sprintMultiplier`, a genuinely separate mechanism from
`player_sprintUnlimited`'s on/off flag.

**`player_sprintUnlimited` override implemented and confirmed correct in design:**
added `GetDvarInt()`, a raw dvar-value getter — calls the same `Dvar_FindVar`-equivalent
`FUN_00498ec0` itself calls internally (`FUN_0062abe0`, name arg passed in `EDI`, a
custom register convention, not on the stack), then reads the raw int directly from
`dvarPtr+0xc`. Deliberately NOT reusing `GetDvarString`/`FUN_00498ec0` for this — that
function blindly returns `*(char**)(dvarPtr+0xc)` as a string pointer, which is only
valid for actual string-type dvars; calling it on a boolean/int dvar like
`player_sprintUnlimited` would read the raw `0`/`1` there as if it were a memory
address and crash dereferencing it as a string. When live-nonzero, our stamina timer is
bypassed entirely (genuinely unlimited sprint, matching real keyboard play in those
missions) — the tick baseline is still kept fresh during the bypass so the timer
doesn't see a huge bogus `dt` jump if the dvar is ever toggled back off later in the
same session.

**Still open:** Extreme Conditioning's `perk_sprintMultiplier` override — not yet
investigated how to detect whether the perk is actually equipped/active, or how to read
its live scale value to adjust `kSprintMaxStaminaSeconds` accordingly.

**Separately found and fixed while investigating:** `Controller_DeltaTimeSeconds()`
(used for look) turned out to use a single **process-wide shared** static timer, not
one per call site despite its own doc comment claiming otherwise — a second caller in
the same per-frame tick would starve whichever call runs second to a near-zero delta
every frame (confirmed via reasoning during this investigation, before it could cause a
live bug). Sprint's stamina timer uses its own independent `GetTickCount()`-based clock
instead; the header comment was corrected to warn against this for future callers.

---

## 8. ADS look-slowdown — root cause found via diagnostic logging, fixed (2026-07-16)

**Status:** Root cause conclusively identified and fixed via live diagnostic data.
Pending one more live retest across a range of `AdsSlowdownStrength` values to
confirm feel, then this can be considered resolved.

**Original implementation:** read `FUN_004b0580(playerIndex)`, confirmed via
decompile+disassembly to be the game's own live "effective FOV this frame"
function, each frame while ADS is held, and scaled our own independent look-rate
by the ratio of that value to the `cg_fov` baseline. Weapon-agnostic by
construction — reads whatever zoom the engine already computed, no per-weapon
classification needed. **Confirmed working correctly on live playtest** for
ordinary scopes before the bug below was found.

**Bug (live-confirmed 2026-07-16):** using an ACOG's 2x toggle mode inverted
look left/right AND made slowdown far too strong at the same time. A follow-up
fix clamped the computed ratio to a sane real-world zoom range (`[0.1, 1.5]`) —
mathematically, this clamp cannot produce a negative scale factor, so it should
have made inversion *impossible* regardless of root cause. **Live retest showed
it got WORSE, not better**, which ruled out "bad ratio value" and (wrongly, see
below) pointed suspicion at something deeper than a value-level fix.

**Dead-end theory (recorded for the trail, since it drove a real investigation
pass): FPU/alt-path corruption.** `FUN_004b0580` has (at least) two internal
paths depending on a per-weapon "alt scope toggle" flag (`DAT_00984b9c` bit 2) —
most weapons take a lerp path (blends `cg_fov`/`cg_fov1` toward a real target
FOV), while weapons with a hybrid/alt-toggle reticle instead call `FUN_004f6b70`
directly (a product of several weapon-struct fields and trig-like calls, heavier
float10/x87 arithmetic). Theorized this alternate path could leave the x87 FPU
register stack at a different depth than our simple double-return calling
convention expects, corrupting other floating-point math that frame. **Ruled
out by live diagnostic data**, see below.

**Actual root cause, found via rate-limited diagnostic logging
(`[ads-fov-diag]`, added directly to `GetAdsLookRateScale()`: `baseFov`,
`effectiveFov`, `ratio`, `scale`, and the raw `DAT_00984b9c` flag byte, every
~250ms while ADS held):** a live ACOG-2x repro showed `altFlags=0x00` on
**every single sample** — `FUN_004b0580` never took the alt-toggle path at all
during the whole test; it stayed on the safe, ordinary lerp path throughout,
with completely legitimate FOV values (`baseFov=65`, `effectiveFov` cycling
through `65`/`40`/`20` as zoom changed). The alt-path/FPU theory was
categorically wrong. The actual cause: the config's `AdsSlowdownStrength` was
set to `2.0` for this test (deliberately, to "test the max threshold"), and the
OLD linear blend formula (`1 - strength*(1-ratio)`) goes negative for any
`strength > 1.0` once `ratio` drops below `(1 - 1/strength)`. At
`ratio=0.3077` (a completely real ACOG zoom level): `1 - 2*(1-0.3077) =
-0.3846` — a genuine negative scale factor from entirely normal inputs, no
engine bug involved anywhere.

**Fix:** rejected simply clamping `AdsSlowdownStrength` to a max of `1.0`
(would remove real customization — someone may legitimately want a
stronger-than-proportional slowdown on deep zooms). Instead switched the
formula's shape: `scale = ratio^strength` (a power curve) instead of a linear
blend. `strength=0` → no slowdown; `strength=1` → exactly `ratio` (matches the
old formula's own "fully proportional" base case); `strength>1` →
progressively more aggressive than proportional — but `ratio^strength` can
never go negative for any `strength >= 0`, no matter how high, since `ratio`
itself is always positive. Only a negative `strength` is still guarded against
(clamped to `0` on config load), since `ratio^negative` blows up toward
infinity as `ratio` approaches `0`.

---

## 9. Crouch/prone rewired to the real togglecrouch/toggleprone toggle (2026-07-16)

**Trigger:** live-reported — a Campaign session got stuck prone (unrelated repro
from issue #10 below) in a way neither the controller's B button nor Sprint could
recover, but real keyboard Ctrl (bound to `toggleprone`) could. That directly
implied our own crouch/prone implementation (a tracked `g_stance` enum + per-frame
raw usercmd-bit forcing) was fighting the real engine's own stance state rather
than reading/driving it.

**Found the real function** via the exact technique already proven for weapnext/
D-pad: live-read the raw-keycode dispatch table (`value = *(int32_t*)(0xA98E4C +
keyCode*12)`) for the actual keys bound to `togglecrouch`/`toggleprone`
(`players2/config.cfg`: `C` → togglecrouch, `CTRL` → toggleprone). `C` (ASCII
`0x43`) reads case `0x48`; `CTRL`'s real internal keycode is `0x9F` (**not**
Windows' `VK_CONTROL`=`0x11` — this table uses the engine's own Quake-derived key
enum, the same lesson learned the hard way during the F5 hunt), reading case
`0x49`. **Both dispatch to the same function, `FUN_0057d2c0(playerIndex, mode)`** —
this is almost certainly the exact function from the earlier F5/ready-up hunt that
was "confirmed wrong" and got a player stuck prone (see issue #5) — that test was
never the function's fault; it was our own competing per-frame bit-forcing (active
throughout that whole test) fighting it, the identical mechanism just confirmed
here via the Ctrl repro.

**Confirmed via raw disassembly** to be a genuine `__fastcall` (`ECX`=playerIndex,
`EDX`=mode, no custom register convention needed):
```
EAX = playerIndex * 0x230
if (byte[EAX + 0xA98CA0] != 0) return;      // guard 1
if (byte[EAX + 0xA98BC4] != 0) return;      // guard 2
ECX = &DAT_00B363B0 + playerIndex*0xBE5C
current = *(int*)(ECX + 0x1C)
*(int*)(ECX + 0x1C) = (current != mode) ? mode : 0;   // genuine toggle
```
A real toggle between 0 (standing) and `mode` (1=crouch, 2=prone) — and its own
semantics already implement this mod's entire B-button stance ladder natively
(Standing+togglecrouch→Crouched, Crouched+togglecrouch→Standing,
Crouched+toggleprone→Prone since 2≠1, Prone+toggleprone→Standing since 2==2,
Prone+togglecrouch→Crouched since 2≠1) — no separate state machine needed.

**Fix:** replaced `g_stance` (enum + tracked copy, asserting a usercmd bit every
frame based on our own bookkeeping) with direct `ToggleStance()` calls on B's
tap/hold transitions, and a live `GetRealStance()` read (of the correct `+0x1C`
field — the pre-existing `LogStanceDiag` diagnostic had been watching the wrong
offset, `+0x0`, since it was first added) for the per-frame usercmd-bit assertion
Pmove still needs. Sprint's stance checks and its "auto-stand from crouch/prone"
logic were updated to match (`ForceStandingViaRealToggle`). **CONFIRMED WORKING
LIVE** by the user, including recovery from the original stuck-prone repro.
**Also fixed issue #10 below as a side effect** (same class of bug: stale
bit-forcing fighting a real state change mid-sequence) — **confirmed live**:
Predator missile while prone in the first mission no longer gets stuck.

---

## 7. Remaining unassigned controller inputs

**Status:** Open, tracked as task #5 (Back, deprioritized), #7 (killstreaks, not yet
scoped), and #9 (sprint's Extreme Conditioning override).

| Input | Intended action | Blocker |
|---|---|---|
| Back | `+scores` (scoreboard) | Reverted after a live regression (see issue #3 above) — needs the live-keycode-table technique applied to TAB, not another bind-table-index guess. Deprioritized (nice-to-have, not gameplay-defining) |
| Killstreaks | Predator missile confirmed partially working; needs per-killstreak investigation | Not yet scoped — needs live testing to characterize what's actually broken (camera control? fire trigger? exit-early?) before any RE work starts |
| Sprint / Extreme Conditioning | Perk should double sprint duration to 8s | Not yet investigated — likely `perk_sprintMultiplier` (a real dvar, confirmed to exist), needs a way to detect the perk is equipped/active and read its live scale value |

---

## Resolved this session (for contrast — see `iw5sp.md` for full write-ups)

- **Sprint (L3):** real `pm_flags` bit (`0x4000`), forced via a Pmove-entry hook plus a
  reassert hook one level deeper. Confirmed working live. Also fixed: sprint no longer
  engages while crouched/prone (auto-stands first, matching console).
- **Reload (X):** real static `kbutton_t` found via memdiff + a new pointer-scan
  feature, wired up via the same `CallKbuttonDown`/`CallKbuttonUp` technique as ADS.
  Confirmed working live.
- **Buy-station + pause movement lockout (issue #1 above):** reinstated the 3-second
  rising-edge gate window that an unrelated same-day architecture change had silently
  replaced. Confirmed working live across the full test matrix.
- **Start opens AND closes the pause menu (issue #2 above):** real hardcoded ESCAPE path
  for opening, plus a `WndProc` subclass hook (replacing a confirmed-dead `Present` hook)
  and `FUN_004396d0`'s real mode-0 unpause case for closing. Confirmed working live across
  multiple full open/close cycles.
- **Y/weapnext (issue #2 above):** live-read the real raw-keycode dispatch table to find
  `FUN_00438710` case `0x42` → `FUN_004a5f70` → `FUN_0057a670`'s weapon-cycling logic.
  Confirmed working live.
- **D-pad / `+actionslot 1-4` (issue #4 above):** live-read the real raw-keycode dispatch
  table for all four slots (catching a lowercase-vs-uppercase key-code gotcha along the
  way), found `FUN_00438710`'s clean case pattern → `FUN_00410ad0`/`FUN_0044ec40`.
  Confirmed working live.
- **Survival ready-up / hold Y (issue #5 above):** the real native call was never found
  despite an extensive search across multiple mechanisms — solved instead with an
  explicit, user-approved exception to this mod's "no OS-level input emulation" rule: a
  synthetic F5 keypress via `PostMessage`, gated to Survival maps only. Confirmed working
  live; the only deliberate departure from real-engine-call-only input in the whole mod,
  to be replaced if a native call is ever found.
- **Sprint stamina/cooldown (issue #6 above):** the real native duration/timer function
  was never found (only the speed-scale consumer, with no timer logic, was traced) —
  implemented as our own 4s-deplete/2s-cooldown layer instead, using real MW3 values,
  with a fixed-duration cooldown timer (not a continuous-float threshold) after catching
  a live regen-flicker bug. Bypassed correctly when the real `player_sprintUnlimited`
  dvar is live-set by a mission. Confirmed working live. Extreme Conditioning's
  `perk_sprintMultiplier` override remains open (see issue #7 above).
