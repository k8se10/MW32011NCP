# Known Issues — `iw5sp.exe` (Campaign/Survival)

Tracked as tasks in the working session; this file is the standalone reference so they
don't stay buried in `iw5sp.md`'s investigation log. Update status here as each is
resolved. Last updated 2026-07-16 (fixed a live keyboard-sprint regression caused by
our own controller hooks — see issue #10 — and, as a direct consequence, deprioritized
keyboard/mouse as a primary, actively-verified input path going forward — issue #11).

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

**Follow-up investigation, 2026-07-17, using our own real fastfile/GSC extraction**
(see the new tooling section further down): the `"+stance"` lead above was previously
sourced from a third-party GSC dump with an open doubt about whether it even covered
Survival-specific scripts. Now independently confirmed straight from our OWN retail
`common_survival.ff` (script `1571.gsc`, function `_id_3F83`):
`self notifyonplayercommand( "survival_player_ready", "+stance" )` is the exact real
per-wave ready trigger, no longer a third-party-sourced, unverified lead.

However, this remains a dead end for direct exploitation: `+stance` has **no default
PC keybind at all** (confirmed absent from `players2/config.cfg`) — a genuine
console-only leftover (real console MW3 readies up via holding B; `+stance` is
presumably what that maps to on that platform) with no ordinary PC keypress ever
reaching it. Forcing the raw usercmd bit some earlier session guessed for `+stance`
(bit `0x2`) was independently re-confirmed wrong this session too (that offset,
`+0x204`, isn't even touched by `FUN_0057dc90`'s real bit-summing code — the guess was
never actually disassembly-backed, just a table-position guess, exactly the class of
mistake issue #3 already warned about).

Also chased whether `F5`'s real command (`"skip"`, a genuine Infinity-Ward-shipped
default PC bind — confirmed present in the stock `players2/config.cfg`, not something
this mod or a user added) has a directly-callable native dispatch, to potentially
replace the `PostMessage` synthesis with a real call:
- `"skip"` is confirmed **absent** from the live `Cmd_ExecuteString` linked list
  (`DAT_017507d8`) — walked the full real list live (132 nodes), no match. Same
  conclusion as issue #2: core/mode-specific gameplay verbs bypass this generic
  dispatcher.
- Must therefore route through the same raw-keycode -> `FUN_00438710` case mechanism as
  `weapnext`/`togglecrouch`, but F5's real internal keycode wasn't found: Win32 `VK_F5`
  (`0x74`) reads `0` (unhandled) in the live dispatch table, and a live scan of the
  `0x80`-`0xC8` special-key range found only entries already accounted for by other
  known bindings (`0x99`=67=real PAUSE, `0x9F`=73=`toggleprone`/CTRL, plus a couple of
  unexplained values, one of which exceeds the dispatcher's own valid case-number bound
  and can't be a real case at all).
- **Conclusion: not pursued further.** The existing `PostMessage`-based F5 synthesis
  already triggers the real, Infinity-Ward-intended default PC command — it's not a
  guess or a hack standing in for an unknown mechanism, it's the actual shipped
  keybind, synthesized rather than pressed. Finding its exact native call point would
  be a nice-to-have architectural cleanup (one fewer OS-input-emulation exception) but
  isn't blocking anything, and the keycode space is large enough that further blind
  scanning has poor odds. Workaround stays as-is unless a future session finds a
  cleaner lead (e.g. a proper keynum enum reference, rather than more scanning).

**Workaround (explicit, narrowly-scoped user exception, 2026-07-15):** synthesize a real
F5 keydown/keyup via `PostMessage` at the game's own window (`GetGameWindow()`, exposed
from `d3d9_hook.cpp`'s WndProc hook), gated behind `IsInSurvivalMode()` (the
`"so_survival_"` mapname-prefix check, via `FUN_00498ec0("mapname")` — a plain
single-stack-arg `Dvar_GetString`-equivalent). This was the **sole deliberate exception**
to this project's "no OS-level input emulation" rule when first landed — a second,
narrower one was later added for D-pad Left's squadmate call-in (issue #14) — but every
OTHER button still drives the engine's real internal state directly. Justified here because: (1) the
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
**(2026-07-17 update: the perk's real internal name is confirmed —
`specialty_longersprint`, independently found twice this session via GSC decompilation
of `common_survival.ff` — `self setperk("specialty_longersprint",1,0)`/
`self unsetperk(...)`, and via the buy-station economy CSV,
`sp/survival_armories.csv`, category `airsupport`, cost 4000, wave-gate 35. Detection
mechanism itself — how to check "is this equipped right now" from native code — still
not found; a native `HasPerk`-equivalent function would need to be located, not yet
attempted.)**

**Separately found and fixed while investigating:** `Controller_DeltaTimeSeconds()`
(used for look) turned out to use a single **process-wide shared** static timer, not
one per call site despite its own doc comment claiming otherwise — a second caller in
the same per-frame tick would starve whichever call runs second to a near-zero delta
every frame (confirmed via reasoning during this investigation, before it could cause a
live bug). Sprint's stamina timer uses its own independent `GetTickCount()`-based clock
instead; the header comment was corrected to warn against this for future callers.

---

## 7. Remaining unassigned controller inputs

**Status:** Open, tracked as task #5 (Back, deprioritized), #7 (killstreaks, not yet
scoped), and #9 (sprint's Extreme Conditioning override).

| Input | Intended action | Blocker |
|---|---|---|
| Back | `+scores` (scoreboard/objectives) | **Key-synthesis workaround implemented 2026-07-17** (Back hold → real `WM_KEYDOWN`/`WM_KEYUP` for TAB, the confirmed real bind `bind TAB "+scores"` from `players2/config.cfg`) — third narrow exception to the no-OS-input-emulation rule, same pattern as ready-up (F5) and D-pad Left's squadmate call-in ('4'). **Live-tested in Campaign: no visible effect at all** — no scoreboard, no objectives overlay. Real cause not yet diagnosed; leading theory is `+scores`/scoreboard is fundamentally an MP concept that's a no-op in SP, and SP's actual "mission objectives" display (if player-triggerable at all) uses a completely different, still-unidentified mechanism — not necessarily the same feature CLAUDE.md's console-behavior description assumed. User explicitly parked this as a known UI gap to fill later ("these are both UI gaps we will fill as part of the improvements side of the mod"), not urgent. The synthesis code itself is harmless (real key event, just currently produces no observable result) and stays in the build. |
| Killstreaks | Predator missile confirmed partially working; needs per-killstreak investigation | **(2026-07-17, full GSC trace done, see issue #26)** `remote_missile` fully traced — real fire (`+attack`) and abort binds confirmed; leading hypothesis is raw-usercmd-bit Fire not reliably triggering the `notifyonplayercommand` the launch is gated on. `precision_airstrike` uses a placement/marker system, not a camera takeover. Turret and squadmate (`friendly_support_delta`/`riotshield`) call-ins are CONFIRMED separate script systems (correction to this row's own earlier framing) — squadmate bug's divergence point narrowed to unresolved function `_id_061C::_id_3DE2`. |
| Sprint / Extreme Conditioning | Perk should double sprint duration to 8s | **(2026-07-17: genuinely parked, see issue #26)** Perk's real name confirmed (`specialty_longersprint`), but no native `HasPerk`-equivalent query exists — `hasperk` dispatches by compile-time numeric ID with zero string trace in the binary, and `perk_sprintMultiplier` has exactly one reference (its own registration), confirming the scaling is entirely GSC-side. No clean native path without going through GSC itself — same dead-end class as Sprint's own kbutton search. |

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

## 10. Sprint pm_flags hooks broke vanilla keyboard sprint — RESOLVED (2026-07-16)

**Trigger:** live-reported during the same session as the sprint-kbutton memdiff
investigation (issue #6) — "sprint on k+m is broken it toggles once timesout then
never recovers."

**Root cause:** `InjectControllerSprintPmFlags` and `ReassertSprintPmFlags` are
hooked directly into the engine's real Pmove-tick entry points (`FUN_00644ed0`/
`FUN_00643ce0`), independent of whether `InjectControllerSprint()` (the function
that updates `g_sprintHeld`/`g_sprintWinded`) ever runs. `InjectControllerSprint()`
itself early-returns the instant `Controller_GetRawButtonsAndTriggers` fails — which
it always does with no controller in slot 0 (and, just as much, with an idle
connected controller nobody's touching) — so on keyboard/mouse play `g_sprintHeld`
sits permanently at its default-initialized `false`. `IsSprintActive()` reads that
same frozen `false`, so `InjectControllerSprintPmFlags` unconditionally ran its
`else` branch — `*flags &= ~kPmFlagSprint;` — clearing the REAL sprint bit on every
single Pmove tick, forever, regardless of what genuine keyboard Shift/
`+breath_sprint` input had just set it to a moment earlier. The real bit could
survive at most one tick before being stomped back off — matching the reported
"toggles once, times out, never recovers" exactly.

**First fix attempt (superseded same day):** gated both hooks on a new
`Controller_IsConnected()` (bare `XInputGetState` success check). This only covers a
fully-unplugged controller, though — user correctly flagged that a connected-but-idle
controller (a normal setup: controller sitting on the desk while actually playing
keyboard/mouse) hits the exact same bug, since `IsSprintActive()` is still false in
that case too. Detecting "which input device is currently active" isn't actually the
right question to ask at all.

**Actual fix — bit ownership tracking:** added a single `g_weOwnSprintBit` flag.
`InjectControllerSprintPmFlags` now only clears the real pm_flags bit if
`g_weOwnSprintBit` says WE were the one who set it (via a prior `IsSprintActive()`
tick); otherwise it leaves the bit completely alone, whatever native keyboard/kbutton
logic put there. `ReassertSprintPmFlags` (which only ever ORs the bit in, never
clears) now also sets `g_weOwnSprintBit = true` when it fires, so the two hooks share
one consistent ownership record. `Controller_IsConnected()` was removed entirely —
unused and unnecessary once ownership tracking makes the "is a controller present/
active" question moot. Rebuilt and redeployed; awaiting live re-confirmation on
keyboard/mouse.

**Lesson:** for a hook installed directly on a real per-tick engine entry point (as
opposed to being called from our own per-frame injector functions) that must
sometimes override native state, "is our input device active" is the wrong gate —
track literal ownership of the bit instead (only clear what you set), since that's
correct regardless of controller presence/idleness and needs no device-detection
heuristic at all. Worth an audit pass across the other Pmove-tick-level hooks in this
file for the same class of bug.

---

## 11. Keyboard/mouse deprioritized as a primary input path — decided 2026-07-16

**Not a bug in itself — a scope/priority decision, recorded here because it's a
direct consequence of issue #10 above and of the parked sprint-kbutton search (see
`iw5sp.md`, "Sprint's real kbutton — PARKED").**

Issue #10 showed our own controller-support hooks can silently break real keyboard
play when they're installed directly on a per-tick engine entry point rather than
gated through our own per-frame injector functions — and that class of bug is only
fully ruled out for the ONE case (sprint) we happened to go looking for this
session, after it had already shipped and been played on for a while before the
regression was even noticed. The sprint-kbutton search itself (three independent
techniques, all negative — see `iw5sp.md`) means we currently have no way to
migrate sprint's controller path off raw `pm_flags`-forcing onto the real kbutton
the way ADS/Reload were done, which is the cleaner, lower-risk mechanism —
so the raw-forcing pattern (the one now confirmed capable of silently breaking
keyboard input) stays in place for sprint indefinitely, not just temporarily.

**Practical guidance:** with this mod installed, keyboard/mouse play may exhibit
input weirdness that doesn't exist in a vanilla install — not because k+m support is
being actively removed, but because it is no longer receiving the same level of
verification attention as controller input going forward. Any further native input
work in this project prioritizes controller correctness first; keyboard/mouse is
tested opportunistically (e.g. "does it still work" spot-checks), not to the same
live-reproduction bar controller features get. **Recommendation: treat controller as
the primary, actively-verified input method with this mod installed; keep a
keyboard within reach and expect to fall back to it if something feels off**, per
the mod's own README/release notes.

**This is deprioritization of verification attention, not a suggestion to avoid k+m
— keyboard/mouse remains functionally essential, not optional, for exactly the areas
controller doesn't cover yet:** full menu/UI navigation (`re_notes/ui_assets.md`),
Back (`+scores`, task #5, unassigned), most killstreak call-ins (task #7, #13 —
D-pad squadmate call-in still fails 100%), and anything else without its own
controller-native implementation. Players will need a keyboard reachable during any
session regardless of this decision, not as a fallback for edge cases but as a
requirement for them. This is a standing caveat, not a one-time
release note — revisit only if a future session finds a reliable way to audit every
per-tick hook in this file for the ownership-tracking class of bug (see issue #10's
own "worth an audit pass" note) and re-verifies keyboard parity end to end.

---

## 12. `regbreak` live-breakpoint tool crashed the running game — abandoned in favor of static analysis (2026-07-16)

**Not a mod bug — a dev-only diagnostic tool (`tools/regbreak/`, never part of the
shipped mod) that caused a real live crash during use.** Built to automate
inspecting CPU register/struct state at a chosen address via the Windows Debug
API (`DebugActiveProcess` + a software `0xCC` breakpoint + `GetThreadContext`),
specifically to resolve what the aim-assist chain's `unaff_ESI` implicit
register context actually pointed to (see `iw5sp.md`'s aim-assist section)
without needing the user to drive an interactive debugger themselves while
mid-match.

**Root cause of the crash:** the tool only handled ONE thread hitting the
breakpoint before restoring the original byte and detaching. If a second thread
executed the same address concurrently before the restore completed, that
thread's `EIP` would resume one byte past the `INT3` while the underlying byte
had already reverted to the real instruction — a misaligned resume point that
decodes garbage mid-instruction. Confirmed via the Windows Application Event
Log (`Get-WinEvent -FilterHashtable @{LogName='Application'; Id=1000}`):
`Exception code: 0xc0000005` inside `iw5sp.exe`, fault offset unrelated to
either breakpoint address actually probed that session — consistent with this
exact failure mode, not a hang or clean exit.

**Resolution: abandoned, not hardened.** The user's explicit call ("static got
us most this way so") was to drop live breakpoints entirely rather than harden
the tool (suspend-all-other-threads + drain-all-pending-debug-events) and
retry. The very question `regbreak` was built to answer (`unaff_ESI`'s real
identity in the aim-assist chain) was fully resolved afterward through pure
static disassembly instead — see `iw5sp.md`'s aim-assist section for the
result. **Standing guidance:** prefer Ghidra disassembly/decompile
(`FindCallers.java`, `DumpDisasm.java`, `DecompileFuncs.java` in
`re_notes/ghidra_scripts/`) over live register inspection for this class of
question going forward; `regbreak.exe` should not be re-run against the live
game unless a future session gets explicit direction to harden and retry it.

---

## 13. B doesn't exit pause — RESOLVED (2026-07-16)

**Live-reported regression against the B-as-menu-back feature (task #19).**
B could open the pause menu fine (via Start) but pressing B while paused did
nothing — the menu never closed.

**Root cause:** `InjectControllerMenuBack()` (the function that forwards a
real ESC keypress via `FUN_004d9850` whenever the engine's own "menu active"
gate bit is set) was only ever called from `InjectAllControllerInput()`. That
function is explicitly documented (same file, just above
`InjectMenuInputTick`) to stop firing entirely once the game is genuinely
paused — it lives on the per-frame gameplay-simulation tick (`FUN_0057de60`),
and pausing halts simulation by design. This is the exact same reason Start's
own open/close logic (`InjectControllerPauseMenu`) had to be moved onto the
separate WndProc-subclass-driven `InjectMenuInputTick` tick (the only one
confirmed to keep running during pause) — but when `InjectControllerMenuBack`
was added for B, it was only wired into the dead-while-paused path, never
added to `InjectMenuInputTick`. So the one piece of logic that exists
specifically to act on an open menu never actually ran while a menu was open.

**Fix:** added `InjectControllerMenuBack();` to `InjectMenuInputTick()`
alongside the existing `InjectControllerPauseMenu();` call
(`analog_input_hooks.cpp`). The call left in `InjectAllControllerInput` stays
too (harmless/idempotent during normal unpaused gameplay, same redundancy
rationale already documented for the pause-menu call there).

**Second live-reported regression from this same fix: crouch fired on exiting
pause.** B is the same physical button used for both "close menu" and
crouch/prone (`InjectControllerButtons`), and that function's own tap/hold
edge-tracking state (`g_crouchButtonWasHeld` etc.) goes stale while paused,
since the whole function is dead during pause (same root cause as the bug
above). The still-in-flight B press that closed the menu looked like a brand
new press the instant gameplay resumed, and its release fired an unwanted
crouch tap.

A first fix attempt (a one-shot flag set on any global menu-active→inactive
transition, consumed once by `InjectControllerButtons` to silently resync
state) was rejected before being tested live: it would misfire if some
*other* menu opened/closed while B was already held down for an unrelated
gameplay press, since it reacted to any menu transition rather than tracking
whether B's own current press had actually touched a menu. **Corrected fix:**
`g_currentBPressTouchedMenu`, maintained continuously by
`InjectControllerMenuBack` (which — unlike `InjectControllerButtons` — keeps
running across a pause): set true the moment B is held while a menu is
active, reset on B's own next rising edge. `InjectControllerButtons` gates
both the tap-fire and hold-fire calls on this flag (not the edge-tracking
bookkeeping itself, which still runs unconditionally so state never desyncs).
This makes the suppression scoped to B's actual current press rather than to
any menu-active transition, so an unrelated menu open/close elsewhere can
never suppress a genuine gameplay crouch/prone press. **CONFIRMED LIVE**
(2026-07-16): B closes the pause menu without triggering crouch, and B also
correctly backs out of a buy-station menu the same way, with normal
crouch/prone tap/hold unaffected elsewhere.

**Third live-reported issue, same session: pausing while a buy-station menu
is open leaves it stacked underneath the pause menu.** Pressing Start while
a non-pause menu (buy station, etc.) is already open called
`SetMenuState(pausedmenu)` directly without closing that menu first, so the
pause menu opened on top of it instead of replacing it -- and unpausing would
have dropped the player back inside the buy-station menu rather than
straight into gameplay.

**Fix:** `InjectControllerPauseMenu`'s opening branch now checks
`IsMenuActive()` (the same real gate bit B's own menu-back logic reads)
before opening pause, and if a menu is already active, forwards a synthetic
ESC down+up via `ForwardKeyToMenu` to close it first -- the exact same real
mechanism B itself uses, just triggered by Start instead of a physical B
press. Deliberately does NOT go through `InjectControllerMenuBack` or touch
`g_currentBPressTouchedMenu`/`g_menuBackHeld`: this is a synthetic close
tied to Start, not a real B press, and must not interact with B's own
press-tracking state (see the crouch-misfire fix just above for why that
state has to stay scoped to B's actual physical presses only). Since the
buy-station menu is now genuinely closed (not just hidden/suspended) before
pause opens, unpausing naturally drops the player straight back into
gameplay with no extra handling needed. Rebuilt; live-verification still
pending (re-test: Start while a buy-station menu is open closes it and opens
pause cleanly on top of plain gameplay, and the later unpause returns
directly to gameplay, not back into the buy-station menu).

---

## 14. D-pad Left squadmate call-in failed 100% (task #13) — fixed via a narrowly-scoped key-synthesis exception (2026-07-16)

Turret call-ins (D-pad Left, actionslot4) worked fine; AI-squadmate call-ins,
purchased at the same buy station on the same slot (a different loadout
choice), failed every single time. Both go through the identical native call
pair `FUN_00410ad0(playerIndex,3)` / `FUN_0044ec40(playerIndex)` — confirmed
byte-for-byte identical to what the real key dispatcher (`FUN_00438710`)
itself calls for the real `'4'` key, via direct disassembly of both call
sites, not a guess.

**Investigation trail (all ruled out or confirmed in order):**
- A whole-binary ASCII string search found dozens of native strings for
  turret/sentry (`ET_TURRET`, `G_SpawnTurret`, `sentry_placement_trace_*`,
  etc.) and **zero** occurrences of "squad" or any ally-call-in terminology
  anywhere — strong evidence the squadmate call-in has no native C++
  implementation at all.
- A live `memdiff rangewatch` across the known kbutton-neighborhood
  (`0xA98B00`-`0xA98D00`) found zero candidates correlating with the real
  `'4'` key — a clean negative, later explained: that region's bytes are a
  one-frame edge-latch consumed by the engine every tick, not a stable
  hold-state byte an async external read can reliably catch (unlike ADS/
  Reload's true kbutton_t's).
- Direct disassembly of `FUN_0057dc90` (the button-summing function) found a
  contiguous `{down, latch}` byte-pair array that DOES include an offset
  matching the position `+actionslot 1-3` were previously found at (`+0x1b4`/
  `+0x1c8`/`+0x1dc`, extending to `+0x1f0`) — but disassembling the REAL
  dispatcher's own call sites for the D-pad keys directly (not a table-order
  guess) showed those cases call `FUN_00410ad0`/`FUN_0044ec40` ONLY, never
  touching this byte array at all. **The earlier "table idx = actionslot N"
  mapping was wrong** (exactly the kind of mistake issue #3 already warned
  about) — that offset family belongs to some other, unrelated set of binds.
  Caught before a wrong fix shipped.
- User-confirmed: real keyboard `'4'` (same bind) works perfectly with the
  mod installed — rules out any global regression; this is specific to the
  controller injection path only.
- User-confirmed: deliberately holding D-pad Left longer (vs. a quick tap)
  made no difference — rules out a switch-completion timing race.
- User-confirmed: this call-in is unique to Survival, not shared with
  Campaign's turret/killstreak system — strong signal it's driven by
  Survival's own GSC scripts (same layer that turned out to own the ready-up
  mechanic, see issue #5), not native code, and is very likely watching for a
  genuine key event our direct native call never produces.

**Fix — EXPLICIT, NARROWLY-SCOPED EXCEPTION (user-approved 2026-07-16), same
category as ready-up's F5 synthesis:** for D-pad Left ONLY, synthesize a real
`WM_KEYDOWN`/`WM_KEYUP` for `'4'` via `PostMessage` at the game's own window,
instead of calling `FUN_00410ad0`/`FUN_0044ec40` directly — so whatever's
actually watching (GSC or otherwise) sees exactly what a real keyboard press
produces. The synthesized key still flows through the real dispatcher itself,
so turret-type items on this same slot continue working through the normal
path (deliberately NOT calling the native functions in addition to the
synthesis — doing both would double-dispatch). **The other three D-pad
directions are unchanged**, still using the direct native call, since nothing
about them has been reported broken.

**Explicitly not a general policy change.** Per the user's own direction:
"we will trace all these non natives later on" — this (and ready-up's F5
synthesis) are workarounds pending the real GSC-side mechanism being found,
not permanent design choices. **CONFIRMED WORKING LIVE by the user** (2026-07-16):
squadmate call-in via D-pad Left now succeeds using the synthesized '4' keypress.

**Bonus fix, same mechanism: turret could not be un-toggled once deployed.** Live-
reported earlier the same session (pulling out a turret via D-pad Left left no way to
put it away again) and initially parked as a separate investigation pending native-path
work on this slot, since the input mechanism was mid-change. **Turns out to be the same
root cause, and genuinely a bug in the OLD implementation, not real console/native
behavior**: the real native toggle for `+actionslot4` is a plain press-to-toggle (press
once to deploy, press again to put away) — but the OLD direct `FUN_00410ad0`/
`FUN_0044ec40` call pair only ever drove the "deploy" side correctly and had no working
toggle-off path, so turret truly was partially broken before, not just missing a
"native limitation" the mod correctly reproduced. The key-synthesis fix above resolves
this too, for free, since the synthesized keypress goes through the real dispatcher's
own toggle logic exactly like a real key press would. **CONFIRMED WORKING LIVE by the
user** (2026-07-16): turret can now be deployed and put away with repeated D-pad Left
presses. No further work needed on this specific report; task tracker entry closed.

---

## 15. Aim assist entity classification — PARKED (2026-07-17): a promising `game_entity`-equivalent struct found, but the cross-link to it broke live

**Status:** Open, tracked as task #16. **Aim assist is completely non-functional at
this stage** — not just unpolished, genuinely broken targeting behavior — and is
disabled in the shipped config (`[AimAssist] Enabled=0`). Must stay disabled for
any public/release build until this is resolved; do not re-enable outside active
development/testing.

**Background:** the movement-based target filter implemented earlier the same
session (a static prop never moves; a living AI's position changes the moment it
walks) proved the rest of the aim-assist pipeline works (math, curve, friction,
magnetism all confirmed correct via live diagnostic logging), but the filter itself
oscillates between multiple simultaneously-valid movers (a real enemy, a settling
ragdoll, thrown grenades) and the user explicitly rejected patching this further —
a real type/health-based classification was needed instead.

**Reference-repo cross-check (user-directed) found a real, second entity array in
our own binary.** `references/mw3-surviv0r/mw3-surviv0r/ft_aimbot.cpp`'s actual
validity check reads `m_iType`/`m_iHealth` from a struct called `game_entity`
(0x270 bytes), completely separate from `centity` (0x194 bytes, the struct our own
existing entity array — and this whole session's earlier `+0xcc` type-byte
investigation — has been reading from). A new Ghidra script,
`FindStrideArrayBase.java` (isolates `IMUL reg,reg,<stride>` + `ADD (same
reg),<base>` idioms), found **92 clean matches for stride `0x270`, 89 of which
share the exact same base address, `0x01197AD8`**, across ~40 independent
functions — very strong static confirmation this is a real, heavily-used array in
our own vanilla binary, not just an artifact of the differently-patched reference
binary.

**Field offsets independently confirmed via decompiling ~25 of the 92 call sites**
(full detail in `iw5sp.md`): `+0xd4` and `+0xec` are both real Vec3 fields
(matching the reference's `m_vecPositionUp`/`m_vecWritablePosition` exactly),
`+0x150` is read as `0 < value` in a threat-detection function (matching the
reference's `m_iHealth` exactly, same offset, same alive-check usage), `+0x110` is
a pointer null-checked for validity and zeroed on entity teardown, and `+0x0` is a
type byte checked `== 3` (consistent with an idTech `ET_MISSILE`-style value) in a
function that also reads a clientnum-style `ushort` at `+0x84` — matching the
reference's `m_iClientNum` position.

**What broke live:** the missing piece was how to go from an index in our existing
`centity`-equivalent array to the matching slot in this new array. Hypothesized
`centity`'s own `+0x150` field was a clientnum cross-link (matching the reference's
`centity.m_clientnum@0x150` by name/position). Built a diagnostic-only build (extra
logging only, no gameplay change) to test this against real AI during a live
Survival session. **Result: disproven.** For real, known-moving AI indices, the
"clientnum" read back as multi-million garbage values that scaled roughly linearly
with the centity index (adjacent indices → adjacent huge numbers — looks like a
counter or address, not a clientnum). For the few reads that landed in a small
plausible range, the value was suspiciously always exactly equal to the centity
index itself — coincidence, not a real link. `centity+0x150` is not the connector
(or isn't being read correctly at that offset/width).

**Decision at the time:** parked per explicit user instruction, rather than
immediately pivoting to the next diagnostic (sampling `0x01197AD8`'s own `+0xec`
position field for movement directly, sidestepping the cross-link question
entirely). The disproven diagnostic logging was removed from
`analog_input_hooks.cpp`; `FindStrideArrayBase.java` was kept and committed as a
genuinely reusable static-analysis tool.

**Follow-up (2026-07-17): that next diagnostic is done, and the classification
problem is very likely solved — no live implementation yet, static case is
strong.** Two more real functions independently confirmed the array's shape,
entirely without needing the broken `centity` cross-link:

- `FUN_005c9a30` (a real nearby-entity/splash-query function) reads AND writes
  `+0xec` as a live, mutable Vec3 position (`*(float*)(param_1+0xec) += delta`,
  used in actual physics/bounds math) — independent reconfirmation from a
  completely different function than the ones already found.
- `FUN_005cc530` (a real checkpoint/save-deserialization function) proves the
  array is fixed-capacity **2048 slots (`0x800`)**, sentinel-terminated (`-1`)
  during save/load, with a parallel **one-byte-per-slot validity-flag array at
  `DAT_01357a98`** (nonzero = populated) walked directly alongside it — meaning
  the WHOLE array is independently walkable with zero dependency on `centity` or
  any clientnum at all: `for i in 0..2048: if flag[i] then process entity[i]`.
- The SAME function's deserialization tail contains
  `if (type == 0x0D || type == 0x0F) { FUN_00449610(..., "defaultactor"); }` —
  **type `0x0D` (13) independently confirmed as the real AI/actor type**, from our
  own vanilla binary, not just the external reference repo's own claim. Type `0x0F`
  (15) is grouped in the same check, plausibly a dead/ragdolled substate, not yet
  distinguished further.

**Net effect: `type==13 && health>0` (both already-confirmed real fields) is
genuine native classification, position (`+0xec`) is a separately-confirmed real
field for aiming math once a valid target is chosen, and the whole thing walks
independently via the validity-flag array** — no movement heuristic, no broken
cross-link needed anywhere. This plausibly replaces the oscillating movement
filter entirely. **Still needs a live diagnostic pass against real moving AI
before shipping** (same bar as everything else in this project) — not yet
attempted, next actual implementation step whenever this is picked back up.

---

## 22. Real controller menu navigation (D-pad + A) — RESOLVED, confirmed live (2026-07-17)

**Status:** Closed. Task #22. Per the user's direction to go all-in on reverse
engineering the UI/menu system (task #6's original scope), this delivers real,
native D-pad item navigation and A-as-select across the main menu, pause menu, and
options screens.

**Approach:** extracted the game's own plain-text `.menu` UI definitions straight
out of `zone/english/ui.ff`/`ui_mp.ff` via OpenAssetTools (same pipeline built
earlier this session for GSC/aim-assist work) — 319 real menu files, including the
Survival armory/buy-station menus and the full `pc_options_*` settings screens.
Decompiled the real key-event handler chain already partially known from B's
ESC-forward work: `FUN_00541020` (real key-event handler) → confirmed
`FUN_004d9850`/`ForwardKeyToMenu` is NOT ESC-specific, it forwards ANY keycode
whenever the same menu-active gate bit (`0x10` at `0xB36210`) is set → traced into
`FUN_004dfd30`, the real generic key-to-menu dispatcher, and read its actual keycode
switch statement directly rather than assuming standard constants.

**First attempt was wrong, live-tested and corrected.** Initially guessed the
standard idTech/Quake3 `K_UPARROW`/`K_DOWNARROW` values (128/129), reasoning that
since ESC (`0x1b`/27) already matched standard ASCII, the whole keycode space
probably followed the same numbering. **Live test: "nothing" happened.** Decompiling
`FUN_004dfd30`'s actual switch statement showed 128/129 don't appear anywhere in it
— the guess was simply wrong. The real values, read directly out of the switch:
Group A (`9, 0x9b, 0x9d, 0xbd, 0xcd`) all call `FUN_006253d0`, confirmed via
decompile to increment a focus-index field (wraps at item count) — genuine native
"next item". Group B (`0x9a, 0x9c, 0xb7, 0xce`) all call `FUN_00625290`, confirmed
to decrement the same index — genuine native "previous item". `0xd` (13, Enter) was
already correctly guessed for select/activate, confirmed by its own switch case
handling a selected-item pointer.

**Second live test revealed Left/Right needed different keycodes than Up/Down, not
the same ones.** First implementation unified Up+Left → "previous" (`0x9a`) and
Down+Right → "next" (`0x9b`), reasoning the two functions above have no concept of
on-screen direction so it wouldn't matter which physical button sent which. This
partly worked (main menu's horizontally-laid-out first page responded correctly
once Left/Right were wired at all), but broke down on options-style two-pane
screens (category list on the left, that category's own settings list on the
right — see the user-supplied screenshot marking the awkward "green" (working)
category-list navigation vs "red" (broken, required scrolling all the way through
past ESC) settings-pane navigation). The user confirmed real keyboard Left/Right
already does this correctly natively and pointed at checking the `.menu` files for
how — found the actual mechanism in `ui/pc_options_video.menu`:
```
execKeyInt 157 { if (getfocuseditemname() == "OPTIONS_LIST_0" || ...) {
    setfocus localvarstring(ui_options_focus); } }   // drill IN to settings pane
execKeyInt 156 { if (getfocuseditemname() == "color_blind" || ...) {
    setLocalVarString ui_options_focus getfocuseditemname();
    setfocus OPTIONS_LIST_0; } }                     // drill OUT to category list
```
`156`/`157` are real, separate keycodes from `0x9a`/`0x9b` (though still alternate
members of the same two generic Group A/B next/previous cases) — meaning options
screens get free, native, context-aware drill-in/drill-out on Left/Right, while
every other simpler menu (pause menu, armory, main menu) falls through to plain
generic previous/next, with zero custom "am I inside a submenu" state-tracking
needed on the mod's own side. This is the real, general mechanism — not a per-menu
special case the mod has to maintain.

**Final implementation:** Up = `0x9a`, Down = `0x9b`, Left = `0x9c`, Right = `0x9d`,
A = `0xd` (Enter), all via `ForwardKeyToMenu`. D-pad's normal gameplay actionslot
dispatch (`InjectControllerDpad`) and A's normal Jump bit (`InjectControllerButtons`)
are both suppressed while `IsMenuActive()` so D-pad/A can't mean two things at once
— same dual-purpose-button pattern already established for B (ESC-forward vs
crouch/prone). **Confirmed working live** by the user across the main menu, pause
menu, and the `pc_options_video`-style two-pane settings screens.

**Not yet covered:** buy-station/armory `itemDef`s (Survival) haven't been
separately live-verified with this exact mechanism, though the same generic
Group A/B dispatch should apply identically (they're plain single-pane vertical
lists, the simpler case already confirmed via the pause menu). Slider-type settings
items (`type 10`, e.g. `dvarFloat "sensitivity" 5 1 30`) have an empty `action{}`
block and their only found value-adjust path (`FUN_00625510`) is gated on mouse-
wheel-shaped keycodes (`200`/`0xc9`/`0xca`), not the Left/Right codes used for
pane-drilling — adjusting a slider's actual VALUE (not just navigating to/from it)
with a controller remains unsolved and is a natural next step. Real button-glyph
UI prompts (task #6's other half, see `ui_assets.md`) also remain unstarted.

---

## 23. Real controller options menu — native zone/menu injection, blocked on a real architectural limit (2026-07-17)

**Status:** Open, in progress. Task #23. Full technical trail in `iw5sp.md`'s "Real
controller options menu" section — this is a summary.

**Goal:** a real controller-options screen integrated into normal in-game Options
navigation (not a special-combo popup), via injecting a compiled `.menu` asset
through the game's own real zone-loading system, entirely in memory — the real
`ui.ff` stays untouched on disk.

**What works, confirmed live:** the full pipeline — `LoadZones` → `FindOrLoadMenuList`
→ register into the real menu registry (own reimplementation, `RegisterMenu`) →
switch the engine into paused/menu render mode (`SetDvarByName("cl_paused",1)` +
`SetPlayerMenuFlags`) → `OpenMenuByName` — genuinely works. A bare custom menuDef
(no background material) rendered correctly in the real pause menu's own slot,
screenshot-confirmed, alongside the real Mission Objectives panel.

**What doesn't work, and why (the actual blocker):** any REAL menu content —
including a modified copy of the real `pc_options_controls_ingame` — produced a
live black-screen flash. Traced through two wrong theories (menu-stack parent/child
hierarchy; wrong hook/calling context — both ruled out by direct evidence) to the
real cause: **real menus have background materials, and loading a material live
triggers a genuine, synchronous D3D9 GPU-resource-creation cascade** (technique
set → vertex/pixel shaders, plus real texture creation if the material references
one) that isn't safe to do outside the engine's own controlled loading-screen
context. Confirmed via deep decompilation, not inference: `FUN_004b6b70`'s
material case cascades into the exact same technique-set loader its own shader
cases use.

A natural fix attempt — reference the material by NAME only, don't embed it,
relying on the already-loaded real copy in `ui.ff` — turns out not to help: the
Linker requires an explicit `material,,<name>` zone entry for any material a
compiled menu references (a hard compile error otherwise), and what it embeds is a
full, standalone, separately-addressed COPY, not a lazy cross-zone reference. That
copy gets loaded as our own zone's OWNED asset at `LoadZones` time, unconditionally
— it never goes through the name-keyed interning/cache-hit path that would
otherwise skip re-creating an already-loaded material. **Confirmed: rendering an
already-loaded menu's background is completely safe (a plain pointer dereference,
no cascade) — the danger is entirely and unavoidably at zone-LOAD time, baked in
by how the Linker compiles cross-zone material references.**

**Net conclusion: a menu with zero background material is the only content class
confirmed safe to load via this live-injection pipeline.** Any real menu — even
one that only references existing `ui.ff` material names — cannot be made safe this
way. Making real content work would require loading through an actual
`FUN_0053cbc0`-driven level-load transition instead of a live WndProc/`SetTimer`
hook, a substantially different architecture not yet attempted.

**Also abandoned, separately:** same-name registry override (overwrite an
already-registered menu's slot in place to make a modified copy supersede the
original) — produced its own live black-screen flash, root-caused to skipping the
real engine's own asset-interning call (`FindOrLoadAsset`), which is architecturally
built to hand back the EXISTING cached entry for an already-registered name, not
adopt new content under it. Fixed in the current `RegisterMenu` (always calls the
real interning step) but the override branch itself was dropped per explicit user
decision — not pursued further, in favor of unique internal names.

**Current strategic direction (not yet implemented):** unique internal names for
our own menu content, plus finding/patching whatever real call site opens
the ORIGINAL name (e.g. `"pc_options_controls_ingame"`) so it redirects to ours —
keeps `ui.ff` completely untouched, purely additive. Given the materials finding
above, this only works for backgroundless content, or requires solving the
level-load-transition problem first. **Update 2026-07-17: the level-load-
transition path is now believed structurally sound** — see `iw5sp.md`'s
"Level-load-transition alternative" section for the full mechanism (hook
`FUN_004ca310` itself, return-address-match two known real call sites, append our
own zone entry). Recommended next step for this task, before the `ui.ff`-
replacement installer fallback.

---

## 24. Vibration/rumble — research pass, real trigger points found (2026-07-17)

**Status:** Open, tracked as task #17. Not yet implemented — this is groundwork
only.

No native vibration infrastructure exists at all (confirmed via a clean
zero-hit string search for `rumble`/`vibrat`/`forcefeedback`), consistent with
the project's founding "zero controller path" finding — output must be entirely
our own `XInputSetState` calls. Research found real, hookable native events for
WHEN to trigger them:

- **Weapon fire — confirmed, single clean choke point.** `FUN_0045e320`
  (per-shot fire-effects handler) calls `FUN_004895b0(entity, "weapon_fired"
  handle, 1)` once per real shot, semi-auto and full-auto alike. Confirmed plain
  `__cdecl` via raw disassembly — safe to hook with the mod's existing pattern.
- **Damage — confirmed, with usable intensity data.** `FUN_0045f770` decrements
  health at `+0x150` (same entity-struct family as the aim-assist `0x01197AD8`
  array) then calls `FUN_0044cdb0("damage" handle, entity, ...,
  literalDamageAmount, ...)` — the damage amount is directly available for
  rumble-intensity scaling. Fires for ANY damageable entity, not just the local
  player — a real implementation needs a local-player filter, not yet resolved.
  Death is a separate notify on the same code path.
- **Not yet reached:** explosions/blast-proximity, melee-hit-landed, killstreak
  activation, low-ammo. Strong leads exist (an incidentally-found ~600-entry real
  GSC notify-event-name table at `FUN_00470d00` includes `"explode"`,
  `"grenade_fire"`, `"missile_fire"`) but none traced to a dispatch site yet.

Full detail, including the two general native notify-dispatch function
signatures found (`FUN_004895b0`/`FUN_0044cdb0`, useful beyond just this task —
any future GSC-adjacent hook work will likely hit one or the other), in
`iw5sp.md`'s "Vibration/rumble trigger points" section.

---

## 25. MW3 client compatibility — Plutonium/AlterWare/DeckOps survey (2026-07-17)

**Status:** Research only, no implementation. Long-term goal (user, 2026-07-17):
eventually support all major MW3 client variants, not just retail Steam.

**Plutonium** (third-party MP-revival client, closed-source, requires legitimate
retail game files as a base) is installed locally — direct comparison done:
- **`iw5mp.exe` is byte-identical to retail Steam** (SHA256 match, exact) — any
  future MP work would apply with zero re-discovery.
- **`iw5sp.exe` is a DIFFERENT binary** (~175KB smaller, differences start within
  the first 100 bytes) — every hardcoded address this project uses would need
  independent re-verification. Also, Plutonium's own docs confirm campaign isn't
  really their supported use case even though the file is present.
- **Anti-cheat concretely confirmed to ban DLL injection and memory access** —
  7-day first offense, permanent after. This mod's entire architecture (proxy
  `d3d9.dll`, MinHook, memory-read-based aim-assist) is exactly what it's built to
  catch, input-only intent notwithstanding. **This sharpens CLAUDE.md's existing
  "MP anti-cheat exposure" flag from a theoretical concern into a confirmed,
  specific, high risk for Plutonium MP specifically** — do not use this mod with
  Plutonium MP.

**AlterWare IW5-Mod** — a distinct, closed-source client specifically for MW3
**Singleplayer + Spec Ops** (not MP), launched via its own separate `iw5-mod.exe`
(not `iw5sp.exe`). Not installed locally, not yet acquired/compared. No known
anti-cheat concern found. **The most promising third-party target given this
project's own SP+Survival-first scope** — but confirming feasibility needs the
actual binary, full from-scratch RE, no shortcut available.

**DeckOps** — NOT a separate client. An installer/automation tool for Steam Deck
that sets up other community clients; for MW3 specifically it uses Plutonium
under the hood. Inherits every Plutonium finding above (same binaries, same
anti-cheat risk), plus an untested open question of whether DLL injection/D3D9
proxying behaves correctly under Proton/Wine's D3D9 translation layer at all.

Draft compatibility table (not yet added to README — pending a decision on final
wording, especially the anti-cheat callout):

| Client | SP/MP | Binary vs. retail | `d3d9.dll` injection viable? | Status |
|---|---|---|---|---|
| Retail Steam | Both | — (baseline) | Yes (confirmed, current target) | Actively supported |
| Plutonium — MP | MP | `iw5mp.exe` byte-identical | Believed yes (same binary) | **Not recommended — confirmed anti-cheat bans DLL injection/memory access** |
| Plutonium — SP | SP | `iw5sp.exe` different (~175KB, not just patches) | Unknown, needs re-verification | Not yet investigated |
| AlterWare IW5-Mod | SP + Spec Ops | Separate `iw5-mod.exe`, not yet acquired | Unknown | Not yet investigated — most promising, no known anti-cheat concern |
| DeckOps (MW3) | MP (via Plutonium) | Same as Plutonium MP | Unknown — Proton/Wine D3D9 layer untested | Not yet investigated — inherits Plutonium's anti-cheat risk |

---

## 26. Full-breadth engine research pass — killstreaks, weapons, perks, HUD/UI, AI/vehicles, physics/health, MP (2026-07-17, later session)

**Status:** Research complete, no code changes. Full detail in `iw5sp.md`'s "Full-breadth research pass" section — this is an index/summary.

User direction: research "everything" about the engine across SP/Survival/MP via
a large parallel batch of research-only forks, ahead of wrapping the session.

- **Killstreaks (task #7)**: `remote_missile` (Predator) fully traced — real
  fire/abort binds confirmed, plus an actionable hypothesis that its "partial"
  behavior stems from Fire being raw usercmd bits rather than a real `+attack`
  command dispatch, which the killstreak's `notifyonplayercommand` gate needs.
  **Important correction to task #13's own framing**: turret call-in and
  `friendly_support_delta`/`riotshield` (squadmate) call-in are CONFIRMED
  completely separate script systems, not two branches of one — "works for
  turrets, fails for squadmates" was never a valid comparison. The squadmate
  bug's real divergence point is narrowed to one specific unresolved function,
  `_id_061C::_id_3DE2`.
- **Weapons**: real `WeaponCompleteDef`/`WeaponDef` struct found and confirmed
  via exact offset arithmetic. Separate native timers for normal reload vs.
  reload-from-empty exist — this mod's single-kbutton reload almost certainly
  already gets correct behavior for free. A per-weapon-animation rumble-
  notetrack system found, relevant to task #17.
- **Perks (task #9)**: `HasPerk`-equivalent native query — genuinely parked, not
  solved. `hasperk` dispatches by compile-time numeric ID with zero string trace
  in the binary; `perk_sprintMultiplier` has exactly one reference (its own
  registration) — nothing native reads it, the scaling is entirely GSC-side. No
  clean native path exists without going through GSC itself.
- **HUD/UI + buy-station**: confirmed a single central HUD dispatcher
  (`CG_OwnerDraw`-equivalent, ~150 cases, sprint meter as the anchor). Buy-station
  reads `sp/survival_armories.csv` via a generic GSC `tablelookup` builtin, not a
  bespoke function — reusable by any future debug tool.
- **AI/vehicles**: civilian AI library confirmed genuinely shared across
  missions. No dedicated vehicle input path found — real evidence (zero vehicle
  binds, hint text rendered as static strings) suggests vehicles reuse the same
  `usercmd_t` fields this mod already hooks, meaning movement/look may already
  work in vehicle sections with no new code.
- **Physics/health**: mantle's real trigger found — it's the same `+gostand`
  command already used for standing up, contextually reinterpreted. **Task #20
  ("god mode")**: a real, unambiguous god-mode-shaped bit found
  (`entity+0x13c` bit `0x1` fully skips the health-decrement block) — untested
  live, but the native gating logic is clear.
- **MP (`iw5mp.exe`) foundational RE** — research only, no hooks/implementation,
  per CLAUDE.md's still-unresolved anti-cheat question. Confirmed the same core
  architecture holds (identical `usercmd_t` struct/offsets, same class of
  boot-time registration function, `d3d9.dll` import present) — not a different
  engine, just a different compile. One real structural difference flagged (a
  mouse-Y freelook-mode branch). Menu/zone-loading equivalent not located —
  genuine gap, not confirmed absent. No implementation should proceed from this
  without the anti-cheat question being resolved first.

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
  live; the first of two deliberate departures from real-engine-call-only input in the
  whole mod (the second being D-pad Left's squadmate call-in, issue #14), each to be
  replaced if a native call is ever found.
- **Sprint stamina/cooldown (issue #6 above):** the real native duration/timer function
  was never found (only the speed-scale consumer, with no timer logic, was traced) —
  implemented as our own 4s-deplete/2s-cooldown layer instead, using real MW3 values,
  with a fixed-duration cooldown timer (not a continuous-float threshold) after catching
  a live regen-flicker bug. Bypassed correctly when the real `player_sprintUnlimited`
  dvar is live-set by a mission. Confirmed working live. Extreme Conditioning's
  `perk_sprintMultiplier` override remains open (see issue #7 above).
