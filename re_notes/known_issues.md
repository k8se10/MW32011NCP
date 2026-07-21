# Known Issues — `iw5sp.exe` (Campaign/Survival)

Tracked as tasks in the working session; this file is the standalone reference so they
don't stay buried in `iw5sp.md`'s investigation log. Update status here as each is
resolved. Last updated 2026-07-19 *(corrected — this line said 2026-07-16 despite 21
more issues, #10-#31, having been added since then; the project's v0.2.0 Alpha release
lands this same day: Sprint's real kbutton migration and its stamina-layer removal,
issue #6; Predator Missile launch fixed and guidance-phase RE'd further, issues #29/
#30; a mortar/turret mission mis-attribution corrected, issues #26/#27)*.

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

**DECISIVELY re-confirmed dead, not just unchecked (2026-07-18, task #20
follow-up).** A full binary-wide constant scan (1,170,536 instructions —
not just handle xrefs like the original pass) for the exact
`monkeytoy` handle address returned **zero hits** anywhere in the
binary, via any addressing mode. The dvar's name string also has exactly
one reference total (its own registration). This is decisively confirmed
DEAD CODE, not a live restriction check this project could bypass by forcing
a value — closing this lead for good, not just parking it.
**Also searched for classic id-engine console/cheat-command strings**
(`"notarget"`, `"god"`, `"give"`, any `"con_"`-prefixed string) — all
absent from the binary (zero raw-byte matches), consistent with (but not
proof of) a fully string-stripped or fully absent console. **New,
partially-chased lead**: `"noclip"` IS a real string, referenced from
`FUN_00470d00` (the already-known ~600-string GSC notify-event-name
interning table, same mechanism `weapon_fired`/`damage` events use) —
meaning `"noclip"` is registered as a real GSC `notify()`/`waittill()`
event name, not a console command. Some cheat/debug-adjacent notify
survived in the retail build; its interned hash handle and real
consumer (who fires it, who listens) were NOT traced — worth a follow-up
if this angle is revisited, though **the working conclusion for task #20
is that a real developer console is not viable to unlock** — build a
custom debug menu or dedicated debug key-combos instead.

**God mode bit RE-CONFIRMED via fresh, full disassembly (2026-07-18,
task #20)** — a prior session's summary claim was independently
re-verified, not just trusted: `FUN_0045f770` (the real damage-
application function), line 103: `if ((*(byte*)(entity+0x13c) & 1) ==
0) { /* entire damage/health-decrement block, including the real
health-decrement at entity+0x150 */ }` — if bit `0x1` at `entity+0x13c`
is SET, this whole block is skipped and the function returns 0
immediately. **Confirmed, exact, disassembly-backed: setting
`entity+0x13c` bit `0x1` makes all damage to that entity a genuine
no-op.** Two other bits at the same offset are visible in this function
but not confirmed to the same depth: bit `0x2` appears to prevent a
single hit from being lethal (a "can't die from THIS hit" flag, not full
invulnerability — distinct from bit `0x1`), bit `0x20` is set once
health drops below 1, likely a "has died" latch. Ammo refill, wave-skip,
and killstreak-spawn's native pieces were NOT reached this pass —
genuinely open. For killstreak-spawn specifically, this session's own
killstreak research already fully traced `remote_missile`'s equip path
(`giveweapon("remote_missile_detonator")`, `1554.gsc`), the turret/
sentry weapon-change dispatcher (`_id_3CE8`/`_id_3CF5`, `1553.gsc`), and
the real native weapon-SET function `weapnext` uses (`FUN_0042d6b0`) —
more direct starting points than re-deriving from scratch.

**Much simpler god-mode candidate found as a side effect of a different
investigation (2026-07-18, Survival architecture survey)**: `179.gsc`'s
`_id_18D0()` (the real mission-failed handler) early-returns entirely if
`getdvarint("so_nofail")` is true (line 587) — a real, dvar-gated
"can't fail the mission" switch. This is a GSC-level dvar check, much
simpler to flip (via this project's own `GetDvarInt`/dvar-forcing machinery,
already used elsewhere for `player_sprintUnlimited`) than the
`entity+0x13c` bit-flag approach above, which requires writing to a
specific entity's memory each frame. **Not yet confirmed which is the
better real fix** — `so_nofail` may prevent MISSION FAILURE specifically
(e.g. Survival's overall "run ended" state) without necessarily stopping
individual damage/death feedback the way the entity-flag approach does,
or vice versa; worth live-testing both once implementation starts.

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
this project or a user added) has a directly-callable native dispatch, to potentially
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

## 6. Sprint stamina/cooldown — RESOLVED 2026-07-19: real kbutton makes the whole
    custom timer (and Extreme Conditioning) moot, removed entirely (2026-07-15, later session)

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

**Sprint's real kbutton — FOUND (2026-07-19), superseding `iw5sp.md`'s "PARKED, exhaustive
search came back negative" verdict.** All three prior live-memdiff-based techniques
genuinely came back negative (that conclusion stands, and remains a useful lesson: this
class of address just isn't reachable via heap correlation for this bind). The bind was
instead found via a completely different, purely STATIC technique requiring no live game
process at all: reconstructed `FUN_00438710`'s full real 77-entry jump table (base
`0x00438f48`, found via its own bounds-check `CMP EAX,0x4c; JA default`) by raw dword walk
rather than relying on the decompiler's partial switch recovery, and separately dumped the
real static 81-entry canonical bind-name table at `0x00929fa0` (the one `FUN_005330a0`
linearly scans — "index 1 = `+attack`"). **The table's index is identical to
`FUN_00438710`'s case number** — cross-validated four independent ways (index/case 1 =
`+attack`, 66/`0x42` = `weapnext`, 72/`0x48` = `togglecrouch`, 59-60/`0x3b-0x3c` =
`+toggleads_throw`/`-toggleads_throw`, matching ADS's already-confirmed `0xA98CB8`
exactly). This is the properly independent confirmation method the "Back regression"
lesson (issue #3) called for — the earlier mistake used the WRONG table (the 32-entry
`{name,-name}` pair table at `0092a014`, not case-ordered); this 81-entry table genuinely
is, four times over.

Index/case 61-62 (`0x3d`/`0x3e`) = `"+sprint"`/`"-sprint"` — a real, separate bind command
distinct from the default-bound `"+breath_sprint"` (index/case 9-10). Case `0x3d`'s raw
disassembly drives a dedicated kbutton_t at (per-player base)+`0xA98CCC`, the same
"special case, dedicated global" pattern as ADS's `0xA98CB8` (one kbutton_t struct away in
memory). **Independently cross-confirmed, not just table-adjacency speculation**: case 9
(`"+breath_sprint"` DOWN, the actual SHIFT-bound default) disassembles to two back-to-back
kbutton calls — one on `0xA98C04` (a new, previously-unidentified kbutton, very likely the
real Hold Breath kbutton for task #24) and a second on `0xA98CCC`, the *exact same*
address `"+sprint"` drives. The real, currently-shipped default Sprint/Hold-Breath key
press already drives this same kbutton on real hardware today.

**Implemented 2026-07-19**: `InjectControllerSprint()` now drives `0xA98CCC` via
`CallKbuttonDown`/`CallKbuttonUp` (same convention as ADS/Reload/Fire), gated on
`IsSprintActive()`'s full logical state (so a real `KeyUp` fires the instant stamina
empties mid-hold, not just on physical release) rather than the raw pm_flags-bit-forcing
approach. The old mechanism (`InjectControllerSprintPmFlags`/`ReassertSprintPmFlags`,
hooks on `FUN_00644ed0`/`FUN_00643ce0`) was removed entirely, not just disabled — full
replace, same precedent as Fire's migration (issue #29) and the crouch/prone migration.
Builds clean (0 warnings/0 errors, full rebuild).
**Hold Breath's own kbutton (`0xA98C04`) is a new, ready-to-use lead for task #24**, not
yet wired up — implementing it would mean also driving `0xA98C04` alongside `0xA98CCC`
when ADS'd with a scoped weapon, mirroring exactly what `"+breath_sprint"`'s real case 9
already does unconditionally (the game itself must gate the actual sway-reduction EFFECT
elsewhere, since this dispatch case fires both calls with no visible context branch).

**LIVE-CONFIRMED WORKING (2026-07-19), and it closes this entire issue's "still open"
line above for free.** User's direct report after playtesting: "this fixes multiple
issues, having native sprint means no workaround needed for stamina and regen as its
embedded naturally by the engine[,] same for extreme conditioning[,] fixed by this
100%." Driving the real kbutton means the engine's own native sprint duration/recovery
timer — the one whose native location was searched for and never found earlier in this
same issue (`FUN_00643870` traced fully, confirmed pure speed-math with no timer logic)
— now engages automatically simply because the real kbutton is being called, the same
way it would for a real keyboard press. This also means Extreme Conditioning's real
override (`perk_sprintMultiplier`/`specialty_longersprint`, discussed above, whose
detection mechanism was never found) is now a moot problem: there's nothing left to
detect or apply, since the native perk system already adjusts the real timer this
kbutton now engages, the exact same way it does for keyboard players.

**As a direct, immediate consequence, this project's entire custom stamina/cooldown timer
layer — everything described above in this issue from "Implemented as our own timer
layer instead" onward — was removed in the same pass, not left in place alongside the
new kbutton:** `g_sprintStamina`/`g_sprintWinded`/`g_sprintCooldownRemaining`/
`g_sprintLastTickMs`, the `player_sprintUnlimited`-dvar bypass (redundant now — the real
kbutton already respects that dvar natively, the same way real keyboard sprint always
did), the `[Sprint]` config section (`MaxStaminaSeconds`/`RegenSeconds`), a same-day
`[Experimental] SprintStaminaBypassForTesting` toggle (added to isolate this exact
change for testing, then removed minutes later once testing confirmed the whole timer
it bypassed was itself obsolete), and the `GetRealSprintValue`/`LogSprintDiag`
diagnostic code below (which had been investigating whether a real native timer even
existed) are all gone. `IsSprintActive()` is now just `g_sprintHeld && GetRealStance()
== 0`. Full rebuild verified clean (0 warnings/0 errors) after the removal. See
`PATCHNOTES.md`'s 2026-07-19 entries for the user-facing summary.

**Separately found and fixed while investigating:** `Controller_DeltaTimeSeconds()`
(used for look) turned out to use a single **process-wide shared** static timer, not
one per call site despite its own doc comment claiming otherwise — a second caller in
the same per-frame tick would starve whichever call runs second to a near-zero delta
every frame (confirmed via reasoning during this investigation, before it could cause a
live bug). Sprint's stamina timer used its own independent `GetTickCount()`-based clock
instead to avoid this exact trap *(historical note — that timer no longer exists as of
2026-07-19, see above; this paragraph documents the original investigation, not current
code)*; the header comment was corrected to warn future callers away from the shared-timer trap regardless.

---

## 7. Remaining unassigned controller inputs

**Status:** Open, tracked as task #5 (Back, deprioritized), #7 (killstreaks, not yet
scoped), and #9 (sprint's Extreme Conditioning override).

| Input | Intended action | Blocker |
|---|---|---|
| Back | `+scores` (scoreboard/objectives) | **Key-synthesis workaround implemented 2026-07-17** (Back hold → real `WM_KEYDOWN`/`WM_KEYUP` for TAB, the confirmed real bind `bind TAB "+scores"` from `players2/config.cfg`) — third narrow exception to the no-OS-input-emulation rule, same pattern as ready-up (F5) and D-pad Left's squadmate call-in ('4'). **Live-tested in Campaign: no visible effect at all** — no scoreboard, no objectives overlay. Real cause not yet diagnosed; leading theory is `+scores`/scoreboard is fundamentally an MP concept that's a no-op in SP, and SP's actual "mission objectives" display (if player-triggerable at all) uses a completely different, still-unidentified mechanism — not necessarily the same feature CLAUDE.md's console-behavior description assumed. User explicitly parked this as a known UI gap to fill later ("these are both UI gaps we will fill as part of the improvements side of the mod"), not urgent. The synthesis code itself is harmless (real key event, just currently produces no observable result) and stays in the build. |
| Killstreaks | Predator missile confirmed partially working; needs per-killstreak investigation | **(2026-07-17, full GSC trace done, see issue #26)** `remote_missile` fully traced — real fire (`+attack`) and abort binds confirmed; leading hypothesis is raw-usercmd-bit Fire not reliably triggering the `notifyonplayercommand` the launch is gated on. `precision_airstrike` uses a placement/marker system, not a camera takeover. Turret and squadmate (`friendly_support_delta`/`riotshield`) call-ins are CONFIRMED separate script systems (correction to this row's own earlier framing) — squadmate bug's divergence point narrowed to unresolved function `_id_061C::_id_3DE2`. **(2026-07-17, implemented, see issue #29)** Fire (RT) rewired off the raw usercmd bit onto the real `+attack` kbutton (`0x00A98C00`), same `CallKbuttonDown`/`CallKbuttonUp` mechanism ADS/Reload already use — directly tests the notify-dispatch hypothesis above. Built clean; **not yet live-confirmed** for either regular gunfire (regression risk — highest-blast-radius input in the project) or the Predator Missile launch itself. |
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
semantics already implement this project's entire B-button stance ladder natively
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

**Practical guidance:** with this project installed, keyboard/mouse play may exhibit
input weirdness that doesn't exist in a vanilla install — not because k+m support is
being actively removed, but because it is no longer receiving the same level of
verification attention as controller input going forward. Any further native input
work in this project prioritizes controller correctness first; keyboard/mouse is
tested opportunistically (e.g. "does it still work" spot-checks), not to the same
live-reproduction bar controller features get. **Recommendation: treat controller as
the primary, actively-verified input method with this project installed; keep a
keyboard within reach and expect to fall back to it if something feels off**, per
the project's own README/release notes.

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

**Not a project bug — a dev-only diagnostic tool (`tools/regbreak/`, never part of the
shipped project) that caused a real live crash during use.** Built to automate
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
  project installed — rules out any global regression; this is specific to the
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

**RECONCILED (2026-07-18): this entry's original claim stands — squadmate
call-in DOES work via the D-pad Left `'4'` key-synthesis.** User
confirmed directly from real play: squadmates worked because of the
emulated `'4'` press, same as this entry always said. The later doubt
(issue #26's "still open, narrowed to `_id_061C::_id_3DE2`") was
misplaced — likely that later research session re-litigated an already-
solved bug based on task #13's stale tracked status rather than
re-verifying against actual play. **Genuinely useful side-effect for the
Predator Missile investigation (issue #29)**: `friendly_support_called`
uses the BARE/GLOBAL `notifyoncommand` builtin (per issue #31's two-
builtin-family finding), and it demonstrably DOES fire correctly from
this project's synthetic `'4'` keypress. This is real evidence that
`notifyoncommand` (bare/global) IS reachable via synthetic/real-feeling
input — the specific problem for Predator Missile is narrower than "no
synthetic input reaches any notify builtin at all": it's specific to
`notifyonplayercommand` (the entity/player-SCOPED variant), which may
have an additional requirement the bare variant doesn't (e.g. the
specific entity-registration target actually matching what our kbutton
call sets up) — worth folding into the ongoing bytecode/native-dispatch
investigation.

**Bonus fix, same mechanism: turret could not be un-toggled once deployed.** Live-
reported earlier the same session (pulling out a turret via D-pad Left left no way to
put it away again) and initially parked as a separate investigation pending native-path
work on this slot, since the input mechanism was mid-change. **Turns out to be the same
root cause, and genuinely a bug in the OLD implementation, not real console/native
behavior**: the real native toggle for `+actionslot4` is a plain press-to-toggle (press
once to deploy, press again to put away) — but the OLD direct `FUN_00410ad0`/
`FUN_0044ec40` call pair only ever drove the "deploy" side correctly and had no working
toggle-off path, so turret truly was partially broken before, not just missing a
"native limitation" the project correctly reproduced. The key-synthesis fix above resolves
this too, for free, since the synthesized keypress goes through the real dispatcher's
own toggle logic exactly like a real key press would. **CONFIRMED WORKING LIVE by the
user** (2026-07-16): turret can now be deployed and put away with repeated D-pad Left
presses. No further work needed on this specific report; task tracker entry closed.

---

## 15. Aim assist entity classification — PERMANENTLY REMOVED (2026-07-20), superseding the "parked" status below

**Status: CLOSED, feature deleted.** Task #16. Following issue #33's VAC/anti-cheat
research, aim assist has been permanently removed from the codebase entirely —
`analog_input_hooks.cpp`'s implementation, its call site in
`InjectControllerLookAngles`, and `mod_config.h`/`.cpp`'s entire `[AimAssist]`
config section are all deleted, not just left disabled. Reasoning: reading live
entity/target data out of process memory to adjust aim is mechanically identical
to a soft-aimbot regardless of intent, and issue #33's research found the closest
real precedent for a proxy-DLL project that manipulates gameplay state beyond
pure input remapping (ENB, vs. ReShade's clean visual-only record) has actual
documented ban history. Unlike this project's core input-remapping work (writes
real values into real input structures, never reads gameplay-entity memory),
there was no way to make this feature safer without changing what it
fundamentally is — cut entirely rather than reworked. See `PATCHNOTES.md`'s
v0.2.2 entry for the release-facing summary and the user-facing security notice
added to `README.md` for versions v0.2.1 and earlier.

**The rest of this entry is kept as historical record of the real RE work done
before the removal decision — the technical findings below are still accurate
and could inform a future project that wants this class of feature with a
different risk tolerance, but are no longer being pursued here.**

**Prior status (2026-07-17, superseded above):** Open, tracked as task #16. **Aim
assist is completely non-functional at this stage** — not just unpolished,
genuinely broken targeting behavior — and is disabled in the shipped config
(`[AimAssist] Enabled=0`). Must stay disabled for any public/release build until
this is resolved; do not re-enable outside active development/testing.

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
needed on the project's own side. This is the real, general mechanism — not a per-menu
special case the project has to maintain.

**Final implementation:** Up = `0x9a`, Down = `0x9b`, Left = `0x9c`, Right = `0x9d`,
A = `0xd` (Enter), all via `ForwardKeyToMenu`. D-pad's normal gameplay actionslot
dispatch (`InjectControllerDpad`) and A's normal Jump bit (`InjectControllerButtons`)
are both suppressed while `IsMenuActive()` so D-pad/A can't mean two things at once
— same dual-purpose-button pattern already established for B (ESC-forward vs
crouch/prone). **Confirmed working live** by the user across the main menu, pause
menu, and the `pc_options_video`-style two-pane settings screens.

**Corrections (2026-07-18), both confirmed live by the user:**
- **Buy-station/armory `itemDef` navigation (Survival): CONFIRMED WORKING,
  100%** — the generic Group A/B dispatch does apply identically, as
  predicted. The "not yet separately live-verified" caveat below is
  retracted for this item.
- **Slider-type settings VALUE adjustment (not just navigation to/from):
  CONFIRMED WORKING via Left/Right** — this section's own original claim
  (below, kept for the record) that the only found value-adjust path
  (`FUN_00625510`) is gated on mouse-wheel-shaped keycodes and that
  Left/Right can't reach it is WRONG, or at least incomplete: it was based
  on finding one native function via decompile without checking whether
  the `.menu` files themselves define their own `execKeyInt`-style Left/
  Right handler directly on slider `itemDef`s — the same mechanism already
  confirmed (a few paragraphs above) to handle the options screen's pane
  drill-in/drill-out entirely at the `.menu`-script level, not through any
  native function at all. Since `InjectControllerMenuNav`'s Left/Right
  already forwards generically to whatever handler the currently-focused
  item defines (`ForwardKeyToMenu`), slider value adjustment was very
  likely already working for free the whole time this was marked
  "unsolved" — never independently re-verified against the `.menu` file's
  actual slider `itemDef` before concluding it needed new work.
  **Methodology lesson**: don't conclude a native decompile search is
  exhaustive for "how does X get handled" in this menu system — the
  `.menu` script layer has its own handler mechanism that can fully bypass
  native code, as this project has now found twice (pane drilling, and
  this).

**Original text (2026-07-17), kept for the record — see corrections
above, not accurate as originally written:** buy-station/armory `itemDef`s
(Survival) haven't been separately live-verified with this exact
mechanism, though the same generic Group A/B dispatch should apply
identically (they're plain single-pane vertical lists, the simpler case
already confirmed via the pause menu). Slider-type settings items (`type
10`, e.g. `dvarFloat "sensitivity" 5 1 30`) have an empty `action{}` block
and their only found value-adjust path (`FUN_00625510`) is gated on
mouse-wheel-shaped keycodes (`200`/`0xc9`/`0xca`), not the Left/Right codes
used for pane-drilling — adjusting a slider's actual VALUE (not just
navigating to/from it) with a controller remains unsolved and is a natural
next step.

**Font-struct diagnostic ADDED 2026-07-19, safe by construction, live-test pending**
(task #6/#31/#32 follow-up): after the boot-splice crash below, a fresh 6-fork
research pass (Font struct layout, render-time glyph lookup, live texture patching,
docs consolidation, interact-prompt resolver design, menu-architecture deep dive)
converged on a completely different, much lower-risk mechanism for adding a glyph:
patch the real `fonts/bigFont` `Font` struct's glyph array IN MEMORY after it has
already loaded normally, rather than intercepting the boot-time zone queue at all.
Before attempting that patch, `InjectFontStructDebugTest()`
(`analog_input_hooks.cpp`) was added as a read-only verification step: calls the
real `FindOrLoadFont("fonts/bigfont")` (`0x0045d040`) from the always-safe
WndProc/`SetTimer` tick (gated on the same obscure LB+RB-held-2s combo as the
disabled zoneload test), which returns the SAME cached `Font*` the real boot
process already created (asset-interned by name, not reloaded), then logs every
confirmed struct field (`pixelHeight` +0x4, `glyphCount` +0x8, `material`/
`glowMaterial` +0xC/+0x10, `glyphs` +0x14) plus a handful of real glyph entries
(direct-indexed 'A'/'E' and the first two sorted-extra entries past the required
96) to `proxy_d3d9.log`. Zero mutation, zero hooking of anything boot-related --
deliberately the safest possible next increment given this session's two crashes.
Builds clean; **live test still needed** (hold LB+RB 2s, then check the log) to
confirm the Ghidra-derived struct layout against real memory before ever writing
to it. See the Font/Glyph struct layout this compares against in `ui_assets.md`'s
2026-07-19 fork-research section.

**Live-CONFIRMED via the diagnostic above (2026-07-19)**: struct layout matches
exactly against real memory (`glyphCount=191`, direct-indexed 'A'/'E' letters
correct, sorted-extra tail ascending as expected) — the Ghidra-derived Font/Glyph
layout is proven correct, not just theorized.

**REAL glyph-array patch mechanism test ADDED, mechanism not yet confirmed
visually** (`InjectFontGlyphPatchTest`, LB+RB+A combo): grows the real font's
glyph array by one entry (heap-allocated replacement array, inserted in sorted
position) and repoints `glyphCount`/`glyphs`, using a BORROWED existing glyph's
UV rect ('A') as a placeholder — proves the reallocate-and-repoint mechanism
without yet needing real new pixel content. Builds clean; not yet triggered live.

**Attempted a REAL, complete font-extension mechanism, CAUGHT AND DISABLED before
ever being live-tested (2026-07-19)** — rebuilt `bigfont_ext.ff` under entirely
unique asset names (`bigfont_glyph_ext.ff`: font `fonts/bigfont_ext`, materials
`fonts/gamefonts_pc_ext`/`fonts/gamefonts_pc_glow_ext`, image `gamefonts_pc_ext`)
to avoid the same-name asset-interning collision that blocked both the boot-splice
attempt and the earlier menu-override work — confirmed via `Unlinker.exe --list`
that the original build used the SAME names as the real stock assets, which would
have silently no-op'd (interning hands back the existing cached object for an
already-registered name). Rebuild is clean (0 warnings/0 errors) and deployed to
`assets/zones/bigfont_glyph_ext.ff`. Implemented `InstallGlyphFontExtension()`:
loads this uniquely-named zone via a direct (non-hooked) `LoadZones` call from the
WndProc/`SetTimer` tick — the same safe mechanism the original `roundtrip.ff` test
already proved (screenshot-verified, zero crash) — then repoints the REAL
`fonts/bigfont`'s `material`/`glowMaterial`/`glyphCount`/`glyphs` fields at the
loaded extended font's own already-correct values (no runtime array construction
needed, since the offline-built font is already a complete, correct superset).
**Before ever shipping this live, caught a direct contradiction with
already-established research**: `bigfont_glyph_ext.ff` contains 2 materials + an
image + shaders, and loading MATERIAL-bearing content (not a bare menu) from this
same WndProc/`SetTimer` tick was already found (see `iw5sp.md`'s "Black-screen
flash... root cause fully resolved: materials" section) to trigger a synchronous
D3D9 GPU-resource-creation cascade outside the engine's own controlled
frame/thread discipline — exactly the class of operation already confirmed to
cause visible corruption/crashing from this exact call site. **Disabled
(`InstallGlyphFontExtension()`'s call site commented out, code kept) before the
user ever tested it** — the physical zone file was also removed from the live
`zone/english/` folder to fully revert. The only path that research found safe
for material-bearing content is routing the load through a real
`FUN_0053cbc0`-driven level-load transition instead of a WndProc/`SetTimer` tick
call — not yet attempted for this or any content in this project. **Do not
re-enable without either finding that safe path or independently re-verifying the
timing risk doesn't apply here.**

**Boot-splice half IMPLEMENTED 2026-07-19, then CONFIRMED LIVE CRASH, DISABLED
same day** (task #31): `Hook_LoadZonesForBootSplice` (`analog_input_hooks.cpp`)
hooks `FUN_004ca310` and splices the extended `bigfont_ext` font zone into the
real boot-time zone queue, gated on an exact return-address match so only one
specific real call site is meant to be touched — see `ui_assets.md`'s "Boot-zone
splice" sections for the full plan. Built clean, `bigfont_ext.ff` copied into the
live `zone/english/` folder, but **the actual live boot crashed** with this hook
active. `proxy_d3d9.log` shows the exact same crash signature as the 2026-07-18
rumble-hook crash: every hook (including this one) reports successful install
(all MH_OK), then an immediate detach with ZERO gameplay-tick activity ever
logged (no `[stance-diag]` heartbeat at all) — the crash happens during early
boot, before the first gameplay frame. Critically, this hook's own
`"[boot-zone-splice] spliced..."` log line never appears anywhere in the log,
meaning the return-address-gated splice branch itself never actually ran before
the crash — either the crash happens before `FUN_00679680`'s Call 2 executes, or
merely hooking `FUN_004ca310` at all (even the plain-passthrough branch every
OTHER real caller takes) is unsafe in a way the static disassembly review didn't
catch. **Disabled** (hook install commented out, code kept — same precedent as
the rumble hook) and `bigfont_ext.ff` removed from the live `zone/english/`
folder to fully revert to last-known-good. Hold Breath (added the same session)
is untouched and not suspected — it only executes once gameplay ticks are
already running, which this log shows never happened. **Root cause not yet
found** — needs a lower-risk diagnostic pass (e.g. logging every call to this
function unconditionally, including its return address, BEFORE attempting any
write, so the next test run shows whether the hook is even being reached safely
at all, let alone hitting the intended call site) before re-attempting. The
bind-resolver hook (`FUN_0061f6f0`) is unaffected and still entirely unstarted.

**ROOT CAUSE FOUND, definitively (2026-07-20, research fork) — using existing
disassembly dumps already on disk (`D:\Tools\ghidra_projects_bootzone\`), no
fresh Ghidra pass needed.** `FUN_004ca310` is **not a real function at all** —
it's a 7-byte incremental-link/thunk stub: `CALL 0x00463430; JMP EAX`. The
"trivially trampolineable 2-instruction tail-dispatch veneer" characterization
in the original plan (see the "UPDATE (2026-07-18...)" entry above, under
"## 23. Real controller options menu") is wrong in a critical way it didn't
anticipate: `FUN_00463430` (the real CALL target) is **return-address-
sensitive** — its own decompile shows the actual return address on the stack
(`unaff_retaddr`) used directly as an input to a relocation-offset
computation (`local_8 = unaff_retaddr - iVar1`), feeding a 134-iteration
fixup/relocation-table walk (`FUN_006cc460`) that ultimately computes the
real target address `FUN_0045e910(...)` returns, which `JMP EAX` then jumps
to. **This is the actual crash mechanism**: MinHook's standard inline hook
overwrites the thunk's 5-byte `CALL` with a detour jump, then relocates and
re-executes that original `CALL` from freshly-allocated trampoline memory
when the hook calls through to "the original function." That relocated
`CALL` pushes a return address pointing INSIDE the trampoline, not the real
`0x004ca315` — corrupting `FUN_00463430`'s relocation math and producing a
garbage computed address, which `JMP EAX` then jumps straight to. This
happens *inside* the trampoline call itself, before the hook's own detour
logic (the return-address caller-match check, the splice log line) ever gets
a chance to run — which is exactly why the `"[boot-zone-splice] spliced..."`
log line never printed, and why this would crash regardless of which of
`FUN_004ca310`'s 4 real callers triggered it, not just the intended one.
**The lower-risk "log every call, don't write yet" diagnostic this entry
called for would NOT have caught this** — the crash happens on the
trampoline call-through itself, before any of this project's own detour code
(logging included) executes.

**The other 2 of `FUN_004ca310`'s 4 real callers, not previously
characterized**: `FUN_0067a690` (engine shutdown/teardown — destroys
windows, releases COM-style interface pointers via vtable `Release()` calls,
then calls the thunk with a fixed `{0,0,6}` entry, likely "unload all zones
on exit") and `FUN_00481e50` (a trivial wrapper, single fixed `{0,0,4}`
entry, purpose not determined this pass — possibly a restart/reload path).

**Safer approach, concrete**: don't hook `FUN_004ca310` at all — it's
fundamentally unsafe to trampoline regardless of caller-matching, for ANY
purpose (font zone, menu zone, or otherwise). Two better options: (1) hook
`FUN_00679680` itself instead (a real, normal function with an ordinary
prologue, confirmed safely trampolineable) at its own call site, to
intercept/append zone-queue entries before IT calls the unsafe thunk; or
(2) one-time-debugger the real resolved target address out of `EAX` at
`004ca315` (what the thunk actually jumps to after its relocation math) and
hook THAT real function directly instead of the thunk. **A follow-up
research fork (same day) evaluated option (1) in more depth for the real
controller-options-menu work — see issue #23 below for the refined,
implementation-ready plan.**

**REFINED, implementation-ready (2026-07-20, follow-up research fork) —
the exact mechanism is more specific than "return-address-sensitive," and
this changes the recommended fix.** Decompiled `FUN_00679680` cleanly via
headless Ghidra (existing `MW3.gpr` project). `FUN_004ca310` is a textbook
**MSVC incremental-link thunk (ILT) that self-patches its own caller's call
site**: `FUN_00463430` resolves the real function's absolute address
(`&DAT_008501e8 + DAT_008501e8`, an RVA-recovery pattern), computes a
relative displacement from the actual return address on the stack, then
writes that 4-byte displacement directly into the 5-byte `CALL`
instruction sitting just before the return address — i.e. it rewrites the
CALLER's own `CALL 0x004ca310` into `CALL <real_function>` in place, so
future executions of that exact call site skip the thunk entirely
(standard ILT behavior for faster incremental rebuilds). **This is exactly
why hooking it crashes**: when MinHook's trampoline calls the thunk, the
return address points into trampoline-allocated memory, not a real 5-byte
`CALL` site — the thunk then "patches" 5 bytes of essentially arbitrary
memory. This risk applies to ANY new call site, not just a MinHook
trampoline — a plain wrapper function calling the thunk directly would
corrupt memory the same way.

**Recommendation, refined to a safer variant of option (2) above**: don't
touch the thunk OR any of its call sites at all. By the time this
project's own hooks install, the engine's own natural boot sequence has
already called the thunk from its real call sites at least once, which
means `DAT_008501e8` (the RVA-recovery data slot) is already resolved and
stable. Plan: hook `FUN_00679680` itself (confirmed safe — ordinary
prologue, no thunk involved), let the ORIGINAL run completely unmodified
via the trampoline (its own internal thunk calls execute exactly as the
game intends, safely, since nothing about THAT invocation changes), then
AFTER it returns, in this project's own detour code, read
`&DAT_008501e8 + *(int*)&DAT_008501e8` directly to get the already-resolved
real function address, and call THAT function directly with an extra
`{ourZoneName, type, 0}` entry appended — a plain call to an
already-patched, non-thunk function, carrying none of the self-modification
risk. **One live check needed before implementing** (no game-state risk):
confirm `DAT_008501e8` is non-zero/stable by the time `d3d9.dll`'s hooks
install — a single log-and-read.

**Zone-queue entry format corrected**: `{zoneNamePtr, type, 0}` where
`type` is `0`, `1`, or `2` (a zone-category tag: 0=core, 1=optional,
2=procedurally-named) — NOT `{name, 4, 0}` as the original 2026-07-18 plan
assumed; that guess was wrong, confirmed via `FUN_00679680`'s actual
decompiled body, which typically leaves 2-5 of its 10 array slots unused.

**Diagnostic IMPLEMENTED 2026-07-20, correction found to the `DAT_008501e8`
formula above, not yet live-tested.** Before writing the real splice, confirmed
`FUN_00679680`'s own real prologue/epilogue directly via the cached disassembly
already on disk (`D:\Tools\ghidra_projects_bootzone\disasm_00679680.txt`, the
same project the ROOT CAUSE research above used): `SUB ESP,0x78; PUSH EBX; PUSH
EBP; PUSH ESI` at entry, plain `RET` at exit, zero stack-args read anywhere —
a genuine `void __cdecl(void)`, confirmed safely trampolineable, exactly as
the plan above assumed.

**Real correction, found while implementing, not re-guessed**: re-read the
cached decompile of `FUN_00463430` itself
(`D:\Tools\ghidra_projects_bootzone\decomp_463430.txt`) before trusting the
`&DAT_008501e8 + *(int*)&DAT_008501e8` formula above. That expression
(`iVar1` in the decompile) is only an INTERMEDIATE value — it feeds a
134-iteration relocation walk (`FUN_006cc460`) and a further resolver call
(`FUN_0045e910`); the value `FUN_00463430` actually returns (and that `JMP
EAX` jumps to) is `iVar2 + iVar3`, where `iVar2 = FUN_0045e910(...)`'s return
and `iVar3 = iVar1 - imageBase`. **The plan's formula does NOT equal the real
resolved `LoadZones` address** — it's a misleading intermediate, and
reimplementing `FUN_00463430`'s full relocation chain to get the true value
would be substantial, fragile work unwarranted for a read-only diagnostic.

**Simpler, more direct diagnostic implemented instead**, using the ILT
self-patch behavior described in the ROOT CAUSE section directly ("rewrites
the CALLER's own `CALL 0x004ca310` into `CALL <real_function>` in place, so
future executions of that exact call site skip the thunk entirely"):
`FUN_00679680`'s own Call 2 (`0x006797bd`, return address `0x006797c2` — both
already independently confirmed and reused elsewhere in this codebase, e.g.
`kBootZoneSpliceReturnAddr`) IS that exact call site. `Hook_FUN_00679680`
(`analog_input_hooks.cpp`) hooks `FUN_00679680`, calls the real trampoline
completely unmodified (so this exact call executes under totally normal
conditions — nothing about the hook alters it), then reads the raw 5 bytes at
`0x006797bd` directly. If MSVC's ILT self-patched it (as theorized), those
bytes decode to `CALL <the true resolved LoadZones address>` instead of `CALL
0x004ca310` — giving the real target directly, without reimplementing any of
`FUN_00463430`'s internal math. Both readings (the original DAT_008501e8
formula, clearly labeled as not-the-real-target, and this call-site decode,
labeled as the trustworthy one) are logged to `proxy_d3d9.log` for
comparison. **Deliberately scoped to logging only** — does not touch the
zone array, does not call the resolved address, does not construct or append
a zone-queue entry, per this project's own "log before you ever mutate"
discipline (the lesson both the rumble-hook crash, issue #24, and the
original boot-splice crash above already taught). Builds clean (0
warnings/0 errors, full rebuild). **Not yet live-tested** — the next real
game launch's `proxy_d3d9.log` is what will actually confirm or refute the
self-patch theory and reveal the real address. The actual splice-and-call
implementation (appending a `{ourZoneName, type, 0}` entry and calling the
resolved address directly) is still unstarted, deliberately left for a
follow-up pass once this diagnostic's reading is confirmed live.

**LIVE-TESTED (2026-07-21), self-patch theory REFUTED at this call site.**
Full user playtest, `MH_OK` on both create+enable, and the whole rest of the
session ran completely normally afterward (no boot regression, no gameplay
disruption — same clean signature as every other confirmed-safe read-only
diagnostic in this file). The actual reading:
```
[boot-thunk-diag] DAT_008501e8 raw=0xFFBAFE18, &DAT_008501e8+val=0x00400000 (NOTE: ... only an intermediate ...)
[boot-thunk-diag] call site 0x006797BD is CALL rel32, decoded target=0x004CA310 (thunk address for comparison: 0x004ca310)
[boot-thunk-diag] call site target is UNCHANGED (still points at the thunk)
```
The decoded target at `0x006797BD` is still `0x004CA310` — i.e. still the thunk itself,
not a self-patched real address. **The ILT self-patch theory does not hold for this
specific call site** (at least not by the time this hook reads it, on this run) — this
is a genuine negative result, not an implementation bug (the hook fired, read, and
logged exactly as designed). Reproduced identically across two separate launches this
session, so it's not a one-off fluke. **Implication for the actual splice work**:
reading a self-patched call site is not a viable way to recover the real `LoadZones`
address, at least not via this specific call site — the next real step needs either
(a) checking whether `FUN_0067a690`/`FUN_00481e50`'s OWN call sites to the thunk
self-patch instead (each patches independently per the ROOT CAUSE research), or (b) a
different address-recovery approach entirely (e.g. one-time-debugging the resolved
value directly out of `FUN_00463430`'s `JMP EAX` at a real breakpoint, per the
original option (2) in the ROOT CAUSE section above). The real splice-and-call
implementation remains unstarted.

**SOLVED DIFFERENTLY (2026-07-21) — the address-recovery problem above didn't need
solving at all.** Re-read this project's own `InstallGlyphFontExtension()` (defined
earlier in `analog_input_hooks.cpp`, disabled since 2026-07-19) closely: it already
calls `FUN_004ca310` (`LoadZones`) **directly, un-hooked**, via the `LoadZones`
function pointer at the top of this file — and the "roundtrip.ff"/zoneload-test
precedent already screenshot-confirmed this exact call pattern is safe, repeatedly
("FUN_004ca310 returned without crashing," dozens of times in `proxy_d3d9.log`).
**Calling the thunk directly was never the problem — only HOOKING it is** (that's
what corrupts its self-relocation math, per the ROOT CAUSE section above).
`InstallGlyphFontExtension()` is disabled not because its `LoadZones` call is
unsafe, but because it's wired to fire from the WndProc/`SetTimer` tick — the wrong
TIMING for material-bearing content (confirmed root cause: `iw5sp.md`'s
"Black-screen flash... materials" section). This entry's own text already named
the fix a session ago: "the only path... confirmed safe for material-bearing
content is routing the load through a real `FUN_0053cbc0`-driven level-load
transition instead" — that line was correct, and this pass acts on it directly
instead of continuing down the address-recovery path.

**`FUN_0053cbc0` confirmed real and safe to hook, via fresh Ghidra disassembly
(not assumed):**
```
0053cbc0  SUB ESP,0xc8
0053cbc6  PUSH EBX
0053cbc7  PUSH EBP
0053cbc8  PUSH ESI
0053cbc9  PUSH EDI
0053cbca  MOV EDI,dword ptr [ESP + 0xdc]
...
0053ce82  CALL 0x004ca310
0053ce87  ADD ESP,0x38
0053ce8a  POP EDI
0053ce8b  POP ESI
0053ce8c  POP EBP
0053ce8d  POP EBX
0053ce8e  ADD ESP,0xc8
0053ce94  RET
```
A genuine, ordinary `__cdecl`-shaped function (`void FUN_0053cbc0(byte *param_1, int
param_2)`, `param_1` = a map/mission name string, confirmed via decompile — it
gates specialops/survival-specific patch-zone loads on `FUN_004d6d40`/
`FUN_00526b30` calls against that name), body spans `0053cbc0`-`0053ce94` (0x2D4
bytes) — nothing like `FUN_004ca310`'s literal 7-byte `CALL;JMP EAX` thunk stub,
plenty of room for MinHook's trampoline. Its own internal direct calls to
`FUN_004ca310` (confirmed via decompile: it calls the thunk directly, multiple
times, for `common_specialops`/`common_survival`/`patch_<mapname>` zones) sit well
past this function's overwritten entry bytes, so they keep their real,
un-relocated return addresses — the ILT self-patch mechanism inside the thunk's
target keeps working exactly as intended for all of them. This hook never touches
or hooks the thunk itself, only `FUN_0053cbc0`'s own outer entry/exit.

**Confirmed real call frequency, not assumed**: exactly ONE real call site
(xref search), inside `FUN_00447ea0` — the real per-level-load orchestrator
(decompile confirms `"map_restart"` command dispatch, `"Start Level Save"`
checkpoint handling, and a guarded `if (*param_1 != '\0') FUN_0053cbc0(param_1,
param_3);` call) — itself called from exactly one place. So this fires once per
real level load/restart/checkpoint-reload, not per-frame — the correct
low-frequency "safe timing window" this project's research already predicted.

**Implemented**: `Hook_FUN_0053cbc0` (`analog_input_hooks.cpp`) — MinHook hook,
calls the real trampoline completely unmodified first, then logs (every call, with
a running counter and the real map-name string, read defensively via the same
SEH-wrapped bounded-copy pattern already established for `BindResolverLogAfterCall`)
that the hook fired. Wired live (uncommented) since it's pure logging. **The actual
splice call — `InstallGlyphFontExtension()`, already fully implemented and
idempotent — is included in the function body but left commented out/disabled by
default**, matching this codebase's own precedent for shipping an unverified,
mutating piece disabled until live-confirmed (e.g. `Hook_LoadZonesForBootSplice`).
This project has crashed live twice already from adjacent boot/zone-loading
mistakes; "confirmed via fresh disassembly" is not this project's bar for shipping
a mutating call live by default — an actual confirmed-safe live test is. Builds
clean (MSBuild, Win32/Release, 0 warnings/0 errors, full rebuild). **Not yet
live-tested** — next real level load (Campaign mission start, Survival wave
start/restart, or a checkpoint reload) should show `[level-load-zone-hook]
FUN_0053cbc0 returned (call #N)...` in `proxy_d3d9.log` with the correct map name
and no boot/gameplay regression. Once that's confirmed clean across at least one
real level load, uncommenting the `InstallGlyphFontExtension()` call is the next
step — that call itself needs its own separate live confirmation before this is
considered done end-to-end.

---

## 23. Real controller options menu — native zone/menu injection, blocked on a real architectural limit (2026-07-17)

**Status:** Open, in progress. Task #23. Full technical trail in `iw5sp.md`'s "Real
controller options menu" section — this is a summary. **The blocker below is now
resolved to an implementation-ready plan — see the "REFINED, implementation-ready
(2026-07-20...)" entry near the end of the "## 22..." boot-splice discussion further
up this file (search for `FUN_00679680`) for the concrete, corrected injection —
and its own follow-up "Diagnostic IMPLEMENTED 2026-07-20" entry right below it for
the read-only `Hook_FUN_00679680` diagnostic now built and awaiting a live-test
before the actual splice is attempted.**
approach and zone-queue entry format.**

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

**UPDATE (2026-07-18, dedicated deep-dive pass): the level-load-transition
alternative is confirmed structurally sound, and its next blocker has a
cleaner solution than previously scoped.** Full detail:

- **Confirmed real, viable mechanism**: `FUN_004ca310` (`LoadZones`) has
  exactly 4 real callers; two are useful injection points —
  `FUN_0053cbc0`'s per-level-load zone queue (2 of 5 array slots unused)
  and `FUN_00679680`'s one-time BOOT-TIME zone queue (run before any
  level loads, right after `Direct3DCreate9`/`ShowWindow`, ~4 of 10
  array slots unused). Plan: hook `FUN_004ca310` itself (its whole body
  is a trivially-trampolineable 2-instruction tail-dispatch veneer), read
  the return address off the stack in the detour, and if it matches
  either known caller, splice in one extra `{ourZoneName, 4, 0}` entry
  before forwarding — any other caller passes through untouched. This
  fixes the ROOT CAUSE, not just relocates it: riding inside a real
  `LoadZones` call the engine itself issues puts our zone in the identical
  call stack/thread/timing context as `ui.ff` itself, which is exactly
  what makes GPU-resource creation safe there and unsafe in the live
  WndProc-hook injection path.
- **Confirmed: the real pause menu's own background material is loaded
  at BOOT TIME** (via `FUN_00679680`'s zone queue), long before Start is
  ever pressed — this is WHY opening it later via `OpenMenuByName` is
  safe (a plain cached-pointer dereference, not a fresh load). Directly
  confirms the "pre-load during a safe moment, open later via the
  separately-proven-safe `OpenMenuByName` path" workaround is exactly how
  the real game already does this for its own menus, not a novel idea.
- **Real, cleaner solution found for the next blocker** (previously
  scoped as "find and patch the specific call site that opens
  `pc_options_controls_ingame`"): decompiled `FUN_0056d4f0`/
  `FUN_004f1980` (large hardcoded client-command/UI-action string
  dispatchers) and `FUN_00619600` (`"openMenuOnDvar"`) — all funnel
  through `FUN_00544a50` (`OpenMenuByName`, already confirmed,
  `__cdecl(ctx, const char* menuName)`), including `FUN_00619600` passing
  `menuName` through as a genuine PARAMETER (not hardcoded) — meaning real
  menu-transition targets live as DATA inside compiled `.menu` files'
  item-action strings, not scattered across native disassembly call
  sites. **Recommendation: don't chase individual call sites at all —
  hook `OpenMenuByName` directly** (MinHook, same pattern as every other
  hook in this codebase) and do STRING-NAME SUBSTITUTION: when `menuName`
  matches the real target (e.g. `"pc_options_controls_ingame"`), swap in
  the unique custom menu's name before forwarding to the trampoline. This
  transparently catches every real call site — native-hardcoded or
  `.menu`-data-embedded — with one hook instead of needing to find and
  patch each one individually.
- **Not yet chased**: confirming the exact real dvar/menu-name string
  that opens `pc_options_controls_ingame` specifically (which parent
  Options menu item triggers it) — needs an OpenAssetTools dump of
  `ui.ff`'s real compiled `.menu` files (a fresh `Unlinker.exe` pass,
  not attempted in this Ghidra-only investigation).

**This is now a concrete, two-part implementation plan** (boot-time
zone-queue injection + `OpenMenuByName` string-substitution hook),
not an open architectural question — next step is implementation and
live testing, not more research, whenever this task is picked up.

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

## 24. Vibration/rumble — CONFIRMED LIVE: hooks break game startup entirely, DISABLED (2026-07-18)

**Status regression, same day as implementation.** Live-tested by the
user: with `Rumble_Install()`'s hooks active, **the game fails to start
at all.** `proxy_d3d9.log` showed every hook (including both rumble
hooks) installing successfully (`MH_CreateHook`/`MH_EnableHook` both
`MH_OK`) followed immediately by `proxy_d3d9 detach`, repeated
identically across multiple launch attempts, with **zero per-frame
activity ever logged in between** (no gameplay-tick heartbeat, no
stance-diag line — nothing this codebase's other hooks log routinely).
This means whatever crashes happens BEFORE the first real gameplay
frame, before any weapon could have been fired or damage taken — i.e.
before the rumble hooks' own trigger conditions could ever be reached.

**Leading hypothesis, not yet confirmed via disassembly**: `FUN_004895b0`
and `FUN_0044cdb0` are GENERAL native notify-dispatch functions (already
documented as such — used for `weapon_fired`/`damage` in this project's own
research, but the "general dispatcher" framing implies OTHER event types
route through the same two functions too, likely with genuinely
different real argument counts per event type, since a generic dispatch
function serving many unrelated notify events is a natural place for
that). This project's hooks declare a FIXED parameter signature for each
(3 args for the "simple" dispatcher, 12 for the "rich" one) confirmed
correct for exactly ONE real call site each (weapon-fire, damage) — if
ANY other real caller (plausibly firing during engine init, well before
any gameplay frame, matching the observed crash timing) invokes either
function with a genuinely different argument count, this project's
fixed-signature hook would read/forward incorrect stack data for that
call, which could corrupt behavior in ways a static single-call-site
disassembly check would never catch.

**Immediate mitigation, deployed**: `Rumble_Install()`'s call site in
`InstallAnalogInputHooks()` (`analog_input_hooks.cpp`) is commented out
— rebuilt, redeployed, **confirmed the game starts normally again with
just this one change**, isolating the two rumble hooks as the cause
(nothing else changed). All other hooks (movement, look, Fire's
kbutton+queue-push, D-pad, Sprint's L3-ADS fix, etc.) are unaffected and
still active. `Rumble_Tick()` itself is NOT commented out (still called
every gameplay frame from `InjectAllControllerInput`) — harmless as-is,
since with `Rumble_Install()` disabled nothing can ever call
`TriggerRumble()` to make `g_rumblePeakIntensity` non-zero, so `Rumble_Tick`'s
own first check makes it a no-op every frame.

**Doc-audit finding (2026-07-19), flagged not fixed (docs-only pass, no code
touched)**: `[Vibration] Enabled` still defaults to `true` in
`mod_config.h`/the generated INI, same bug CLASS this project already found
and fixed once for `[AimAssist] Enabled` (shipped `true` by default in an
earlier build — see the v0.1.2 patch note, "aim assist's config default was
true, not false — fresh installs shipped it enabled"). Currently harmless in
practice ONLY because `Rumble_Install()` is disabled at its call site, so the
flag has nothing to gate right now — but it's a landmine for whoever
reimplements this feature against the safer `FUN_0045e320`/health-poll
targets recommended above: if `Rumble_Install()` (or its replacement) is
re-enabled without ALSO flipping this default to `false`, fresh installs
would ship the not-yet-reproven feature turned on by default. Fix this
default alongside the reimplementation, not as a separate task.

**Recommended real fix, not yet attempted**: hook the NARROWER, specific
caller functions instead of the shared generic dispatcher —
`FUN_0045e320` (the per-shot fire-effects handler, already confirmed to
unconditionally call `FUN_004895b0(entity, "weapon_fired" handle, 1)`
internally) and `FUN_0045f770` (the damage-application function,
already confirmed to unconditionally call `FUN_0044cdb0` with the real
damage amount internally) — these are each a single, specific,
narrowly-scoped function rather than a shared dispatcher serving many
unrelated event types, so hooking them avoids the "some other caller
passes a different real argument count" risk entirely. **Their OWN
calling conventions/full parameter signatures were never independently
confirmed via disassembly** (only their INTERNAL calls into the generic
dispatcher were) — this needs the same disassembly-first rigor this
project already applies everywhere else (see the `FUN_00428a70`
calling-convention check, issue #29) before any new hook attempt, given
this exact class of mistake (trusting an assumed signature without full
verification) is very likely what just broke the game.

**UPDATE (2026-07-20, research fork): calling conventions confirmed, but
the "narrower/safer" premise itself is WRONG — this needs one more
investigation layer before attempting.** Both functions ARE called via
genuine stack args (confirmed: `FUN_0045e320`'s entry reads its first
parameter with `MOV EDI, dword ptr [ESP+0x480]`), so that part of the
plan holds. But neither is the single-call-site function this section
assumed:
- **`FUN_0045e320`**: exactly 1 real caller (`FUN_005b68c0`), but that
  caller is itself a large per-frame animation/event-notify dispatcher —
  a switch statement keyed on a notify-type code — and routes to
  `FUN_0045e320` from **8 different case values**
  (`0x24,0x25,0x29,0x2a,0x34,0x35,0x36,0x37`). No confirmation yet all 8
  mean "weapon fired" specifically; could be different fire-mode/
  animation-notify sub-events. Hooking it will very likely fire for more
  than just "a shot was fired."
- **`FUN_0045f770`**: **12 different real callers**
  (`FUN_005b5620`, `FUN_004ea2b0`, `FUN_005b5f70`, `FUN_005b68c0`,
  `FUN_005c6b10`, `FUN_005e2820`, `FUN_005c5f50`, `FUN_005c9a30`,
  `FUN_00445280`, `FUN_00654020`, `FUN_005dc700`, `FUN_005dc870`), one of
  which (`FUN_005b68c0`) hits it from two separate branches. Its own body
  internally calls `FUN_0044cdb0` three separate times with three
  different event-name data pointers, consistent with it being a general
  "apply value change + notify" utility (health/armor/ammo/etc.), not
  damage-specific.

**Net effect**: both candidates are still shared/multiplexed functions,
just one layer down from the original crash-causing generic dispatcher —
hooking either risks the exact same "some other real caller/case passes
different real data" bug class that broke the game the first time. Not
ready to implement. Needs: decompiling each of `FUN_0045e320`'s 8 case
branches' surrounding context in `FUN_005b68c0`, and auditing
`FUN_0045f770`'s 12 callers, before either is safe to hook. Full detail
(disassembly/decompile dumps, caller lists) at
`D:\Tools\ghidra_projects_rumblefork\`.

## Original implementation entry (2026-07-18), superseded by the above

**Status:** Task #17 implemented. Builds clean (0 warnings/0 errors, full rebuild
confirmed). **Not yet live-tested** — no game access during implementation; needs a
real playtest before this is considered done, same bar as every other native hook in
this project.

**New module**: `rumble.h`/`rumble.cpp` (kept separate from `controller_input.cpp`'s
XInput polling and `analog_input_hooks.cpp`'s gameplay-input translation, per
CLAUDE.md's module-separation rule). `Controller_SetVibration(left, right)` added to
`controller_input.cpp`/`.h` (dynamically loads `XInputSetState` from
`xinput9_1_0.dll`, same lazy-load pattern already used for `XInputGetState`).

**Calling conventions re-confirmed via raw disassembly before hooking** (pseudocode
alone wasn't trusted, per this project's own standing rule — a wrong calling
convention risks a crash): both `FUN_004895b0` (weapon-fire notify) and `FUN_0044cdb0`
(damage notify) are genuinely plain `__cdecl`, flat stack args, bare `RET` (caller
cleanup) — no custom register convention needed, unlike the kbutton-family calls
elsewhere in this codebase. `FUN_004895b0`'s own disassembly explicitly confirms its
event-handle argument is read via `MOVZX ... word ptr` — a real 2-byte handle, not a
pointer, matching the "each hashed via `FUN_005048b0` into a 2-byte handle"
documentation already on record.

**Local-player filter, resolved**: both notifies fire for ANY entity (AI included),
which the original 2026-07-17 research flagged as unresolved. Resolved this pass by
reusing a field this project ALREADY treats as a real "does this entity have a client
struct" gate: `entity+0x10c`, non-null-checked as its own precondition by
`FUN_005BC9A0` (the real native `notifyonplayercommand` registration function, issue
#29). **Honest caveat**: in solo SP/Survival (this project's only currently-supported
config) this is equivalent to "is the local player," since there's exactly one client
entity — but it does NOT specifically exclude a co-op partner's entity in 2-player
Survival, which would also pass this check. Not resolved this pass, documented rather
than silently assumed away.

**Hooks installed** (`Rumble_Install()`, called from `InstallAnalogInputHooks()`
alongside the other MinHook installs, same install/log pattern as every other hook in
this codebase):
- `FUN_004895b0` — fires a fixed-intensity/fixed-duration pulse when the event handle
  matches the live-read `"weapon_fired"` handle (read from its real runtime address,
  not hardcoded, since the interning hash is computed at startup) AND the entity
  passes the local-player filter.
- `FUN_0044cdb0` — fires a pulse scaled by the real damage amount (the function's own
  6th parameter, confirmed via the real call site's argument order) when the event
  handle matches `"damage"` and the entity passes the local-player filter, capped by a
  configurable max intensity.

**Decay**: a simple linear decay over a configurable duration, `GetTickCount()`-based
(same timer style as `InjectControllerSprint`'s stamina/cooldown timer), ticked once
per real gameplay frame (`Rumble_Tick()`, called from `InjectAllControllerInput` —
deliberately NOT the menu tick, since rumble is gameplay feedback, not a UI feature). A
stronger/longer pulse arriving while an earlier one is still decaying takes over
(peak intensity + a fresh decay window) rather than being additive or getting cut
short. **Known v1 simplification, not a placeholder**: both motors (low-frequency/
high-frequency) are driven equally — this engine's real per-event dual-motor profile
(if the console version had one) wasn't reverse-engineered or differentiated this pass.

**New config** (`mod_config.h`/`.cpp`, following the exact `[AimAssist]` pattern):
`[Vibration]` section — `Enabled` (default on, doubles as this feature's own
kill-switch, no separate `[Experimental]` entry needed on top of it), `FireIntensity`/
`FireDurationMs`, `DamagePerPoint`/`DamageMaxIntensity`/`DamageDurationMs`.

**Not yet reached** (same as the original research pass, still open): explosions/
blast-proximity, melee-hit-landed, killstreak activation, low-ammo rumble triggers.
`FUN_00470d00`'s ~600-entry real GSC notify-event-name table includes `"explode"`/
`"grenade_fire"`/`"missile_fire"` as leads, none traced to a dispatch site yet.

No native vibration infrastructure exists at all (confirmed via a clean
zero-hit string search for `rumble`/`vibrat`/`forcefeedback`), consistent with
the project's founding "zero controller path" finding — output must be entirely
our own `XInputSetState` calls. Research found real, hookable native events for
WHEN to trigger them:

- **Weapon fire — confirmed, single clean choke point.** `FUN_0045e320`
  (per-shot fire-effects handler) calls `FUN_004895b0(entity, "weapon_fired"
  handle, 1)` once per real shot, semi-auto and full-auto alike. Confirmed plain
  `__cdecl` via raw disassembly — safe to hook with the project's existing pattern.
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
- **`iw5sp.exe` is a DIFFERENT binary.** **Correction (2026-07-17, later
  session, via the sibling `MW32011NSP` project's direct re-measurement):**
  the "~175KB smaller" figure recorded here was wrong — actual size delta is
  only **2,320 bytes** (5,656,120 retail vs. 5,653,800 Plutonium). The
  ~175KB number was very likely a misremembering of a *byte-difference
  count* (`cmp -l` reports 175,411 individual differing byte positions
  across the overlapping region) as if it were an overall file-size
  difference. That said, 175K differing byte positions across a ~5.6MB
  binary is still real and substantial (~3%) — consistent with a genuine
  recompile (even small source changes cascade into widespread shifted
  call/jump-target immediates), just not the "wholesale rebuild" impression
  the wrong number gave. Every hardcoded address this project uses would
  still need independent re-verification against Plutonium's build — that
  conclusion is unchanged, only the specific size figure was wrong. Also,
  Plutonium's own docs confirm campaign isn't really their supported use
  case even though the file is present.
- **Anti-cheat concretely confirmed to ban DLL injection and memory access** —
  7-day first offense, permanent after. This project's entire architecture (proxy
  `d3d9.dll`, MinHook, memory-read-based aim-assist) is exactly what it's built to
  catch, input-only intent notwithstanding. **This sharpens CLAUDE.md's existing
  "MP anti-cheat exposure" flag from a theoretical concern into a confirmed,
  specific, high risk for Plutonium MP specifically** — do not use this project with
  Plutonium MP.
- **Cross-reference, per CLAUDE.md's cross-project policy (2026-07-17, later
  session)**: the byte-identical-`iw5mp.exe` fact recorded here turned out to
  have a direct netcode-security consequence, found by the sibling
  `MW32011NSP` project. Since Plutonium's MP client is unmodified from
  retail, any CLIENT-side (as opposed to server-side) netcode vulnerability
  found in retail `iw5mp.exe` is present on Plutonium MP installs too —
  Plutonium's own mitigation (routing through their own dedicated servers)
  only addresses server-side code, not client-side message parsing or demo
  playback. `MW32011NSP` found and confirmed at least one such client-side
  bug. Full detail: `MW32011NSP/re_notes/vulnerability_research.md`'s
  "Plutonium's mitigation does not cover client-side bugs" section. Not
  relevant to this project's own input-hooking work directly, but worth knowing
  if this project's own Plutonium-compatibility research continues.

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
| Plutonium — SP | SP | `iw5sp.exe` different (2,320 bytes size delta, ~175K individual differing byte positions — corrected 2026-07-17, was misstated as "~175KB smaller") | Unknown, needs re-verification | Not yet investigated |
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
  `_id_061C::_id_3DE2`. **UPDATE (2026-07-18, full killstreak catalog pass):**
  `_id_061C::_id_3DE2`'s body found and traced — there is NO per-type code
  divergence anywhere in it; delta and riotshield run byte-for-byte identical
  spawn logic, differing only in a cosmetic HUD icon. The "divergence is
  inside this function" hypothesis is REFUTED. Both types are equally
  `notifyoncommand`-gated on the same `+actionslot 4` bind — if a real bug
  exists, it's outside this GSC chain entirely (untested candidates: per-map
  spawn-path availability, or the riot-shield equipment item itself once an
  AI holds it). Also corrects the roster this section assumed: Survival's
  buy-station only ever sells 4 real killstreaks
  (`remote_missile`/`precision_airstrike`/`friendly_support_delta`/
  `friendly_support_riotshield`) — `stealth_airstrike`/`carepackage_c4`/
  `carepackage_ammo` (mentioned in `iw5sp.md`'s earlier "killstreak-crate
  table" lead) don't exist as purchasable items at all, confirmed absent
  from the real economy CSV. See `re_notes/killstreak_reference.md`'s new
  "Survival buy-station killstreak roster" section for the full corrected
  table, including `precision_airstrike`'s newly-resolved mechanism (a
  THIRD, genuinely different input path — a native `beginlocationselection`
  placement-marker API, not `notifyonplayercommand`-gated at all, possibly
  already reachable via this project's existing D-pad+A menu-navigation work).
- **Weapons**: real `WeaponCompleteDef`/`WeaponDef` struct found and confirmed
  via exact offset arithmetic. Separate native timers for normal reload vs.
  reload-from-empty exist — this project's single-kbutton reload almost certainly
  already gets correct behavior for free. A per-weapon-animation rumble-
  notetrack system found, relevant to task #17.
- **Perks (task #9)**: `HasPerk`-equivalent native query — genuinely parked, not
  solved. `hasperk` dispatches by compile-time numeric ID with zero string trace
  in the binary; `perk_sprintMultiplier` has exactly one reference (its own
  registration) — nothing native reads it, the scaling is entirely GSC-side. No
  clean native path exists without going through GSC itself.
  **(2026-07-18) New lead, not yet tried, more tractable than another static
  bitmask hunt**: `FUN_004b9350(playerStructAddr, currentTimeMs)` (found
  2026-07-16, already called by this project's own `LogSprintDiag` for diagnostic
  logging, player struct `&DAT_00984b88`) returns what looks like the real
  current sprint-meter value the HUD reads. If its return value differs with
  Extreme Conditioning equipped vs. not, that would prove some native code
  upstream of this call already reads perk-scaled sprint state somewhere —
  a live A/B log comparison (equip the perk, compare the logged value against
  not having it) is far more tractable than continuing a blind struct search,
  but needs live game access to test.
- **HUD/UI + buy-station**: confirmed a single central HUD dispatcher
  (`CG_OwnerDraw`-equivalent, ~150 cases, sprint meter as the anchor). Buy-station
  reads `sp/survival_armories.csv` via a generic GSC `tablelookup` builtin, not a
  bespoke function — reusable by any future debug tool.
- **AI/vehicles**: civilian AI library confirmed genuinely shared across
  missions. No dedicated vehicle input path found — real evidence (zero vehicle
  binds, hint text rendered as static strings) suggests vehicles reuse the same
  `usercmd_t` fields this project already hooks, meaning movement/look may already
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

## 27. Campaign controller playtest, live findings (2026-07-17, later session — in progress)

**Status:** User is playing through Campaign on controller and reporting real
fallback-to-keyboard/mouse points as they go. Logging each finding here as
reported; entry will keep growing across the session. Reached "Bag and
Drag" (Act II) with every mission up to that point fully playable, but with
targeted fallback needed at specific points — mainly vehicles and
killstreaks, not blanket failures. Directly relevant to task #7 (killstreak
input) and refines issue #26's vehicle hypothesis below.

- **Mission "Hunter Killer" (Act 1) — DPV (Diver Propulsion Vehicle)
  sequence: movement works, aiming does not.** The underwater DPV segment
  (real name confirmed via web search — matches the user's "seaglide-style
  thing like Subnautica" description) let the player move/steer on
  controller fine, but aiming while on the DPV required falling back to
  mouse. **Vehicle-specific, not blanket**: the same mission's boat section
  worked fully on controller (movement AND aim) — so this isn't "vehicles
  in general lack aim input," it's specific to the DPV. This is a real,
  live-confirmed counterexample to issue #26's AI/vehicles hypothesis
  ("vehicles reuse the same `usercmd_t` fields this project already hooks,
  meaning movement/look may already work in vehicle sections with no new
  code") — that hypothesis holds for at least the boat, but not for the
  DPV specifically. Root cause not yet investigated — candidate theories,
  none confirmed: the DPV may use a separate aim/camera mechanism from
  normal vehicle-mounted turrets or on-foot look (e.g. a distinct
  view-angle path our right-stick look hook doesn't reach), or may gate aim
  input behind a different kbutton/dvar this project doesn't drive. Needs real
  RE work (find the DPV's own entity/vehicle type and its input-handling
  path) before attempting a fix — not diagnosed yet, just captured
  precisely as reported.
- **Bug #2 — Crouch intermittently fails to fire, ~98% reliable, ~2% silent
  no-op; recovers if the player pauses and unpauses in certain
  sequences.** Reported as a real, reproducible-but-rare live playtest
  finding, not a one-off fluke — happens often enough to notice a pattern
  (pause/unpause fixes it) but rare enough to characterize as ~2% of
  attempts. **Real, plausible lead already on record from issue #9's own
  `ToggleStance()` (`FUN_0057d2c0`) write-up, worth checking first**: that
  function has two guard bytes at the very top —
  `if (byte[playerIndex*0x230 + 0xA98CA0] != 0) return;` and
  `if (byte[playerIndex*0x230 + 0xA98BC4] != 0) return;` — if EITHER is
  nonzero, the toggle silently no-ops with no error/feedback, which matches
  "crouch sometimes just doesn't fire" exactly. Neither guard's real
  meaning was decoded when issue #9 was written (only the toggle logic
  itself was needed at the time). **Why pause/unpause would fix it**: this
  project has an established pattern of exactly this class of bug elsewhere
  (the `g_paused`/`cl_paused` desync fix, and issue #9's own root cause —
  "our own competing per-frame bit-forcing... fighting" real engine state)
  — a locally-tracked/stale bit getting out of sync with real engine state,
  where a pause/unpause cycle happens to force a re-read/re-sync. Whether
  that's literally what's happening here (vs. these two guard bytes being
  genuine, unrelated engine gating conditions like "in a cutscene" / "stance
  change locked by animation") is NOT yet confirmed — needs live diagnostic
  logging on both guard bytes (the same `LogStanceDiag`-style technique
  issue #9 already used) to catch a real failure in the act, not assumed
  from this theory alone.
- **Bug #3 — Hold Breath on sniper scopes was never implemented (a known,
  forgotten gap, not a new discovery), and its absence causes a real,
  incorrect side effect: L3 while crouched + ADS with a sniper wrongly
  fires Sprint's auto-stand-up behavior instead of doing nothing/holding
  breath.** Two parts, directly connected:
  1. **Missing feature**: Hold Breath (sniper sway reduction while ADS,
     normally on the same physical input as Sprint — Left Thumb/L3 on
     console) was never built. This isn't a bug in existing code, it's a
     feature that was simply never started.
  2. **Real bug this causes**: with Hold Breath absent, L3 unconditionally
     runs this project's own Sprint implementation — which, per issue #9's
     fix, auto-stands the player up out of crouch/prone before sprinting
     (`ForceStandingViaRealToggle`, matching real console Sprint behavior
     when NOT aiming). But there's no check for "currently ADS with a
     sniper" before that auto-stand fires, so pressing L3 while crouched
     and scoped in forces the player upright — breaking cover/stance
     specifically in the situation (sniping) where staying low matters
     most, and where a player's real intent was almost certainly "hold
     breath," not "sprint."
  - **Direct tie to already-parked research**: the real engine bind is
    `+breath_sprint` (`players2/config.cfg`: `bind SHIFT "+breath_sprint"`,
    confirmed via the "Real keycode reference" work below) — a SINGLE,
    unified native bind for both Sprint and Hold Breath depending on
    context (the SP default-keybind table in `iw5sp.md` literally labels
    it "Left Thumb = Sprint/Hold Breath"). The engine itself treats these
    as one contextual input, not two separate ones. This project's own Sprint
    implementation never found/drove the real `+breath_sprint` kbutton
    (three independent search attempts, all parked — see "Sprint's real
    kbutton — PARKED" below) and instead forces sprint via raw `pm_flags`
    manipulation, which has no concept of "context" at all — it's Sprint
    or nothing. **This strongly suggests Hold Breath was likely always
    going to be blocked on the same parked kbutton-search work**, since
    the real native mechanism is one bind, not two. Worth revisiting the
    kbutton search specifically through this lens (maybe the ADS+scoped
    context is what was missing from the earlier 3 search attempts, since
    none of them tried transitioning Shift while already aiming down a
    scoped weapon).
  - **Minimum viable fix for the bug (part 2) without waiting on the
    kbutton search**: gate the existing auto-stand-on-sprint logic behind
    a real "is ADS with a sniper-class weapon" check before it fires —
    stops the incorrect stand-up regardless of whether real Hold Breath
    ever gets implemented. Two separable pieces of work, not one blocking
    task.
  - **Part 2 IMPLEMENTED (2026-07-18)**: `InjectControllerSprint`'s
    auto-stand call is now gated on `!g_adsHeld` (`analog_input_hooks.cpp`)
    — builds clean, not yet live-tested. **Simplification from the ideal
    fix above, worth flagging**: gates on "is ADS'd with ANY weapon," not
    specifically "is ADS'd with a sniper-class weapon," since no clean
    native weapon-class query was available in this pass. This is
    conservative (never wrongly forces standing while ADS'd, regardless of
    weapon type) but means Sprint's rising edge is now a no-op stance-wise
    for every ADS'd weapon, not just snipers — real console behavior for
    non-sniper ADS+Sprint interaction (does Sprint even engage while ADS'd
    at all on other weapons, or does it cancel ADS first?) was not
    investigated and should be live-tested alongside this fix. Part 1
    (real Hold Breath sway reduction) remains unimplemented — see the
    research note below for a recommended approach.
  - **Hold Breath (part 1) research pass (2026-07-18):** confirms the
    kbutton search really is a dead end for this too — `+breath_sprint`
    is the same bind already exhaustively searched for in "Sprint's real
    kbutton — PARKED" (`iw5sp.md`), so re-attempting a kbutton search
    isn't worthwhile. **Recommended approach instead**: implement Hold
    Breath the same way Sprint's own stamina system was implemented — this
    project's own additive sway-reduction layer (scale down look-rate or a
    tracked sway multiplier while the bind is held + ADS'd + weapon has
    the confirmed-real `canHoldBreath` bool flag from `WeaponCompleteDef`),
    not a native-call-driven feature. The exact native sway-consumer
    function/field offsets were not pinned down this pass (a genuine gap,
    not a confirmed dead end) — would need a fresh Ghidra pass against the
    existing `MW3` project or OpenAssetTools' `IW5_Assets.h` field order to
    resolve `canHoldBreath`'s exact struct offset before implementing.
    **SUPERSEDED 2026-07-19**: this recommendation predates the Sprint
    kbutton discovery below, which reopened this exact search and found it.
  - **Part 1 IMPLEMENTED 2026-07-19**, once Sprint's own kbutton search (this
    issue, #6) turned up a second kbutton on the same `+breath_sprint`
    dispatch case: `0xA98C04`, fired back-to-back with Sprint's `0xA98CCC` on
    every real SHIFT press, unconditionally — confirming this bind really is
    the shared, context-sensitive native Sprint/Hold-Breath input the
    research pass above suspected, and that a real kbutton exists after all
    (the "dead end" conclusion above was about finding it via memdiff/heap
    correlation specifically, which never worked for any of this bind
    family — the static jump-table technique that found Sprint's kbutton
    found this one too, as a byproduct). `analog_input_hooks.cpp` now drives
    `0xA98C04` via `CallKbuttonDown`/`CallKbuttonUp` (same convention as
    ADS/Reload/Sprint/Fire), gated on `g_adsHeld` rather than stance — Hold
    Breath while crouched or prone and scoped in is a normal case, unlike
    Sprint's own standing-only gate. No `canHoldBreath` weapon-class check
    was added: the real bind fires this kbutton unconditionally regardless
    of weapon, and per the same permissive precedent as the ready-up F5
    synthesis, the engine simply ignores it on weapons that don't support
    Hold Breath. Builds clean (0 warnings/0 errors, full rebuild).
    **Not yet live-tested** — needs a sniper-ADS playtest to confirm actual
    sway reduction, and a non-sniper-ADS + L3 check to confirm Sprint
    correctly stays disengaged while aiming (already covered by part 2's
    `!g_adsHeld` gate on the auto-stand, live-tested 2026-07-18).
  - **CONFIRMED LIVE REGRESSION, DISABLED same day (2026-07-19).** User's live
    sniper-ADS playtest: "real bug with the ads sniper now it always holds
    breath once toggled initially you hold LS to hold breath not toggle and
    theres no way to toggle it off" — i.e. the first engage works as a real
    hold (matches design intent), but once released it gets stuck permanently
    active with no way to cancel it. Same OBSERVED symptom class as the ADS
    "activates once then stays stuck" bug from 2026-07-14 (see
    `analog_input_hooks.cpp`'s big comment above `CallKbuttonDown`) — but
    this reuses those already-fixed helper functions with a consistent
    `bindIndex` both directions and the correct `timeMs` third arg, so
    neither of that bug's two known root causes (mismatched keyId between
    Down/Up, missing stack arg) applies here directly. **Real cause NOT yet
    found** — two live hypotheses, neither confirmed: (a) this project's own
    `g_sprintHeld`/`g_adsHeld` tracking isn't actually reaching the `KeyUp`
    call the way the code implies (something about `InjectControllerAds`'s
    real edge timing relative to `InjectControllerSprint` not yet
    independently re-verified this pass), or (b) the native Hold Breath
    EFFECT itself may not be a simple "clears the instant the kbutton goes
    up" state the way Sprint/ADS demonstrably are — it could have its own
    real duration/exit condition this project doesn't know about yet, given
    `0xA98C04` was only ever confirmed as "a kbutton the real SHIFT press
    also drives," not independently confirmed to behave like a clean
    hold-while-down toggle at the engine level.
  - **DISABLED (2026-07-19)**: `kHoldBreathLiveEnabled = false` in
    `analog_input_hooks.cpp` — the real `CallKbuttonDown`/`CallKbuttonUp`
    calls were skipped entirely (matches the same disable-first,
    diagnose-after precedent as the rumble-hook and boot-zone-splice crashes
    this same session), while a `[hold-breath-diag]` log line still fired on
    every edge transition (DOWN/UP) so a future playtest could capture the
    real transition sequence without touching live game state.
  - **ROOT-CAUSED and FIXED, same day (2026-07-19)**, via a dedicated Ghidra
    fork rather than a live diagnostic pass (user chose to go straight to a
    real fix, on the basis that it's cleanly revertible either way): fully
    decompiled `FUN_0057d1c0`/`FUN_0057d200` (KeyDown/KeyUp) and reconstructed
    the real `kbutton_t` layout — `down[0]`/`down[1]` keyId slots at
    `+0x00`/`+0x04`, `downtime` at `+0x08`, `msec` at `+0x0C`, an `active`
    flag byte at `+0x10`, and a **second flag byte at `+0x11`**. KeyDown sets
    BOTH `+0x10` and `+0x11` to 1; KeyUp correctly clears `+0x10` once both
    `down[]` slots are empty, but **structurally never touches `+0x11`
    anywhere in its own decompiled body** — confirmed by reading the complete
    function, not inferred. Since KeyDown/KeyUp are leaf functions (call
    nothing else), any real gameplay code reading "is Hold Breath active"
    must read one of this struct's own fields — `+0x10` demonstrably works
    correctly (proven by Sprint/ADS not exhibiting this bug), making `+0x11`
    the coherent explanation for a "sets once, stays forever" symptom
    specifically. Independently ruled out along the way: the real
    dispatcher's UP-case (`FUN_00438710` case 10, disassembled fully) is a
    plain, symmetric `KeyUp` call with nothing extra — native code does
    nothing beyond what `CallKbuttonUp` already replicates, so the fix has
    to happen on this project's own side, not by finding a missing native
    call. Also ruled out: bindIndex collision between Sprint (16) and Hold
    Breath (18) — exhaustive xref search confirms each kbutton_t's `down[]`
    slots are only ever touched by that struct's own 4 real references, all
    inside the dispatcher; the two features' arbitrary index choices cannot
    cross-contaminate.
  - **Attempted fix #2**: `UpdateHoldBreathKbutton` manually zeroed
    `kbutton+0x11` via a direct volatile byte write immediately after calling
    the real `CallKbuttonUp`. Built clean, re-enabled, **but user's live
    playtest confirmed STILL stuck** — same symptom, unchanged. The `+0x11`
    theory was well-evidenced from the decompile alone (a real, confirmed
    field KeyUp never clears), but is not the (or not the only) real cause of
    the visible bug — whatever native code actually drives the sway-reduction
    effect either doesn't read `0xA98C04` directly at all, or reads it via a
    path not found this pass (a dedicated follow-up Ghidra fork confirmed the
    native dispatcher's UP-case does nothing beyond a plain symmetric KeyUp,
    and ruled out bindIndex collision, without finding the real consumer —
    an exhaustive xref search on both kbuttons found only 4 references each,
    all inside the dispatcher itself, meaning any real consumer resolves the
    kbutton dynamically rather than by hardcoded address, beyond what a
    static xref search reaches in reasonable time).
  - **REAL FIX, 4th key-synthesis exception (2026-07-19)**: user's call, given
    two direct-kbutton attempts both failed live and the mechanism clearly
    isn't fully understood — stop trying to drive `0xA98C04` directly at all,
    and instead synthesize a REAL Shift keypress (`PostMessage`
    `WM_KEYDOWN`/`WM_KEYUP`, `VK_SHIFT`) whenever ADS'd, the exact same
    real bind a keyboard player's Shift press already takes (`bind SHIFT
    "+breath_sprint"`). This is the FOURTH exception to this project's "no
    OS-level input emulation" rule, joining Survival ready-up's F5, D-pad
    Left's squadmate-call-in `'4'`, and Back's scoreboard TAB — same
    justification each time (IW5 has no DirectInput import at all, so
    keyboard input is genuine window messages, making a synthetic keypress
    indistinguishable from a real one). Sidesteps whatever this project's own
    direct-kbutton approach was missing entirely, by routing through the
    real native input pipeline instead of trying to replicate it.
    `SendSyntheticHoldBreathKey()` replaces `UpdateHoldBreathKbutton`/the
    kbutton constants entirely (removed, not just disabled).
    **Side effect deliberately accounted for**: a real Shift press also fires
    Sprint's own kbutton (`0xA98CCC`) natively — `IsSprintActive()` was
    updated to also exclude `g_adsHeld` (previously only stance-gated), so
    this project's own direct Sprint-kbutton path and the new synthetic-Shift
    path are fully mutually exclusive (raw kbutton owns Sprint when NOT
    aiming; synthetic Shift owns Hold Breath — and harmlessly re-fires
    Sprint's kbutton too, exactly like a real keyboard press, which the
    engine itself already ignores while ADS'd) rather than both trying to
    claim the same kbutton_t's `down[]` slots simultaneously. Builds clean (0
    warnings/0 errors, full rebuild).
  - **STILL CONFIRMED STUCK LIVE with the synthetic-Shift fix active** — a genuine
    surprise: user confirmed the bug reproduced even using PURE keyboard/mouse
    (zero controller touch at all), which should have made every one of this
    project's own controller-gated `Inject*` functions a complete no-op. Ruled
    out via direct testing, in order: (1) NOT a leftover-corruption-from-earlier-
    testing theory (a full game process restart didn't clear it); (2) NOT a
    genuine pre-existing retail bug (confirmed working correctly with `d3d9.dll`
    removed entirely — vanilla MW3 does NOT have this bug); (3) NOT a
    controller/keyboard input race (still broke with the controller fully
    disconnected, not just idle). This meant the corruption had to come from
    something in this DLL that runs unconditionally every frame, regardless of
    input device — narrowing it to `Hook_ControlsLinkTo` (`0x5d7f20`) and
    `Hook_MissileGuidanceDispatch` (`0x4554d0`), the only two other hooks
    installed besides the core movement/look hook, both added this same session
    for the unrelated Predator Missile guidance investigation (issue #30) and
    both running every frame regardless of context (`Hook_MissileGuidanceDispatch`
    especially, confirmed called every single tick from the real Pmove-tick
    function, not just during an actual missile flight).
  - **CONFIRMED FIXED**: disabled both hooks (`InstallAnalogInputHooks`, code
    kept, not deleted — same precedent as the rumble hook). **User confirmed
    live**, including the strongest test yet (controller ADS held simultaneously
    with a real keyboard Shift press, the hybrid scenario most likely to expose
    a race) — Hold Breath now "works perfect." Root-caused to one or both of
    these two hooks, not fully bisected further (not worth the risk of
    re-enabling either just to find out which) — **`Hook_MissileGuidanceDispatch`
    is the leading suspect** given it uniquely runs every single frame
    unconditionally, but this is not proven over `Hook_ControlsLinkTo`
    specifically. Both must stay disabled until task #30's missile-guidance work
    resumes with a properly re-verified, safe hook design — re-enabling either
    without first understanding what was actually wrong would risk
    reintroducing this exact regression. See issue #30 below for the
    cross-reference.
  - **Hold Breath itself: CONFIRMED WORKING LIVE (2026-07-19)** with the 4th
    key-synthesis exception active (synthetic Shift while ADS'd,
    `IsSprintActive()` excluding `g_adsHeld`), closing task #24's original
    scope via that design.
  - **RETEST, same day**: with the two interfering hooks confirmed disabled,
    reverted to the ORIGINAL, simplest design (plain `CallKbuttonDown`/
    `CallKbuttonUp` on `0xA98C04`, no `+0x11` manual clear, no key-synthesis)
    to check whether that design was actually correct all along and the
    interference was the only real problem. **CONFIRMED STILL STUCK** — so
    there IS a genuine, separate problem specific to driving `0xA98C04` as a
    kbutton, independent of the now-fixed interference. Task #24 reopened.
    Given a dedicated Ghidra pass already confirmed `KeyDown`/`KeyUp` are leaf
    functions touching only this kbutton_t's own memory, and an exhaustive
    xref search found only 4 total references to `0xA98C04` anywhere in the
    binary (all inside the dispatcher itself, none anywhere else) — no other
    C++ code reads this kbutton_t by a hardcoded address at all. This means
    either the real Hold Breath consumer resolves it dynamically (e.g. from
    GSC, at runtime, by bind name) or the "`0xA98C04` = Hold Breath's kbutton"
    identification itself is wrong (just an adjacency coincidence in the
    dispatcher's case 9, which also touches Sprint's real kbutton on the same
    press). **Pivoting to file-based research** (GSC script corpus + weapon
    CSV/GDT data for `canHoldBreath` and sway-related fields) rather than
    further x86 static RE, which has been exhausted for this specific
    question.
  - **File-based research: clean negative, zero GSC/weapon-data involvement**
    (2026-07-19). Full GSC corpus search (317 files) for `breath`/`sway`/
    `steady`/`holdbreath`/`canholdbreath`/`aimsway`/`scopesway` found nothing
    relevant — the sway-reduction effect is 100% native, confirming rather
    than contradicting the Ghidra fork's "leaf function" finding. Weapon data
    (`iw5_barrett_mp`) confirms `canHoldBreath` is a bare boolean capability
    flag with no associated sway-magnitude/duration field anywhere in its
    ~1567-field list — the actual effect strength is a hardcoded native
    constant, not per-weapon data. No loose "breath" string anywhere in
    `iw5sp.exe` either, closing off any GSC-builtin-by-name theory.
  - **DEFINITIVE ROOT CAUSE FOUND (2026-07-19), permanent fix confirmed.** A
    dedicated Ghidra pass resolved this completely: `0xA98C04` is not an
    independent kbutton_t at all — it is literally **Fire's own `down[1]`
    slot** (`0xA98C00 + 4`, Fire's real, already-confirmed kbutton). The real
    dispatcher's case 9 calls `KeyDown`/`KeyUp` with `self=0xA98C04` anyway,
    which makes those functions treat that memory as a brand-new struct's own
    `+0x10`/`+0x11` fields — but those fields land exactly on `0xA98C14`/
    `0xA98C15`, which is **also** array slot 1 of `FUN_0057dc90` (the
    per-frame simple-bind reader, already known from earlier `notify` research
    as a 10-entry/stride-0x14 array starting at `0xA98C00`). That function
    unconditionally zeroes byte `+1` of its own slot 1 (`= 0xA98C15 =
    0xA98C04+0x11`) every single frame, for a completely unrelated bind, with
    no awareness that anything else is using that memory. Two genuine engine
    subsystems unknowingly share one memory region — this is why every
    fix attempt targeting `0xA98C04`'s own fields directly (plain kbutton
    calls, the `+0x11` manual clear) was doomed regardless of how correct the
    fix logic was: something else stomps the exact byte the mechanism depends
    on, every frame, unconditionally. (Bonus finding, not chased further:
    `kAdsKbutton2`'s own `+0x10`/`+0x11` coincide with a different one-off
    entry in the same array — a second instance of the same aliasing class,
    though ADS doesn't visibly break, plausibly because its effect doesn't
    depend on those exact bytes.) **Conclusion: driving `0xA98C04` directly can
    never be made reliable. The 4th key-synthesis exception
    (`SendSyntheticHoldBreathKey`, synthetic Shift while ADS'd) is the correct,
    permanent design — not a workaround to keep chasing away from** — confirmed
    by the user as the adequate permanent fix. Reverted back to this design
    (see the commit history for the brief direct-kbutton retest and revert).
    **Task #24 CLOSED.**
  - **REOPENED 2026-07-20: live regression, "perma on... like even native."**
    User reported Hold Breath stuck on again during the same session as the
    look-acceleration-ramp playtest (issue #32). Pulled `proxy_d3d9.log`
    (`hold-breath-diag-v2`) for that session before touching any code — this
    time the evidence points somewhere new: every `DOWN` in the log is
    followed by a clean `UP` (`g_sprintHeld=0`), including the final segment
    before the user paused and quit (4+ seconds scoped, no further L3 press
    logged, no stuck-true tracking state). This rules out a repeat of the
    original "our own tracking never goes false" bug — this project's own
    `g_sprintHeld`/`g_holdBreathSyntheticHeld` state is behaving correctly.
    User confirmed (asked directly) that releasing L3 does nothing once
    stuck — the effect stays on regardless, which the user themselves
    characterized as behaving "like even native," i.e. the NATIVE
    breath-hold state itself is latched on despite our synthetic `WM_KEYUP`
    correctly firing. The same log also shows a burst of `DOWN`/`UP` pairs
    landing inside the same or adjacent frame (no heartbeat between them) —
    faster edge transitions than a human could physically produce on a real
    keyboard, right around where the stuck report originated. **New working
    hypothesis, tying directly into the same-day issue #32 finding**: this
    engine is locked to a 30fps tick (33.33ms/frame) and cannot be assumed
    to cleanly process input transitions faster than that — a `WM_KEYUP`
    posted too soon behind a `WM_KEYDOWN` for the same key is a plausible
    way for the native handler to silently drop the release and latch the
    effect on. **Fix attempt**: added a 40ms debounce
    (`kHoldBreathDebounceMs`, `analog_input_hooks.cpp`) around
    `SendSyntheticHoldBreathKey` — a state change is only actually sent to
    the native window once at least one engine frame has elapsed since the
    last transition we sent; faster flicker is coalesced (the same check
    re-runs every frame, so the moment debounce clears it sends whatever the
    CURRENT desired state is, not a queued backlog of stale transitions).
    Builds clean. **Not yet live-tested** — if stuck-on recurs even with the
    debounce, the "too-fast WM_KEYUP dropped" theory is wrong and the real
    native consumer of this key state still isn't understood; don't just
    raise the debounce number blindly if that happens, go back to RE. Task
    #24 reopened pending this retest.
  - **Debounce theory FALSIFIED live (2026-07-20).** Retested with the 40ms
    debounce in place — still stuck on, and the user confirmed going in and
    out of ADS afterward didn't clear it either. Critically, that session's
    log showed a single, cleanly-spaced `DOWN -> heartbeat -> UP` cycle
    (~130ms apart, well past the 40ms debounce, no rapid flicker at all)
    still resulted in a reported stuck state — ruling the debounce theory
    out definitively, not just leaving it unconfirmed. **Escalated to a
    different mechanism entirely**: `PostMessage` only queues a window
    message for the target HWND's own pump to process — it does NOT touch
    the OS-level keyboard state table `GetKeyState`/`GetAsyncKeyState`
    read. The other 3 key-synthesis exceptions (Survival ready-up's F5,
    D-pad Left's `'4'`, Back's TAB) are all one-shot/transient triggers, so
    a message-driven handler catches them fine — Hold Breath is
    architecturally different, needing a SUSTAINED "is this key currently
    down" read every frame for as long as it's held. Working hypothesis:
    the native check for "is breath currently held" polls a real OS-level
    keystate rather than watching for `WM_KEYUP`, which fits every
    symptom observed: the press engages it, but `PostMessage`'s
    `WM_KEYUP`, never having touched that keystate table, leaves the poll
    reading "down" forever regardless of pacing or later ADS toggles.
    **Fix attempt**: switched `SendSyntheticHoldBreathKey` from
    `PostMessageA(WM_KEYDOWN/WM_KEYUP)` to `SendInput` with a real
    `INPUT_KEYBOARD` struct — unlike `PostMessage`, `SendInput` actually
    updates `GetKeyState`/`GetAsyncKeyState`, making it genuinely
    indistinguishable from a real hardware press even to code that polls
    key state directly. Gated on the game actually holding OS foreground
    focus (`GetForegroundWindow() == hwnd`), since `SendInput` is
    system-wide, not window-scoped like `PostMessage` was — a real
    player's Shift press couldn't reach the game otherwise either. Builds
    clean. **Not yet live-tested.**
  - **SendInput ALSO confirmed stuck live (2026-07-20)** — identical symptom
    to the PostMessage attempt. Two different transport mechanisms now
    failing identically means the transport layer was never the actual
    problem; something about what happens once the key event reaches the
    native engine is at fault. **User's own hypothesis, worth taking
    seriously**: this project's own Sprint-kbutton code
    (`UpdateSprintKbutton`, driven directly on `0xA98CCC` with this
    project's own `kSprintBindIndex=16`, gated `!g_adsHeld`) might be
    interacting badly with the SAME `0xA98CCC` kbutton also being touched by
    the native engine's own internal dispatch whenever the synthetic Shift
    reaches it — per the case-9 disassembly already on record, a real (or
    now genuinely OS-level synthetic) Shift press unconditionally drives
    BOTH `0xA98C04` (Hold Breath's alias, already proven corrupted by
    `FUN_0057dc90`) AND this exact same `0xA98CCC` Sprint kbutton, using
    whatever bindIndex the native dispatch uses internally — not this
    project's own `16`. If the real sway-reduction check actually reads
    Sprint's own (otherwise reliable) kbutton state rather than the known-
    corrupted `0xA98C04` alias, a lingering down[] slot on `0xA98CCC` from
    either source would explain a 100%-reproducible stuck-on symptom far
    better than a timing race against `FUN_0057dc90` would (a race would be
    expected to be intermittent, not perfectly consistent).
  - **Diagnostic-first, not a third blind fix (2026-07-20).** Rather than
    guess again, added a real-memory kbutton_t readback
    (`ReadKbutton`/`AppendKbuttonSnapshots`) that logs `down[0]`/`down[1]`/
    `active`(+0x10)/the `+0x11` flag byte for BOTH `0xA98C04` and `0xA98CCC`
    directly from live process memory. Wired into every Hold Breath DOWN/UP
    transition log AND widened the heartbeat to fire for the ENTIRE ADS
    window (not just while this project's own `holdBreathActive` tracking
    believes it's engaged) — the previous heartbeat went silent the instant
    our own UP fired, leaving the rest of a scoped session (exactly where
    the user reports the effect still visually stuck) completely
    uninstrumented. Next live session's `proxy_d3d9.log` should show
    directly whether either kbutton is actually still down when the sway
    effect is reported stuck, confirming or refuting the Sprint-kbutton
    theory with hard data instead of another guess. Builds clean. **Not
    yet live-tested** (diagnostic only — SendInput synthesis unchanged).
  - **Diagnostic returned a conclusive answer, Sprint-kbutton theory
    REFUTED, real culprit isolated (2026-07-20).** A full live session's
    readback log shows `0xA98CCC` (Sprint's real kbutton) toggling `active`
    perfectly in sync with its own `down0` on every single cycle —
    completely clean, ruling out the user's Sprint-interference hypothesis.
    `0xA98C04` (Hold Breath's alias) tells a different story: `down0`/
    `down1` cycle correctly (`0 <-> 160` = `VK_LSHIFT`) on every press and
    release, exactly as expected — but its `active` byte (+0x10) latches to
    `1` on the very FIRST release in the session and **never returns to `0`
    again**, for the rest of the log, regardless of how many further
    DOWN/UP cycles follow. This is direct, repeated, live-measured evidence
    — not a decompile-based guess like the earlier `+0x11` attempt — that
    `+0x10` specifically is the field failing to follow `KeyUp` on this
    alias, while the down-slots themselves are completely fine. **Fix**:
    added `ClearHoldBreathActiveFlag()`, force-clearing `0xA98C04+0x10`
    ourselves right after every synthetic release, plus a continual
    per-frame self-heal while Hold Breath isn't supposed to be engaged
    (cheap — a single byte write) — the live data showed this field never
    recovers on its own once corrupted, so a one-shot clear on the edge
    alone might not be durable enough if the edge is ever missed. Builds
    clean.
  - **CONFIRMED FIXED LIVE — task #24 CLOSED (2026-07-20).** User confirmed
    the force-clear resolved it completely: "goodf news completely fixed."
    Also corrected this project's own understanding of the actual effect
    while confirming it — it's not a weapon-sway reduction, it's the aim
    STEADYING while breath is held, with accuracy dropping noticeably once
    breath runs out; the user previously had zero control over that state
    once it locked on, matching the observed "active flag latched forever"
    root cause exactly. Corrected throughout this project's own docs/
    comments where "sway reduction" was used loosely for this effect. A
    pure native (direct-kbutton, no key-synthesis) variant was proposed as
    a possible future cleanup, but the current SendInput +
    force-clear combination is confirmed working and is not blocking
    anything — no urgency to revisit unless the synthetic-key approach
    causes some other issue down the line.
  - **Third native attempt, user-requested — CONFIRMED WORKING LIVE, task
    #24 permanently closed (2026-07-20).** The first two direct-kbutton
    attempts on `0xA98C04` (plain `CallKbuttonDown`/`CallKbuttonUp`, then a
    manual `+0x11` clear) both failed live BEFORE this project knew which
    field was actually the problem — the readback data above proved it's
    `+0x10` (`active`) that doesn't follow `KeyUp`, not `+0x11` (the second
    attempt's guess, based on a decompile read rather than measured data,
    and the wrong byte). With the fix (`ClearHoldBreathActiveFlag`) already
    confirmed live via the synthetic-Shift path, tried driving `0xA98C04`
    directly again — same `CallKbuttonDown`/`CallKbuttonUp` calls as
    attempt #1, this time paired with the same `+0x10` clear. **User
    confirmed: "it works natively as intended."** Hold Breath now drops
    the 4th key-synthesis exception entirely — genuinely native input
    (kbutton + a proven single-byte fix for the one field that didn't
    self-clear), not emulation. `SendSyntheticHoldBreathKey`,
    `GetForegroundWindow`-gating, and the now-unused local `GetGameWindow`
    forward-declaration were removed from this section as dead code (the
    synthetic path stays fully recoverable from git history if ever
    needed again — see commit history around this date). This is the
    final, permanent design for Hold Breath; the 4-exceptions count for
    "no OS-level input emulation" drops back to 3 (Survival ready-up's F5,
    D-pad Left's `'4'`, Back's TAB).
- **Positive result — Mission "Persona Non Grata" (Act 1, immediately after
  Hunter Killer): the UGV (Unmanned Ground Vehicle, mounted minigun +
  grenade launcher, played as Yuri) worked perfectly on controller as
  expected** — no fallback needed. Second confirmed-working vehicle
  alongside Hunter Killer's boat, reinforcing that issue #26's "vehicles
  reuse the same `usercmd_t` fields, may already work with no new code"
  hypothesis holds broadly — the DPV (bug #1 above) looks like the
  exception so far, not the rule.
- **Bug #4 — Mission "Turbulence" (Act 1, the hijacked-plane mission): during
  the scripted plane-breaking-apart sequence, the player retained free
  movement on controller when the intended design is for the player to be
  held still/scripted-locked.** User's own framing: "again a bug none the
  less," and noted in passing this scenario is thematically "the reverse of"
  a CoD4 Spec Ops mission (not independently verified, a passing
  observation not load-bearing for the fix). **Real, plausible, and
  potentially systemic root cause worth checking first**: this project injects
  left-stick movement as a POST-hook, additive pass on top of whatever the
  real engine's own movement summer (`FUN_0057d430`) already computed for
  that frame (see the core architecture notes above — "post-hook, additive
  on top of keyboard"). If the real engine enforces a scripted "player
  frozen"/cinematic-lock state by zeroing or ignoring `forwardmove`/
  `rightmove` INSIDE that same original function during such sequences,
  our additive post-hook would still inject fresh movement afterward,
  bypassing the lock entirely — the same class of "right mechanism, wrong
  layer" trap already hit and fixed for Prone/ADS/Sprint (see issue #9).
  **Not yet confirmed** — needs checking whether a real "movement locked"
  flag/dvar exists and is set during this sequence, and whether our hook
  should gate on it (skip injecting movement when set) rather than blindly
  adding every frame. **Potentially higher-impact than just this one
  mission** if the same lock mechanism is reused by other scripted
  sequences elsewhere in the campaign — worth a general fix (respect the
  real lock flag universally in the movement hook) rather than a
  single-mission special case, once the real flag is found.
  **FOUND (2026-07-20, research fork) — real freeze flag confirmed via
  existing disassembly, no fresh Ghidra pass needed.** Both
  `FUN_0057d430` (the real movement summer) and `FUN_0057d7e0` (the real
  view-angle/look updater) gate on the exact SAME bit — `0x800` on the
  per-client dword at `+0x1094` (issue #30's own flag family; for player
  0 this resolves to `0xB363B0 + 0x1094 = 0xB37444` — confirmed
  independently two ways, see the address-bug note in issue #30 above).
  `FUN_0057d430`: `TEST dword ptr [EBP+0x1094],0x800` → jumps straight to
  its epilogue, skipping all four `forwardmove`/`rightmove`-family writes
  for that frame when set. `FUN_0057d7e0`: `TEST dword ptr
  [0x00b37444],0x800` → same pattern, skips the entire view-angle
  computation. **This is the real fix**: this project's movement/look
  hooks (additive post-hooks that run regardless of what the native
  functions did) should read this same bit and skip injecting
  movement/look input when it's set, matching native behavior during
  Turbulence's plane-breakup sequence and any other scripted-freeze
  moment that reuses the same flag. Not yet implemented — this was a
  research-only pass, ready for the next implementation session.
- **Bug #5 — MISSION CORRECTED 2026-07-18 (was "Back on the Grid," really
  "Goalpost" — see task #26's zone-identification entry below): village
  mortar sequence, aiming worked correctly on controller (sensitivity
  matched keyboard/mouse setup, aim/traverse fully usable), but firing it
  did not** — had to fall back to keyboard/mouse specifically to fire, despite
  the mortar being otherwise fully controller-driven for aim. This has a
  cleaner shape than bugs #1/#3/#4 above: aim/look already routes through
  this project's real right-stick-look hook (which is why sensitivity/aim
  "just worked," consistent with how ADS/normal look already behaves), but
  the mortar's own FIRE input is very likely a distinct bind/kbutton from
  normal `+attack` (mortar sequences in this engine family are typically
  built as a bespoke minigame with their own dedicated fire command, not
  the regular weapon-fire path) that this project's RT/`+attack` hook was never
  wired to reach, or reaches but the mortar-specific handler ignores. Needs
  the same treatment as any other not-yet-wired input: find the mortar's
  real fire bind/kbutton (likely via the same raw-keycode-dispatch-table
  technique already proven for weapnext/D-pad/crouch — see issues #4/#9)
  and confirm whether it's a distinct command from `+attack` or the same
  one gated by an unmet mortar-specific condition.
  **Research update (2026-07-18, task #26):** confirmed via a fresh
  `sp_warlord.ff` zone dump + GSC decompile that the mortar entity type is
  literally named `bog_mortar` internally (a real dev-codename match
  confirming this is the right mission's zone) and is **deliberately
  excluded from the generic vehicle-init/fire pipeline** — a shared
  vehicle-spawner script explicitly special-cases and skips it (`if
  (var_0.vehicletype == "bog_mortar") return;`), so it never gets the
  normal per-vehicle dispatch every other drivable/mountable vehicle in
  this zone gets. The mortar's own fire-control script itself was NOT
  located among the 26 decompiled scripts in this zone — likely
  hash-named with no distinguishing string to grep for. **Do NOT assume
  today's `+attack` kbutton rewrite (issue #29) already fixes this** —
  real evidence against that assumption: the turret (same mission,
  different mount) already fired correctly under the OLD raw-usercmd-bit
  Fire, meaning turret and mortar are demonstrably NOT using the identical
  fire mechanism (otherwise both would have failed identically before
  today's change). A live re-test is still needed either way, but go in
  expecting mortar fire to remain broken, not assuming a free fix.
  **Turret-polling hypothesis tested and NOT supported (2026-07-18):**
  checked whether mortar shares the turret/sentry killstreak's real
  `usebuttonpressed()`/`attackbuttonpressed()` polling mechanism instead
  — zero hits for either builtin anywhere in `sp_warlord.ff`'s 24 real
  scripts, and turret's own polling function (`1558.gsc`'s `_id_3CBE`)
  turned out to be killstreak-sentry-specific (`maketurretsolid()`/
  `setmode("sentry")`), not a generic mounted-weapon abstraction mortar
  could share — a full 317-file corpus sweep found no such shared system
  anywhere. `bog_mortar` is confirmed excluded from the vehicle
  CLASSIFICATION system entirely (both its appearance-init and its
  per-vehicletype dispatch-table builder), not just steering — consistent
  with it being some other kind of entity, but its real handling script
  remains unlocated.
  **Important, unresolved mission-identification flag, found as a side
  effect (2026-07-18): `sp_warlord.ff` may not actually be "Back on the
  Grid."** This zone's own content includes
  `aitype/ally_hero_price_africa.gscbin`/`ally_hero_soap_africa.gscbin` —
  Price and Soap set in AFRICA — which doesn't fit "Back on the Grid"'s
  real setting (Yuri's Dubai flashback framing story, per the mission
  content already confirmed in `dubai.ff` for task #27's turret research:
  Makarov chase, elevator ambush, restaurant collapse, Dubai skyline).
  `sp_warlord.ff`'s own zone name plausibly matches "Turbulence" instead
  (Yuri/Soap investigating an African warlord aboard a hijacked plane —
  issue #27 bug #4's mission) far better than it matches "Back on the
  Grid." **If this is correct, the mortar sequence being researched under
  task #26 may actually belong to "Turbulence," not "Back on the Grid" —
  meaning `dubai.ff` (task #27's turret-regen zone) and `sp_warlord.ff`
  (task #26's mortar zone) might be two DIFFERENT missions entirely, not
  the same mission's two set-pieces as this project has been assuming.**
  Not resolved either way — needs either a live check (what mission name
  actually displays for the mortar sequence, or which allies are present)
  or a deeper zone-content cross-reference before treating either
  attribution as settled. Flagging prominently since it could mean task
  #27's own "Hypothesis B refuted" conclusion targeted the wrong mission's
  content.
  **RESOLVED, decisively (2026-07-18, follow-up targeting-system pass):
  `sp_warlord.ff` is CONFIRMED NOT "Back on the Grid."** Freshly dumped
  the zone in isolation and read its real map-entity file
  (`maps/sp_warlord.mapents`, 329 lines) directly: actual placed entities
  are `script_vehicle_mi17_africa` (Mi-17 helicopters),
  `actor_enemy_africa_militia_AK47`, `technical_rider_stealth_function`,
  `vehicle_pickup_technical` — unambiguous African-militia/helicopter/
  technical-vehicle content, zero mortar entities, zero turret entities,
  zero Dubai-consistent content anywhere in the actual map data. This is
  direct entity-placement evidence, not just a name coincidence — confirms
  `sp_warlord.ff` is almost certainly "Turbulence" (the African-warlord
  mission), not "Back on the Grid." **A targeting/placement-system search
  (mortar/shell/impact/elevation/bearing/indirect/artillery/
  beginlocationselection/trajectory/ballistic, full corpus) found nothing
  relevant in `sp_warlord.ff` for the same reason: the mortar was never
  there to find.**
  **Also checked `dubai.ff`** (the zone independently confirmed as the
  REAL "Back on the Grid" via Yuri/Makarov/restaurant-collapse content,
  used for task #27's turret-regen research) with the same keyword sweep
  — **also zero mortar hits.** `dubai.ff` genuinely has no mortar content
  either.
  **Bottom line: the mortar/turret sequence has never actually been
  located in ANY zone this project has dumped.** Both prior candidate
  zones are now ruled out with direct evidence, not absence-of-string-
  match: `sp_warlord.ff` is a different mission's content entirely, and
  the REAL "Back on the Grid" (`dubai.ff`) has no mortar/player-turret
  content in it either — meaning task #27's earlier "Hypothesis B
  refuted, no turret-specific regen logic exists" conclusion was drawn
  from a zone that may not even CONTAIN the turret sequence it was
  searching for, casting real doubt on that conclusion. **Task #27
  reopened** (was marked completed) pending re-identification of the
  correct zone. **Needed before any further mortar/turret research can be
  productive**: identify the real mission fresh, either via a live
  in-game check (mission name, which allies are present during the
  mortar/turret sequence) or by dumping the remaining untried Campaign
  zones (`sp_paris_a/b`, `sp_ny_harbor`, `sp_ny_manhattan`, `sp_prague`,
  `sp_payback`, `sp_intro`) and checking their `.mapents` files the same
  direct way this pass used to rule out `sp_warlord.ff`.

  **RESOLVED (2026-07-18, dedicated zone-identification pass): the real
  mission is GOALPOST, not "Back on the Grid" — a mission-attribution
  error carried across multiple prior sessions.** All 7 remaining
  untried `sp_*.ff` loader zones were dumped fresh — zero mortar/turret
  hits, several turned out to be near-empty thin-loader stubs (their
  real content lives in separately-named zones, same pattern already
  known for `sp_dubai.ff`/`sp_berlin.ff`). Widened the search to
  un-prefixed real-content zone names (`castle`, `hamburg`, `hijack`,
  `innocent`, `london`, `paris_ac130`, `prague_escape`, `roundtrip`,
  `warlord`, `payback`) and found a decisive hit in **`hamburg.ff`**:
  - `745.gsc` — a real `level._effect["mortar"][...]` impact-FX table
    (`bunker_ceiling`/`dirt_large2`/`mud`/`water`/`concrete`/`dirt`,
    pointing at `beach_impact_hamburg`/`big_hamburg_river_blowup` FX
    assets), confirmed to originate specifically from `hamburg.ff`, not
    shared content.
  - `32281.gsc` (`_id_7DF4`–`_id_7DF7`) — a real, player-operable mounted
    turret: `spawnturret("misc_turret", ..., "minigun_m1a1_player_tc")`,
    `setmodel("weapon_m1a1_minigun")`, `level.player
    disableturretdismount()`, `level.player playerlinktodelta(...)`
    (rides a vehicle-linked mount via `maps\_vehicle::get_dummy()` — the
    same still-unlocated shared vehicle-utility namespace flagged
    elsewhere in this project; `hamburg.ff` is a new lead for THAT search
    too).
  - **`hamburg.ff` also contains the exact real T-90/SMAW assets already
    tracked for Goalpost** (`weapons/smaw_nolock`, `hud_icon_smaw`,
    `viewmodel_smaw_reload`, `vehicle_t90_tank_woodland*`,
    `weapon_dshk_turret_t90`) — confirming the mortar/turret sequence and
    the tank/SMAW sequence are in the SAME mission (Goalpost), not two
    separate missions.
  - **Not yet reconciled**: `compatibility_matrix.md` separately notes
    Goalpost was "played and fully fine on controller" from live
    playtest — likely that note was based on the tank/SMAW portion
    specifically, without realizing the earlier mortar/turret portion
    (previously misfiled under "Back on the Grid") is the same mission.
    Needs a live check to confirm, not assumed either way.
  - **Not yet reached**: the mortar effect table's consuming function
    (would confirm player-fired vs. ambient enemy shelling); the actual
    mortar fire-control script within `hamburg.ff`'s 71 scripts (only
    searched by keyword, not by content, this pass); whether Goalpost has
    a genuinely separate "village mortar" set-piece distinct from its
    tank-defense sequence.
  - **`dubai.ff` and `sp_warlord.ff`/`warlord.ff` are now conclusively
    ruled OUT** as the mortar/turret zone, with direct entity/asset
    evidence, not just absence-of-keyword-match — see the earlier entries
    in this section for that trail.
  - **REOPENED (2026-07-20) — the "Goalpost" attribution above is very
    likely WRONG, contradicted by external evidence.** A research fork
    dumped `hamburg.ff` fresh and decompiled all its named + hash-named
    scripts: **`bog_mortar` does not appear anywhere in `hamburg.ff`** —
    zero hits. That string's origin traces back to `sp_warlord.ff`, which
    this project's own research (right above, and issue #27 bug #4) has
    since separately confirmed is actually "Turbulence," not the mortar
    mission — meaning the original `bog_mortar` finding this whole
    Goalpost attribution was partly built on came from the wrong zone and
    appears to have never been re-verified against `hamburg.ff` directly.
    The only mortar content actually IN `hamburg.ff` is a purely
    ambient/scripted enemy-bombardment system (`maps\_mortar::`,
    timer-triggered via `delaythread`, `"mortar_incoming"` warning audio,
    impact-FX only) — no player fire input anywhere in it, nothing for a
    controller hook to reach. **Independent, decisive external evidence,
    user-supplied (2026-07-20):** a real, publicly-verifiable MW3
    achievement/trophy guide ("For Whom the Shell Tolls" — a real MW3
    achievement, 4-shells-only mortar challenge) explicitly states this
    sequence happens in **"Back on the Grid,"** matching the mission
    attribution this project had BEFORE the 2026-07-18 "correction" to
    Goalpost, not after. **Most likely real explanation, not yet
    confirmed**: `dubai.ff` (independently confirmed as the genuine "Back
    on the Grid" zone via Yuri/Makarov/restaurant-collapse content) was
    checked for mortar content and came back negative — but `dubai.ff`'s
    own script names (`dubai_finale.gsc` in particular) suggest it may
    only be the BACK HALF of the mission. CoD campaign missions routinely
    span multiple sequentially-loaded zone files; an earlier
    village/mortar set-piece could live in a still-undumped zone that's
    part of the SAME mission as `dubai.ff`, not a separate mission
    entirely. **Net effect: the mortar/turret sequence is most likely
    still genuinely in "Back on the Grid" after all — the Goalpost
    attribution should be treated as unconfirmed/likely wrong until a
    live in-game mission-name check settles it, and task #26/#27 should
    NOT proceed on the Goalpost premise.** Needed before further RE:
    either a live check (what mission name actually displays during the
    mortar/turret sequence), or dumping whatever zone(s) load
    sequentially before `dubai.ff` within the same mission.
- **Bug #6 (NOT YET CONFIRMED, two competing hypotheses) — MISSION
  CORRECTED 2026-07-18 (was "Back on the Grid," really "Goalpost," same
  mission as bug #5 above — see task #26/#27's zone-identification entry):
  the mounted Browning M2 turret sequence on the captured technical
  (holding off waves of enemies/other technicals) felt far too hard on
  controller.** User's own framing, explicitly uncertain
  between two distinct causes, not yet diagnosed:
  1. **Hypothesis A — no aim assist.** This project's aim assist is
     already a known, confirmed-non-functional, parked gap (task #16 —
     "disabled for public builds," not just unpolished). A sustained,
     high-enemy-density defense sequence like this is exactly the kind of
     moment where the absence of aim assist would be most keenly felt vs.
     mouse precision. Given task #16 is ALREADY confirmed broken, this is
     the higher-prior-probability explanation of the two.
  2. **Hypothesis B — a real CoD-series "mounted turret" survivability
     buff isn't getting set when mounting via controller. Refined by the
     user (2026-07-18): more likely FASTER HEALTH REGENERATION while
     mounted, not flat damage reduction/max-health** ("i think it was
     faster regen not health if i remember" — explicitly hedged, not
     certain, but a real, precise refinement worth preserving as stated
     rather than the earlier, less specific "tankiness" framing). Real,
     distinct, separately-checkable possibility, NOT just a fallback
     guess — CoD titles have historically buffed player survivability
     while manning a mounted turret, precisely because these sequences are
     designed to feel like short, turret-vs-horde standoffs, not a fair
     1:1 firefight; a faster/more-aggressive regen rate (rather than a
     damage multiplier) is a plausible, distinct mechanism for that same
     design goal. If our controller mount/interact path (X = `+activate`,
     per the confirmed keybind table) doesn't trigger whatever the real
     engine normally sets alongside a keyboard-driven mount, the player
     would regen at the normal rate during a sequence balanced around
     regenerating faster — independent of and additive to hypothesis A,
     not mutually exclusive. **Practical consequence for diagnosis**: look
     for a real regen-RATE field/flag/timer (e.g. a shortened regen-delay
     or an increased regen-per-tick value gated on "is mounted," analogous
     in shape to this project's own Sprint stamina/cooldown timer work in
     issue #6) rather than a static max-health or damage-taken multiplier —
     changes where to look, not just what to look for.
  - **Not yet diagnosed which (or both) is the real cause.** Natural next
    steps: (a) check live whether Hypothesis A alone plausibly explains the
    difficulty (informal — does it feel like a normal aim-assist-less
    firefight, or does damage taken feel unusually high even when landing
    hits reliably); (b) if suspicion remains on Hypothesis B, live-diff a
    real health/damage-multiplier field or entity flag between a keyboard-
    mounted turret session and a controller-mounted one, the same memdiff
    technique already used elsewhere in this project. Don't attempt a fix
    before at least one hypothesis is confirmed — could easily spend effort
    on the wrong one.
  - **User's own live impression, for the record**: "it did feel a bit
    different" (comparing controller-mounted vs. presumed keyboard-mounted
    turret feel) — a genuine, if informal, data point in favor of
    Hypothesis B being at least partly real, not just theoretical. **User
    has explicitly flagged this for a dedicated internal deep-investigation
    + RE pass** (not to be started opportunistically alongside other work —
    tracked as its own task, see task #27).
  - **RESOLVED, task #27, 2026-07-18: Hypothesis B REFUTED, Hypothesis A
    stands as the explanation.** Dumped the real mission zone fresh
    (`dubai.ff` — NOT the thin `sp_dubai.ff` loader zone an earlier session
    dumped, which only contains a 2-script wrapper) and decompiled all 4
    real scripts (`dubai.gsc`, `dubai_code.gsc`, `dubai_finale.gsc`,
    `dubai_utils.gsc`). The real regen-buff mechanic Hypothesis B predicted
    DOES exist in this exact mission
    (`level.player._id_20F2.playerhealth_regularregendelay`, manipulated in
    two scripted set-pieces: an elevator/helicopter-gunship ambush and the
    restaurant-collapse sequence — both genuine "player is briefly made
    tankier" moments) — but it is **not applied to the turret sequence
    anywhere in this mission's own scripts**. No turret-specific damage/
    regen/invulnerability logic exists in `dubai.ff` at all; the only
    turret-shaped references found are an enemy helicopter minigun and an
    unrelated AI spotlight-turret utility. **This converges with task #25's
    new `cmd+0x3e`/`0x3f` finding (issue #30)**: the turret most likely just
    suffers from the same missing-aim-precision-channel issue as DPV aim/
    mortar fire/missile guidance (no aim assist + this project's controller
    hooks never populate the real mounted-aim byte pair), not a missing
    health mechanic — Hypothesis A. **One unresolved lead, not chased down
    this pass**: `maps\_vehicle::_id_2A12()` is called from
    `dubai_code.gsc` but is a shared vehicle-utility script
    (`_vehicle.gsc`) that was never located/decompiled (not in the already-
    dumped `common.ff` output; likely among ~295 undumped `common.ff`
    scripts or in `code_pre_gfx.ff`/`code_post_gfx.ff`) — if the turret
    uses a GENERIC "vehicle mounted-weapon position" system rather than
    mission-specific scripting, any damage-reduction logic for that shared
    system would live there, still unfound. Task #27 closed as resolved
    **UPDATE (2026-07-18, dedicated hunt): still not found — genuinely
    absent, not just unchecked.** Fresh, isolated dumps (bypassing the
    shared `zone_dump/` folder, confirmed unreliable for "is X present"
    questions since it's a merged/overwritten mix of many past sessions)
    of `common.ff` (188 real scripts, complete), `code_pre_gfx.ff`/
    `code_post_gfx.ff`/their `_mp` variants (empty/placeholder-only),
    `dubai.ff` (fresh, complete script list), `sp_dubai.ff`, and
    `patch_specialops.ff` all confirmed absent of any `_vehicle` script.
    **Real, useful finding despite the negative result**: fresh
    `dubai_code.gsc` decompile shows `maps\_vehicle::` is a genuinely
    richer API than the one call previously logged — real call sites
    include `_id_2881(<vehicle-targetname-string>)` (a "get vehicle entity
    by name" accessor), `_id_2A12()` (called 5x, no args) and `_id_2A13()`
    (1x) — plausibly a mount/dismount or enter/exit pair given the count
    asymmetry — plus `_id_1F9E`/`_id_29C8`/`_id_29D8`/`_id_29DA`/`_id_29E4`/
    `_id_29E7`/`_id_2A3D`/`_id_2A3E`. Since these resolve to a literal
    `maps\_vehicle` path string (not a hash) in the decompile, the
    compiler's import table genuinely names this file — it's a real
    compiled asset somewhere, just not in any zone checked so far.
    **Recommended next zones to check, not yet attempted**: MP-side common
    zones (vehicles are far more central to actual Multiplayer's design —
    if this file is shared with MP rather than SP-exclusive, it likely
    lives there), Spec Ops mission zones (`so_*`, 15 real ops, entirely
    untouched by this project so far), or other Campaign zones with
    confirmed vehicle content (`sp_paris_a/b`, `sp_ny_harbor`, `sp_prague`,
    `sp_payback`, `sp_warlord`, `patch_sp_berlin.ff`).
    **UPDATE (2026-07-18, full-corpus sweep after `common.ff` was fully
    dumped): `_vehicle::` usage is far larger than previously known — 28+
    real call sites across 11 scripts** (`102/1354/1357/1362/1384/1387/
    1556/1560/1561/1564/1566.gsc`), confirming it's a large, generic
    entity-utility library (helicopters, airdrops, UAV/missile props,
    littlebird crash FX), not something specific to "Back on the Grid."
    Two hits directly tie it to already-open threads: `1560.gsc:109` calls
    `maps\_vehicle::_id_2A99("remotemissile_uav")` (links `_vehicle::` into
    the Predator Missile system), and `1564.gsc:1760,2132` — the exact
    script implementing `friendly_support_delta`/`riotshield`'s spawn
    logic (issue #31) — calls `_id_2A12()` twice, the SAME function
    `dubai_code.gsc`'s ambush helicopters use. Its own defining source is
    still not located anywhere in the corpus checked so far. **Separately,
    `mountvehicle()`/`dismountvehicle()`'s wrapper functions (`65.gsc`'s
    `_id_2819`/`_id_281A`) were confirmed to have ZERO literal-name callers
    anywhere in the full 317-file corpus**, despite clearly being real,
    used code — meaning they're almost certainly invoked via an indirect/
    function-pointer call pattern (this GSC dialect's `[[ ... ]]` syntax)
    that a literal-name grep can't find. Worth a follow-up search
    specifically for that pattern before concluding the player-mount
    trigger is unreachable via GSC search. Also ruled out this pass: no
    shared player-operated mortar script exists in `common.ff` (`1364.gsc`
    is a real mortar-barrage system, but ambient/enemy-controlled, not the
    player's own "Back on the Grid" mortar); no sibling
    `beginlocationselection`-family builtin used anywhere beyond
    `precision_airstrike`'s own `1559.gsc` — it's a one-off system, not a
    shared placement-selection framework.
    for the "is it a missing regen buff" question specifically; the
    turret's actual fix is now expected to fall out of issue #30's
    `cmd+0x3e`/`0x3f` implementation work, not a separate investigation.
  - **UPDATE (2026-07-20)**: issue #30's own `+0x1094`/`0x00B36210` setter
    research (a research fork this session) came back a clean static
    negative — the setter isn't findable via Ghidra reference/call-graph
    analysis, needs a live memory-write breakpoint or memdiff during an
    actual turret-mount session instead (dynamic analysis, not static).
    Also: the same session's mortar-mission reconciliation (bug #5 above)
    found real evidence the mortar/turret sequence is likely still in
    "Back on the Grid," not Goalpost as previously corrected — if that
    holds up, this bug's own mission attribution needs the same
    re-check before any further turret-specific RE.
- **Bug #7 (mission NOT YET IDENTIFIED — user will confirm later) — Interact
  (X = `+activate`) failed to work in one specific mission moment**,
  despite working correctly everywhere else in the playthrough so far
  (per issue #11, already resolved as hold-to-interact). **User's own
  hypothesis, and a real, already-documented lead worth checking first**:
  this project's Interact is wired to plain `+activate` only, but issue #9's
  own keybind table (`iw5sp.md`) already found a SEPARATE, real bit
  (`0x8`, struct offset `+0x18c`) mapped to `+usereload` — explicitly
  described there as "combined use/reload — explains why it's
  context-sensitive," distinct from generic `+activate`. If a specific
  interact prompt in the game expects that context-sensitive
  `+usereload` bit rather than plain `+activate`, our X-button hook would
  correctly handle every normal "use" prompt (matching the otherwise
  100%-working track record so far) while missing whichever specific
  prompts are actually gated on `+usereload` instead.
  **Mission/moment CONFIRMED (2026-07-18): "Mind the Gap" (London) — the
  tank sequence where a car lands on the roof and the player must press F
  (`+usereload`, per the bind table above — NOT the plain "F = interact"
  most prompts use) to exit the tank.** Controller did not react to this
  specific prompt at all; keyboard F was required. This is a strong,
  concrete confirmation of the `+usereload` hypothesis above — exiting a
  vehicle is exactly the kind of context CoD's engine family
  historically overloads onto a combined use/reload-style bind rather
  than plain `+activate`. Tracked as task #28.
  **Research update (2026-07-18, task #28):** confirmed a real, generic
  `dismountvehicle()` GSC builtin exists (script `65.gsc`, function
  `_id_281A()`, paired with `mountvehicle()` in `_id_2819()`) — the
  underlying exit mechanism is real and confirmed to exist as a callable
  GSC builtin. **However, this fork could not conclusively identify which
  zone file contains "Mind the Gap" itself** to trace its specific
  exit-prompt trigger back to this builtin — `sp_berlin.ff` was tried as a
  best guess but its contents (a `"tanker_explosion"` FX, a near-empty
  3-script zone) look more consistent with "Turbulence" than a full London
  combat level; inconclusive, not confirmed either way. **Needs a
  follow-up pass with correct zone identification** (cross-check
  `iw5sp.md`'s zone catalog against real mission order, or dump the
  remaining untried zones: `sp_paris_a/b`, `sp_ny_harbor`,
  `sp_ny_manhattan`, `sp_prague`, `sp_payback`) before the real
  `+usereload`-vs-`dismountvehicle()` connection can be traced to a fix.
  **ZONE IDENTIFIED (2026-07-20, research fork): `london.ff`, confirmed
  with strong evidence, not a guess.** All six previously-untried
  candidates (`sp_paris_a/b`, `sp_ny_harbor`, `sp_ny_manhattan`,
  `sp_prague`, `sp_payback`) were dumped and checked directly — none
  matched (`sp_ny_harbor`/`sp_ny_manhattan` are Sandman/Delta Force
  Manhattan content, `sp_paris_a` is AC-130-related, the rest are
  near-empty thin loaders, same pattern already known for
  `sp_berlin.ff`). `london.ff` is an un-prefixed real-content zone
  (matching the established naming pattern, same as `hamburg.ff` for
  Goalpost) containing `london`/`london_docks`/`westminster` script
  families. Decisive confirmation: `london_docks_code.gsc` (3917 lines)
  contains a literal `var_9.name = "Sgt. Burns"` cinematic name-tag
  assignment — direct confirmation of Marcus Burns/SAS content, i.e.
  Mind the Gap. **Exit-trigger itself still NOT located**: all 13
  `maps/` scripts in the zone were decompiled and grepped for
  `dismount`/`usereload`/`activate(`/`exitvehicle`/`tank`/the confirmed
  `_id_281A`/`_id_2819` `dismountvehicle()` wrapper — zero hits except
  one unrelated `disableturretdismount()` call in `london_uav.gsc`. No
  `.mapents` file exists for `london.ff` (unlike `hamburg.ff`), and no
  literal armored-vehicle "tank" asset appears in the zone's own
  images/materials/xmodels either (only SAS van, police van, UK utility
  truck, UCAV drone textures) — the propane/oxygen prop "tanks" that DID
  match are a red herring. One unchased lead:
  `animscripts/traverse/london_roof_slide.gscbin` (thematically
  consistent with "car lands on roof," but generic/reusable, not
  conclusive on its own). **Recommended next step**: a full content read
  of `london_docks_code.gsc`'s 3900+ lines (not just keyword grep), or
  check the native `FUN_00498ec0`-family prompt-binding path directly —
  the fork's own hypothesis is the tank sequence may be driven by a
  scripted trigger-volume + generic `activate` prompt this project
  already handles, with the real `+usereload` distinction happening
  purely natively rather than via any GSC-visible builtin call.
- **Positive result — Mission "Mind the Gap" (London, Canary Wharf, playing
  as Marcus Burns/SAS): the helicopter/aerial-camera sequence at the very
  start of the mission works fine on controller.** No fallback needed.
  Mission name confirmed via mission-list research; the exact "helicopter
  camera" framing wasn't independently re-confirmed beyond the mission's
  general UAV-surveillance-style opening, but the mission identity itself
  is solid. Third confirmed-working entry alongside Hunter Killer's boat
  and Persona Non Grata's UGV.
- **Positive result — Mission "Return to Sender" (Act 2, Mission 2,
  Bosaso/Somalia): the camera-based mounted gun on Nikolai's Hind
  helicopter, doing strafing runs (Yuri operating the door gun/Remote
  Turret), works fully on controller.** No fallback needed. Confirmed
  as the correct mission via mission-order research (Act 2: Goalpost →
  Return to Sender → Bag and Drag → ...), consistent with the user's own
  playthrough position. Fourth confirmed-working entry alongside Hunter
  Killer's boat, Persona Non Grata's UGV, and Mind the Gap's opening
  aerial sequence — and the first Act 2 data point, immediately before
  "Bag and Drag" where live testing paused for this reporting session.
- **Bug #8 — Mission "Goalpost" (Act 2, Mission 1, Hamburg): the SMAW
  rocket launcher failed to lock onto an aircraft target on controller.**
  Confirmed via research: SMAW genuinely has real lock-on capability in
  this engine (works like MW2's AT4/BO1's M72 LAW — free-fire against
  ground targets, but lock-on specifically against aircraft), and
  Goalpost's own SMAW use is documented against both T-90 tanks (ground,
  correctly dumb-fire-only, NOT a bug) and — per the user's own testing —
  an aircraft target too, where lock-on should have applied and didn't.
  **Scoped precisely to avoid over-logging**: no lock-on against the
  tanks is expected, correct behavior, not a defect — only the aircraft
  case is a POSSIBLE bug.
  **NOT YET CONFIRMED as a real bug at all — user's own follow-up
  caveat**: the helicopter target "may also be due to the heli being
  scripted." If that aircraft is a non-targetable, scripted/background
  entity (a common CoD pattern — vehicles that fly through a level for
  atmosphere without being real, lockable targets), lock-on would
  correctly fail regardless of input device, and this wouldn't be a
  controller-specific bug at all — keyboard would fail identically.
  **Two competing explanations, not yet distinguished**: (a) real
  controller-specific gap in reaching the lock-on bind (same class as
  Hold Breath/task #24 and mortar fire/task #26 — a distinct,
  non-`+attack` held-input state this project's RT hook was never wired to
  reach), or (b) the target was never lockable in the first place,
  independent of input device. **Needs a same-target keyboard comparison
  test first** (does keyboard lock onto the SAME aircraft in the SAME
  spot?) before any RE work — if keyboard also fails, this closes as a
  non-issue, not a bug.
- **Bug #9 — Predator Missile (`remote_missile`), post-fire missile-guidance
  sequence: movement breaks on controller.** Reported live: after firing,
  the sequence where the player controls the flying missile in flight
  (camera takeover, steering it to impact — shares the real UAV-control
  system per `iw5sp.md`'s GSC trace) is where movement input on controller
  breaks. Exact symptom not yet detailed (stick unresponsive vs. erratic
  vs. fighting a scripted camera) — needs a follow-up description from the
  user before further diagnosis.
  **Leading hypothesis: this is a concrete repro case for task #25**
  ("Movement hook bypasses scripted player-freeze/cinematic-lock state") —
  during missile-guidance the real player entity is presumably frozen
  (camera/control has shifted to the missile projectile, not the player),
  but this project's `InjectControllerMovement` has no scripted-freeze/
  cinematic-lock awareness and keeps forcing `forwardmove`/`rightmove`
  into the usercmd unconditionally every frame regardless of what state
  the game itself is in — the same class of bug already flagged (not yet
  fixed) for issue #4's plane-breakup sequence in "Turbulence". Distinct
  from the already-fixed stuck-prone bug (issue #10) — that was a stance
  desync bug, this is reported as movement itself breaking, not a stuck
  stance.
  **Also directly relevant to today's Fire rewiring (task #7, entry #29
  above)**: since Fire was just moved onto the real `+attack` kbutton
  specifically to fix this same killstreak's launch reliability, this
  finding should be re-checked as part of that same live-test pass — note
  whether the missile-guidance movement bug reproduces identically
  regardless of how Fire is wired (expected, since they're unrelated
  mechanisms — Fire vs. movement — but worth confirming they aren't
  secretly entangled).
  **Not yet fixed — needs task #25's general scripted-freeze detection
  work, or a narrower missile-guidance-specific gate, before a real fix
  can land.**
- More findings to be appended below as reported (further vehicles,
  killstreaks, and any other fallback points as testing continues past
  "Bag and Drag").

---

## 28. Back → real `+scores` (scoreboard/objectives) — implemented via the third key-synthesis exception (2026-07-17), documentation gap fixed (2026-07-18)

**Status:** Implemented, wired up, builds clean (0 warnings/0 errors,
verified 2026-07-18). **Not yet separately live-confirmed** — high
confidence given it uses the exact same proven technique as the two
already-confirmed exceptions (Survival ready-up's F5 synthesis, issue #5;
D-pad Left squadmate call-in's `'4'` synthesis, issue #14), but per this
project's own Production Readiness Criteria, "should work by the same
mechanism as two already-confirmed cases" is not the same as confirmed —
this entry stays open until it is.

**What it does:** `InjectControllerScoreboard()`
(`analog_input_hooks.cpp`) synthesizes a real `WM_KEYDOWN`/`WM_KEYUP` for
`VK_TAB` via `PostMessageA` at the game's own window, mirroring real
keyboard TAB exactly (`bind TAB "+scores"`, confirmed real in
`players2/config.cfg`) — hold-through-passthrough, not tap/toggle, since
`+scores` is itself a real hold-to-show bind. Default physical mapping:
Xbox Back button (`g_buttonMap.scoreboard = PhysicalInput::Back`,
`mod_config.h`), remappable like other buttons (task #15).

**Why key synthesis instead of a real native call**: the previous, reverted
attempt (see the dead-end record directly above this entry, and the
now-superseded comment block still kept in `analog_input_hooks.cpp` per
this project's "document dead ends" standard) wired `FUN_00438710`'s
dispatcher with a case number computed by trusting a bind-name-table index
as if it were the switch's real case numbering — the same mistake already
flagged as a standing lesson after weapnext's own correct resolution.
That regressed live (hit `+back`'s real kbutton instead, made the player
walk backward). The live-raw-keycode-table technique that correctly
resolved weapnext/D-pad doesn't apply cleanly here either, since `+scores`
isn't a per-frame usercmd kbutton at all — it's a plain keyboard bind read
directly by the scoreboard/objectives overlay UI, the same category of
problem the two other key-synthesis exceptions already existed to solve.

**Scope/behavior notes**: in Campaign this shows the real
scoreboard/mission-objectives overlay; Survival has no native scoreboard at
all (confirmed this session — see the compatibility-matrix/project-memory
note on Back's Campaign-vs-Survival scope split), so holding Back in
Survival is expected to do nothing visible, not a bug. Back has no other
current meaning in this project (confirmed unused elsewhere), so there's no
dual-purpose-button conflict to manage.

**Documentation gap, now fixed**: this implementation existed and was
fully wired up as of 2026-07-17, but was never reflected in this file's own
"first of two exceptions" summary (now corrected above to "first of
three"), `README.md`'s control-map table (which still said "unassigned,
not yet implemented" until 2026-07-18), or the live task list (task #5,
which stayed "pending" the whole time). Root cause of the gap not
independently diagnosed — flagged here so the same class of drift is
easier to catch earlier next time: a feature's implementation landing in
source doesn't mean its cross-referenced docs update automatically, and
this project has now hit this exact gap twice in one session (see also
issue #22's stale slider-adjustment claim, corrected below).

## 29. Fire (RT) rewired off the raw usercmd bit onto the real `+attack` kbutton — Predator Missile launch CONFIRMED WORKING via the `"n 1"` delivery-index fix (2026-07-18/19, heading corrected 2026-07-19 — see below for the full chain; the kbutton-alone hypothesis was refuted first, then superseded by the real fix)

**Status:** Implemented, builds clean (0 warnings/0 errors), and now
live-tested by the user. **Result: half confirmed, half refuted.**
1. Regular gunfire — **CONFIRMED no regression.** Shooting still works
   normally after the switch off the raw usercmd bit onto the real
   `+attack` kbutton. The "safe because `FUN_0057dc90` re-derives the
   same bit from the real kbutton every frame" reasoning below held up.
2. Predator Missile launch — **CONFIRMED STILL BROKEN**, unchanged from
   before this fix. The standing hypothesis ("raw usercmd bit doesn't
   reach whatever native code fires `notifyonplayercommand`, but a real
   kbutton_t `KeyDown` call will") is **REFUTED** — calling the real
   kbutton_t directly was not sufficient. This means
   `notifyonplayercommand`'s actual native trigger point is NOT inside
   `FUN_0057d1c0` (the kbutton KeyDown function) itself, or isn't reached
   by calling it directly the way this project does (vs. however a real
   keypress reaches it). The kbutton-level fix stays in the codebase
   (it's real, correct, and gunfire depends on nothing regressing by
   reverting it) but does NOT close task #7's Predator Missile case.

**Next step (SUPERSEDED, 2026-07-18 research pass — the string-based
search direction below was a dead end, reframing required):** the three
candidates originally listed here (find a bind-name-string-based generic
dispatch step, a table-walk-by-name hook, or revisit `VM_Notify`/
`SL_GetString`) were investigated and the underlying premise is now
refuted. **Decisive finding: the literal strings `"notifyonplayercommand"`
and `"playercommand"` do not exist ANYWHERE in `iw5sp.exe`'s static data**
— a full raw byte-level scan of every memory block (not just
Ghidra-defined strings), zero occurrences. A full decompile of the entire
input chain (`FUN_00541020` → `DAT_00a98e4c` → `FUN_00438710` →
`FUN_0057d1c0`/`FUN_0057d200`, all fully decompiled this pass, not just
the previously-documented excerpts) confirms it is purely numeric with
**zero bind-name-string logic anywhere** — no call in this chain takes a
string argument or resembles a notify dispatch. A sweep of all 95 callers
of `FUN_004895b0` (the general native→GSC notify dispatcher already known
from weapon_fired/damage) found none living in the input-handling code
region either.

**Conclusion: there is almost certainly no native "keypress pushes a
notify" trigger to find at all** — same architecture already confirmed
for `hasperk` elsewhere in this project (GSC builtins resolve to opaque
numeric method IDs at GSC compile time, zero string trace natively).
`notifyonplayercommand` is very likely a GSC-VM-internal builtin: GSC
bytecode itself polls a bind's down/up state via a generic, numeric-ID-
keyed intrinsic, rather than the engine pushing an event out synchronously
on keypress.

**Polling-frequency hypothesis RULED OUT (2026-07-18, user-confirmed prior
live experience): holding Fire for a long duration still does not launch
the missile.** This rules out "our edge-triggered KeyDown/KeyUp pair
doesn't stay down long enough for a slow GSC poll to catch it" as the
explanation — a real held press, which this project's `CallKbuttonDown`/
`CallKbuttonUp` pair genuinely produces for the full hold duration (same
mechanism ADS/Reload already prove works correctly), still doesn't
trigger the launch even given ample time for any plausible poll rate to
observe it. **This means the real kbutton_t this project writes to is either
never read by whatever GSC-VM intrinsic backs `notifyonplayercommand`, or
is read but some other precondition is unmet.**

**Bytecode-level trace, real progress (2026-07-18):** using `gsc-tool`'s
own open-source engine tables (`github.com/xensik/gsc-tool`,
`src/gsc/engine/iw5_pc_meth.cpp`/`iw5_pc_func.cpp`), confirmed
`notifyonplayercommand` (entity-scoped) compiles to **method ID `0x82A5`**
(distinct from `notifyoncommand`'s bare/global function ID `0x00D`,
consistent with the two-builtin split found separately in issue #31) and
`OP_CallBuiltinMethod2` (**opcode `0x8D`**, the 2-argument builtin-method
call, matching `notifyonplayercommand`'s real `(eventName, bindName)`
signature). Parsed `1555.gscbin`'s real container format (`name\0` + 3
length fields + zlib-compressed header + raw bytecode) directly from the
retail file and extracted the raw bytecode. **Decisive, byte-level
confirmation**: the exact byte sequence `8D A5 82` (opcode + little-endian
method ID) appears **7 times** in the real compiled bytecode, at offsets
matching the known source-level `notifyonplayercommand` call sites (lines
336, 1302-1313). This is the first concrete proof this builtin call is
findable and identifiable at the bytecode level, not just a theoretical
GSC-VM-internal hypothesis.

**Not yet reached: the native dispatch table itself.** Finding the GSC
bytecode interpreter's main opcode-dispatch loop in `iw5sp.exe` (its
`case 0x8D:` handler specifically) — which would reveal how method ID
`0x82A5` resolves to an actual native function pointer — needs locating
that interpreter loop from scratch in Ghidra (a large switch/jump-table
function with 100+ cases across the known opcode range). Not yet
attempted. **One flag worth noting for whoever continues**: `0x82A5`
(33445) is unusually large for a small bounded set of engine methods —
may be a hash/truncated-hash of the method name rather than a flat
sequential enum (a pattern seen in some later CoD engine generations),
which would mean the native side is a hash table lookup, not simple array
indexing. Not confirmed either way — only the interpreter's actual
indexing code can settle it. **Concrete next step**: find the opcode
interpreter loop, its `0x8D` case, and the indexing/lookup formula it
uses for the embedded method ID.

**FULL CHAIN RESOLVED, FIX IMPLEMENTED (2026-07-18) — the missing piece
was a required argument, not a missing native trigger.** A dedicated
fork traced delivery all the way through with fresh disassembly:

1. `FUN_0044bb50` (recognizes the literal `"n"` command) calls
   `FUN_0053b1f0(clientId)` with ONLY the client ID — `"n"` itself
   carries no payload, it's purely a trigger.
2. **`FUN_0053b1f0` (delivery) reads `Cmd_Argv(1)`** — the token AFTER
   `"n"` in the same tokenized command, via the same real argc/argv
   globals (`0x01757218`/`0x0175725c`/`0x0175727c`) already confirmed
   used by 24-52 other real functions across the binary. If no second
   argument is present (exactly the case when only `"n"` is pushed),
   this reads a real empty-string fallback constant — which can never
   match anything.
3. **`FUN_00738683` is NOT a string hash — it's literally `atol()`.**
   Delivery parses `Cmd_Argv(1)` as a DECIMAL INTEGER, not a string, and
   compares that integer against the stored registration value.
4. **Registration (`FUN_00454a30`, via `FUN_005BC9A0`) stores
   `FUN_005330a0(bindNameStr)`** — a linear scan over a DISTINCT 81-entry
   bind-name table at `0x00929fa0` (confirmed via direct memory dump to
   be separate from the already-known 32-entry kbutton table — an easy
   conflation, verified independently), returning the table INDEX where
   the string matches. Index 0 is a deliberate placeholder/empty-string
   slot (avoids ambiguity with "not found"). **Index 1 = `"+attack"`**,
   confirmed by dumping the table directly.

**Fix**: `InjectControllerFire()`'s `PushClientCommand` call changed from
`PushClientCommand(kLocalClientIndex, "n")` to **`PushClientCommand(
kLocalClientIndex, "n 1")`** — `"1"` is the decimal index `+attack`
actually resolves to in this specific 81-entry table, which is what
delivery's integer comparison needs to match registration's stored
value. Builds clean (0 warnings/0 errors). **NOT YET LIVE-TESTED** —
this closes every gap the static trace could find, but only a real
playtest confirms Predator Missile actually launches now.

**Also added alongside this fix**: a change-triggered diagnostic
(`LogMissileGuidanceFlagDiag()`, logs to `proxy_d3d9.log`) watching the
per-client `+0x1094` dword (issue #30's third-analog-channel gate) —
its real setter couldn't be found via static scanning (a whole-binary
scalar scan found only 2 references, both reads — the setter is almost
certainly data-driven, not a fixed instruction), so the same fallback
that solved the ADS-slowdown bug applies: observe it live during an
actual Predator Missile playtest to see exactly when bit `0x80000`
(missile guidance) flips. Also watches bit `0x800` on the same dword — a
new candidate for the separate, still-unresolved "Turbulence"
moves-when-frozen bug (issue #27 bug #4), found as a side effect, not
yet chased.

**LIVE DATA IN (2026-07-18): Predator Missile now launches successfully
with the `"n 1"` fix — CONFIRMED.** User live-tested immediately after
this build: "predator missile firing does now work on controller." This
closes the launch-reliability half of task #7/#29 — the full
GSC-bytecode-to-native-delivery chain traced this session was correct
top to bottom.

**Post-fire aim/guidance: still broken, and the real diagnostic log data
REFUTES the `bit 0x80000` hypothesis.** Across the entire captured
session (149 log lines spanning the missile-guidance attempt), **bit
`0x80000` is never once set** — `FUN_0057e360`'s branch is confirmed NOT
what's active during missile guidance, contrary to the original
hypothesis. **A real, promising alternative pattern appears in the same
data**: bit `0x400000` toggles on/off repeatedly for a ~97-second stretch
in the middle of the log (`t=10567718` to `t=10663687`), visually
distinct from a separate `0x4` bit that toggles during the periods
before and after that stretch. The `0x400000` stretch's duration and
placement (bounded before/after by the `0x4`-toggling pattern, which is
plausibly just an unrelated per-frame gameplay flag like ground-contact)
makes it a strong candidate for the REAL missile-guidance indicator —
just a different bit than originally guessed. **Not yet investigated**:
what `0x400000` actually does natively (same disassembly technique
already used for `0x80000`/`0x800` — find every real xref that TESTS
this bit, trace to its consuming function). If it turns out to gate a
DIFFERENT control-mode branch than `FUN_0057e360`, that branch — not the
one already traced — is what needs the `cmd+0x3e`/`0x3f`-style fix for
post-fire aim.

**Rumble**: user reports it "didn't do anything" in this same test —
expected and correct, not a new bug. The two rumble hooks are currently
commented out (disabled after crashing game startup, see the entry
above) pending the safer `FUN_0045e320`-based reimplementation; no
rumble output at all is the correct current state.

**MAJOR FINDING (2026-07-18, full GSC deep-read of `1555.gsc`/`1554.gsc`,
not just keyword-anchored searches): pre-fire aim and post-fire guidance
use two GENUINELY DIFFERENT link builtins — likely the real root cause,
and possibly a level ABOVE where the native `+0x1094` bit investigation
has been looking.**

- **Pre-fire drone-view aiming** (`_id_3C16`, lines 804-819, confirmed
  WORKING per live playtest): links the player's view/controls to a
  delta "uavrig" entity via `playerlinkweaponviewtodelta(...)` (Survival's
  normal path) or `playerlinktodelta(...)` (an alternate branch, gated on
  `level._id_3C50` being defined). Immediately followed by
  `freezecontrols(0)` — explicitly UN-freezing controls, confirming input
  is meant to work here.
- **Post-fire missile guidance** (same function, lines 892-902, right
  after `_id_3C35` spawns the `magicbullet` projectile): `var_0 unlink()`
  (drops the pre-fire delta link) → `var_0 cameralinkto(var_10,
  "tag_origin")` → **`var_0 controlslinkto(var_10)`**. This is a
  COMPLETELY DIFFERENT builtin from the pre-fire pair — it links player
  CONTROL INPUT directly to the projectile entity itself, not to a
  delta/rig entity. **Nobody has traced `controlslinkto`'s own native
  implementation** — the ongoing search for a per-client flag bit
  (`+0x1094`, `0x80000`/`0x400000`) may be looking one level too high if
  `controlslinkto` routes input through an entirely different mechanism
  tied to the linked ENTITY rather than a per-client struct flag the
  normal per-frame orchestrator checks against. **This is now the
  single most promising lead for post-fire aim — tracing
  `controlslinkto`'s real native implementation directly should take
  priority over further `+0x1094`-bit scanning.**

**Real scripted view-angle clamp also found**: `_id_3C48()` (line 1621)
calls `self lerpviewangleclamp(0,0,0, <fov-scale>×3, <fov-scale>)` —
scheduled via `delaythread(0.1, ::_id_3C48)`, but **only in the alternate
branch** (`level._id_3C50` defined) — Survival's normal branch
(`playerlinkweaponviewtodelta`) does NOT call this. Could independently
affect aim feel/rate depending on which branch actually applies — not yet
confirmed which branch Survival vs. Campaign's "Down the Rabbit Hole"
hits.

**Confirmed: no missing third script.** `maps\_remotemissile` (referenced
from `1554.gsc`) is `1555.gsc`'s own namespace, not a separate file — all
ten `_id_3C3C`/`_id_3BE9`/etc. functions it calls are defined locally
inside `1555.gsc`. Symmetrically `1555.gsc` calls back into `1554.gsc`
via its real namespace, `_id_0612`. A complete, self-contained pair,
nothing external missing.

**Full function inventory of `1555.gsc` now catalogued** (hint gates,
radio-chatter dialogue system, weapon-change watchers dispatching into
the main sequence, a damage/force-abort watcher, HUD text helpers, three
real exit/cleanup paths, a real 12-second reload-cooldown timer, kill-
tracking for post-kill dialogue by vehicle type, the bind-registration
point, a 50ms abort-poll — checks `uav_enabled`, NOT input, contrary to
what might be assumed — the launch-origin/`magicbullet` call, on-screen
target-marker boxes, and multi-target-cycling via `attackbuttonpressed()`
polling for switching between linked squad members, likely irrelevant to
solo Predator Missile use) — full detail in `iw5sp.md` if needed for
future work, not reproduced here in full.

**Related but unconfirmed native finding (2026-07-18, `+0x1094` bit
`0x400000` follow-up)**: the direct setter for that bit on the
`+0x1094` field wasn't found (same data-driven-offset pattern as before
— scalar scanning can't find it). A CLOSELY RELATED finding turned up
instead, inside `FUN_0057de60` (the exact function this project's own
`InjectAllControllerInput` already hooks): a single byte flag at
`struct+0x30` (same per-player struct family) gates setting/clearing
**`cmd->buttons` bit `0x400000`** every frame (`OR`/`AND`-complement,
confirmed via disassembly). Same bit value, same struct family, live at
a point this project already has full access to — a plausible sibling
mechanism, but NOT proven to be the same write as whatever sets
`+0x1094`'s own bit. `struct+0x30`'s own setter wasn't traced. **Given
the same-day GSC deep-read found a more direct, better-evidenced lead
(`controlslinkto`, see above) — a follow-up fork is chasing that first;
this `struct+0x30`/`cmd->buttons` finding is logged as a fallback lead,
not the primary path forward right now.**

**RESOLVED, decisively: `controlslinkto`'s real native implementation
found, explaining exactly why `+0x1094` bit `0x80000` never fired
(2026-07-18).** `controlslinkto`/`cameralinkto` (GSC builtins, method IDs
`0x81E3`/`0x81C5`) resolve to native functions `FUN_005d7f20`/
`FUN_005d7e70` respectively (found via the same builtin-method-dispatch
technique already proven for `notifyonplayercommand` — batch-
registration table owned by `FUN_004bc320`). Decompiled
`FUN_005d7f20` (`controlslinkto`) in full:
```c
clientStruct = *(int*)(entity + 0x10c);
if (*(uint*)(clientStruct + 0xc) & 0x80000) FUN_0042b910();  // guard: already linked
*(uint*)(clientStruct + 0xc) |= 0x80000;                     // SETS THE REAL LINK FLAG
*(uint*)(clientStruct + 0x4c) = linkTargetId;
FUN_004e75c0(entity);
```
**This is bit `0x80000` at client-struct offset `+0xc` — reached via
POINTER INDIRECTION through `entity+0x10c` — a completely different,
structurally distinct field from `+0x1094` on the flat
`&DAT_00B363B0 + playerIndex*0xBE5C` array `FUN_0057e480` reads
directly.** Same bit VALUE, coincidentally, but a different address
entirely — decisively explains why the live diagnostic log never once
observed `+0x1094` bit `0x80000` set during missile guidance: **the
investigation was testing the wrong address the whole time**, not a
refutation of "there's a real flag," just of "it's at this specific flat
offset." `cameralinkto`'s own function sets a SEPARATE flag (bit `0x2` at
client-struct `+0x10`, plus link-target/entity-flag data at `+0x44`/
`+0x48`) — a second, independent mechanism likely relevant to view-angle
behavior specifically, not yet needed if the `+0xc` lead fully explains
things.

**Diagnostic hook IMPLEMENTED (2026-07-18)**: `FUN_005d7f20`'s true entry
point and calling convention were independently re-confirmed via fresh
disassembly (plain `__cdecl`, ONE stack arg — an entity HANDLE, not a raw
pointer — bare `RET`) before hooking, per this project's own "verify via
disassembly first" standard. `Hook_ControlsLinkTo` (`analog_input_hooks.cpp`)
is a pure log-and-forward: calls the real original function completely
unchanged, then independently re-resolves the SAME entity-handle
(low 16 bits = index when upper 16 bits are zero, `entity = 0x01197AD8 +
index*0x270` — this confirms the previously-PARKED `0x01197AD8`/`0x270`
entity-handle-resolution array from issue #15's aim-assist research is
real and live-used) → client-struct (`+0x10c`) → `+0xc` chain, read-only,
to log the resulting flag value to `proxy_d3d9.log`. No behavior change
at all. Builds clean (0 warnings/0 errors). **NOT YET LIVE-TESTED** —
next Predator Missile playtest should show `[controlslinkto-diag]` lines
confirming exactly when this fires and what the resolved flag value is.

**Not yet found: the per-frame READER of this `+0xc` bit** — the
functional equivalent of `FUN_0057e360` for THIS flag (which function
checks `*(int*)(*(int*)(entity+0x10c) + 0xc) & 0x80000` each frame and
redirects analog input accordingly). This is the one remaining concrete
step before a real fix can be implemented — needs an xref sweep on
`FUN_005d7f20` itself for sibling readers, or a targeted disassembly
pass over the per-frame usercmd pipeline for this exact two-hop
pointer-then-bitfield pattern. **Do not assume the destination is still
`cmd+0x3e`/`0x3f`** — that assumption was tied to the now-irrelevant flat
`+0x1094` branch specifically; the real reader (once found) may write
somewhere else entirely.

**Campaign scope confirmed, and corrected (2026-07-18):** a dedicated
research pass found `remote_missile`'s Campaign usage lives in
`rescue_2.ff` (= "Down the Rabbit Hole," confirmed via matching
diamond-mine/rescue subtitle strings) and uses the LITERAL SAME compiled
`1554.gscbin`/`1555.gscbin` scripts as Survival — byte-for-byte identical
`notifyonplayercommand` calls for equip/fire/abort. **This means a fix for
Fire's `notifyonplayercommand` reachability fixes Down the Rabbit Hole and
Survival simultaneously, not two separate problems.** One mission-specific
difference: Campaign's equip slot is `+actionslot 2`, not Survival's.
**Correction**: `killstreak_reference.md`'s earlier claim that "Black
Tuesday" also uses `remote_missile` was checked against the two best zone
candidates (`intro.ff`, `berlin.ff`) and found unsupported — neither
contains real `remote_missile` gameplay scripts (only an unused
material-asset stub in `berlin.ff`). Likely a mistaken assumption from an
earlier, non-RE-verified session. Corrected in `killstreak_reference.md`.

**BREAKTHROUGH (2026-07-18): the full chain from GSC bytecode to native
delivery is now mapped end-to-end, real addresses at every hop.** A
dedicated deep-RE pass (Ghidra, ~208 tool calls) closed off every
remaining unknown in the dispatch chain:

1. **Interpreter confirmed**: `FUN_00610a40` is the real GSC bytecode
   opcode-dispatch loop. Its `switchD_00610ab4` case group `0x8b`-`0x90`
   handles `OP_CallBuiltinMethod{0..5}` (arg count = opcode − `0x8b`) —
   `case 0x8d` sets arg count 2, matching `notifyonplayercommand`'s real
   2-arg signature exactly. Independently confirms `gsc-tool`'s public
   opcode tables against this actual retail binary.
2. **Dispatch is a flat array indexed directly by method ID, not a hash
   table** — `(**(code**)(&DAT_0184cdb0 + methodId * 4))(...)`. A
   parallel table (`DAT_0186c68c`) handles bare functions like
   `notifyoncommand`, confirmed as a separate call site with its own
   table (matches issue #31's two-builtin-family finding).
3. **Method ID `0x82A5`'s real native function found**: `FUN_005BC9A0`,
   registered at runtime via a generic registrar (`FUN_0049e190`) called
   from a batch-registration table owned by `FUN_004603a0`. Decompiled in
   full:
   ```c
   void FUN_005bc9a0(entity_handle) {
       entity = FUN_004ef2e0(entity_handle);  // -> entity struct, same array this project already uses elsewhere
       if (entity && entity[0x10c] != 0) {    // gate field
           eventName = FUN_00497530(0);       // GSC VM stack arg 0
           bindName  = FUN_00497530(1);       // GSC VM stack arg 1
           FUN_00454a30(entity[0x84], bindName, eventName);  // REGISTERS interest
       }
   }
   ```
   **This is the subscribe/registration side, not the fire side** —
   confirms `notifyonplayercommand`'s real semantics: it registers a
   listener for a future bind-press, matched later by whatever actually
   delivers the event (below). Entity gate field `entity[0x10c]` being
   zero would silently no-op this whole call — a real, not-yet-checked
   candidate precondition.
4. **Real, hard 64-entry registration cap found**: `FUN_00454a30` interns
   `(clientID, hash(bindName), hash(eventName))` into a fixed-size table
   (`DAT_017507dc` count, max `0x40`), reference-counted. **A genuine,
   independently-actionable finding**: issue #31's master survey found
   ~18 distinct `notifyonplayercommand`/`notifyoncommand` sites across
   the game — if enough are simultaneously alive in a real Survival
   session (plausible with per-instance reference counting), new
   registrations past the cap would silently fail via a fallback/eviction
   path. Worth an independent live check (count active registrations)
   regardless of what else is found.
5. **Delivery function found and traced**: `FUN_0053b1f0(clientID)` walks
   the 64-slot table, hashes a "current bind name" string, and fires the
   real GSC notify (`FUN_004605c0`) for every matching entry.
6. **Delivery is triggered by a QUEUED COMMAND STRING, not directly by a
   keypress.** `FUN_0053b1f0`'s only caller is `FUN_0044bb50`, which
   string-compares an incoming queued command against a handful of
   literals (`"n"`, `"fogswitch"`, `"mr"`, `"sl"`, `"removecorpse"`) —
   **the literal string `"n"` specifically triggers the delivery walk**;
   everything else falls through to generic server-command forwarding.
   `FUN_0044bb50` is called only from `FUN_0050a460`, which drains a real
   per-player 128-slot ring-buffer command queue every frame.
7. **The critical unresolved point, and the most likely real explanation
   for Predator Missile's failure**: the generic queue-push primitive
   (`FUN_00428a70(clientIdx, string)`) has 4 real callers. Three are
   unrelated (a debug/location command, a corpse-cleanup command, a
   generic formatted-string push). The 4th, `FUN_00528db0`, reads
   `argv[0]` of what looks like the currently-executing command (same
   memory region as this project's already-confirmed `Cmd_ExecuteString`
   argc/argv machinery) and **only enqueues `"n"` when that token does
   NOT start with `'-'` and does NOT start with `'+'`**. **If this filter
   genuinely excludes `+`/`-`-prefixed bind commands, `+attack`'s
   down-edge would never reach the queue at all via this path** —
   meaning the notify-delivery mechanism may be architecturally
   unreachable for held movement-class binds like `+attack` through this
   route, regardless of how the underlying kbutton is driven (explaining
   why the earlier `CallKbuttonDown` fix, though real and correct, wasn't
   sufficient). `FUN_00528db0`'s own callers were not traced to confirm
   exactly when/how it fires for `+attack` specifically — genuinely
   unresolved, not just unattempted.

**Two concrete, independently-actionable next steps, not mutually
exclusive:**
- **(a) Implement a direct workaround**: since `FUN_00428a70(clientIdx,
  "n")` is a real, callable native function (not OS-level input
  emulation — a direct internal engine call, arguably even more "native"
  than the key-synthesis exceptions already in this project), this project's own
  Fire hook could call it directly on Fire's down-edge, bypassing
  whatever gate normally would have pushed `"n"` for `+`-prefixed binds.
  Low-risk, purely additive (doesn't remove or change the existing
  `CallKbuttonDown` call), easily revertible.
- **(b) Independently verify the 64-entry registration cap isn't itself
  the blocker** — a live diagnostic counting active `notifyonplayercommand`/
  `notifyoncommand` registrations during a real Survival session would
  settle this regardless of (a)'s outcome.

**(a) IMPLEMENTED (2026-07-18), calling convention independently
disassembly-confirmed first.** Before wiring any call to `FUN_00428a70`,
a dedicated fork confirmed its exact calling convention via raw
disassembly (not just decompiled pseudocode, since a wrong convention
risks a crash, not just a silent no-op): plain `__cdecl(int clientIdx,
const char* str)`, both args on the stack, no register tricks. `str` is a
raw C string copied via a bounded `strncpy`-style call into a 64-byte
ring-buffer slot — no interned-string handle required. No lock/mutex.
Overflow past the 128-slot ring buffer logs a warning but still enqueues
(wraps), not a hard failure. **Also corrects a misreading from the
earlier trace**: `FUN_00528db0` does NOT hardcode the literal `"n"` as
originally reported — it forwards whatever command is currently
executing (`argv[0]`), filtered to exclude `+`/`-`-prefixed tokens. This
doesn't change the plan (this project calls `FUN_00428a70` directly with its
own literal `"n"`, bypassing `FUN_00528db0` and its filter entirely), but
the earlier belief that `FUN_00528db0` itself hardcodes `"n"` was wrong
and is corrected here.

`InjectControllerFire()` (`analog_input_hooks.cpp`) now calls
`PushClientCommand(kLocalClientIndex, "n")` on Fire's down-edge only
(matching a real one-shot command dispatch, not spammed every frame
Fire is held), additive alongside the existing `CallKbuttonDown` call —
not a replacement, isolated and easily revertible if wrong. **Gated
behind a new `[Experimental] FireNotifyQueueKick` toggle in
`mw3ncp_config.ini`** (default on) — the first entry in a new
config pattern (`mod_config.h`/`.cpp`) specifically for individually-
toggleable, not-yet-fully-proven behaviors, so a live hypothesis test
like this one can be flipped off without a recompile if it's ever
suspected of causing a problem, rather than needing a source edit and
rebuild. Entries here are meant to graduate to unconditional (and be
removed from the section) once confirmed correct and stable — not a
permanent settings surface. Builds clean (0 warnings/0 errors). **NOT YET
LIVE-TESTED** — the call itself is
confirmed safe to make (won't crash), but that says nothing about
whether pushing `"n"` this way is actually the missing piece for
Predator Missile's launch. Next live-test pass should check both: does
the missile now launch, and does anything about normal gunfire/gameplay
regress from pushing `"n"` into the command queue every time Fire is
pressed (no reason to expect a regression per the disassembly, but this
project's own standard is to verify, not assume).

**Original hypothesis note (superseded by the live-test result above,
kept for the record):** this touches the single most-used input in the
entire project (every shot fired, in every mode), so per this project's
Production Readiness Criteria it stayed open until confirmed both ways,
not just "should work by the same mechanism as ADS/Reload."

**Why:** task #7's full GSC trace (issue #26) found `remote_missile`
(Predator Missile)'s launch is gated behind
`notifyonplayercommand("launch_remote_missile", "+attack")` — a
native↔GSC bridge that fires on real bind/command dispatch, not on raw
`usercmd_t` buttons bits being set directly. Fire (RT) has always been
implemented as a raw-bit force (`cmd->buttons |= 0x1`), bypassing the
real `+attack` kbutton entirely — the standing, unconfirmed hypothesis
for why the missile's camera/view works (generic UAV control, not
notify-gated) but launch doesn't reliably fire.

**What changed:** `+attack`'s real kbutton_t address was already sitting
in `iw5sp.md`'s existing "Kbutton table position ↔ usercmd.buttons bit
correlation" table from 2026-07-14 (struct base `0x00A98AD8` + offset
`0x128` = `0x00A98C00`, table idx 0, first entry of the same 10-entry/
stride-`0x14` array `FUN_0057dc90` itself reads every frame) — same
struct family as ADS's kbuttons (`0x00A98B8C`/`0x00A98CB8`) and Reload's
(`0x00A98C68`), so the existing `CallKbuttonDown`/`CallKbuttonUp` helpers
(already proven live for both) apply directly, no new calling-convention
work needed. Added `InjectControllerFire()` (`analog_input_hooks.cpp`),
edge-triggered exactly like `InjectControllerAds`/`InjectControllerReload`,
and removed the raw-bit force from `InjectControllerButtons` — a full
replace, not additive, matching the precedent set by the crouch/prone
migration off raw bit-forcing onto the real `ToggleStance` call (issue
#9). `FUN_0057dc90` already reads this exact kbutton every frame and
re-derives usercmd bit `0x1` from it — the same bit our manual force used
to set directly — so ordinary gunfire is expected to behave identically,
just via the authentic path instead of a manual override.

**Live-test result (2026-07-18, both items now settled — see the status
block at the top of this entry):** regular gunfire confirmed unaffected;
Predator Missile launch confirmed still broken. The kbutton-level fix
alone was not sufficient — see "Next step" above for where to look next.

## 30. Third analog-input channel (`cmd+0x3e`/`0x3f`) discovered — likely UNIFYING root cause for DPV/mortar/turret (2026-07-18, research pass, task #25; REFUTED for Predator Missile guidance specifically, 2026-07-19 — see the correction near the end of this entry)

**CONFIRMED LIVE REGRESSION, `Hook_ControlsLinkTo`/`Hook_MissileGuidanceDispatch`
DISABLED (2026-07-19)** — these two diagnostic hooks (installed for this issue's
own guidance-phase investigation, see below) were root-caused (one or both, not
fully bisected) to a real, confirmed live bug affecting Hold Breath (issue #6) —
including, surprisingly, on PURE keyboard/mouse with zero controller involvement
at all. Both are disabled in `InstallAnalogInputHooks` (code kept, not deleted).
**This means the guidance-phase diagnostic data this issue's own "still open"
question depends on can no longer be collected** until a safer hook design is
found and re-verified — the missile-flight live data pull this issue calls for
below is blocked on that, not just on finding time to fly a missile in-game. See
issue #6's own entry for the full regression trail. `Hook_MissileGuidanceDispatch`
is the leading suspect (confirmed to run every single frame unconditionally,
unlike `Hook_ControlsLinkTo` which should only fire when an actual guided
weapon links) but this is not proven.

**Status:** Research complete, strong hypothesis, NOT implemented or
live-tested. Potentially the highest-value single finding of this session
— if confirmed, this is one fix for FOUR separately-tracked bugs, not four
independent investigations.

**What was found:** decompiling the real per-frame orchestrator
`FUN_0057e480` (the function that calls the movement/look functions this
project's own `InjectControllerMovement`/`InjectControllerLookAngles` hooks
parallel) revealed the engine has **at least three distinct control-mode
branches**, each of which REPLACES normal movement/look entirely for that
frame — and this project's controller hooks are blind to all of them, since
`InjectAllControllerInput` (hooked at `FUN_0057de60`) runs unconditionally
at the end of every branch regardless of which one fired:

1. **Menu-active** (gate `0x00B36210` bit `0x10`, same bit this project's own
   `IsMenuActive()` already reads) → `FUN_0057d3e0`, which only re-derives
   crouch/prone bits from the real stance field — no movement/look input
   at all while a menu is open.
2. **A flag at per-client-struct offset `+0x1094`, bit `0x80000`** →
   `FUN_0057e360`. This function reads real kbutton latches (including
   `+attack`'s kbutton at `0x00A98C00` — the same one today's Fire rewire
   uses) into `cmd->buttons`, re-derives stance bits, then calls
   `FUN_0057d740` → `FUN_0057d680` (**the confirmed real raw
   mouse-delta source** — not the keyboard-summed path, not this project's
   `kPitchAccum`/`kYawAccum` globals) and writes the scaled result into
   **`cmd+0x3e`/`cmd+0x3f`** — a THIRD analog-input byte pair, distinct
   from both normal look (`kPitchAccum`/`kYawAccum`) and normal movement
   (`cmd+0x1c`/`0x1d`).
3. **Bit `0x8`** on the same `0x00B36210` gate → `FUN_0057df60`. **Refined
   (2026-07-18, `precision_airstrike` research pass): this is NOT
   vehicle-specific — it's a shared mode-dispatch function**, gated on
   global `DAT_00984d50` and reading a per-player mode value at
   `0x00A99A44 + playerIndex*0xD28`. **Mode 1** computes a clamped 2D
   cursor position (`cmd+0x3b`/`0x3c`/`0x3d`) using `FUN_0057d680` — the
   SAME raw mouse-delta source normal look already consumes — scaled by a
   real, confirmed cvar `cg_mapLocationSelectionCursorSpeed`. **This is
   `precision_airstrike`'s real artillery-marker cursor-movement path**,
   confirmed via that cvar and the real `map_location_selector_arrow` HUD
   material/`confirm_location` interned event name (all found as literal
   strings in the binary — unlike `notifyonplayercommand`, this system
   is NOT a zero-string GSC-only builtin). **Mode 2** is the actual
   vehicle-driving case: just ORs `cmd->buttons |= 0x20000`. **Since
   Mode 1's cursor math reuses the exact same raw-mouse-delta function
   this project's normal look hook already feeds, it's plausible controller
   aiming during `precision_airstrike`'s placement already works
   correctly TODAY with zero new code — not confirmed, needs a live
   check** (aim during a real `precision_airstrike` buy/placement and see
   if the cursor tracks the right stick). If it does, the only remaining
   gap for that specific killstreak is the confirm/Fire-detection step
   into `confirm_location` (not yet traced — not inside `FUN_0057df60`
   itself, must be a separate check elsewhere for "Fire pressed while
   mode==1").
   **CORRECTION (2026-07-18, user-confirmed live): this cursor/
   `confirm_location` system is NOT what Survival's `precision_airstrike`
   actually uses — that theory was closer to MP's cursor-based version.**
   Survival's real mechanic, per direct playtest confirmation, is a
   smoke-grenade-style THROWN marker (aim with the look stick, throw with
   Fire, same as an ordinary grenade) — **confirmed fully working on
   controller already**, no separate confirm/placement step needed,
   since both aim and throw are inputs this project already drives correctly.
   `FUN_0057df60` mode 1 may still be real and relevant to something else
   (MP's version, or a different Campaign-only use), but is NOT the
   mechanism to chase for Survival's `precision_airstrike` specifically —
   see `killstreak_reference.md`'s corrected entry.

**Why this matters:** branch 2's `cmd+0x3e`/`0x3f` channel is a strong,
evidence-backed unifying candidate for a whole cluster of previously
separately-tracked bugs, all of which share the same shape ("aiming/
control works via the look-stick, but the OUTPUT never reaches the right
place"): DPV aiming not working (issue #27 bug #1, Hunter Killer), mortar
aim-works-fire-doesn't (issue #27 bug #5, task #26, Goalpost — corrected
2026-07-19, was misfiled as "Back on the Grid" when this paragraph was
first written), mounted-turret feeling notably harder than expected
(issue #27 bug #6, task #27, same Goalpost correction), and Predator
Missile guidance-sequence movement break (issue #27 bug #9, since REFUTED
as sharing this mechanism — see below). These were originally believed to
share the SAME root cause:
this project's controller hooks only ever write `cmd+0x1c/0x1d` (movement)
and the `kPitchAccum`/`kYawAccum` globals (look) — never `cmd+0x3e/0x3f`
— so whenever the engine switches into branch 2's mode (mounted/aim-only
contexts: DPV, mortar, turret — missile-guidance REFUTED as sharing this
mechanism, see the 2026-07-19 correction below), the controller's
right-stick input has nowhere real to land.

**Not yet confirmed:** which exact real gameplay contexts set the
`+0x1094` bit `0x80000` flag (the setter wasn't traced — would need a
cross-reference sweep) — so it's a strong hypothesis, not yet a proven
unification across all four bugs individually.

**GSC-side search attempted and came back negative (2026-07-18):** a
dedicated pass freshly dumped and decompiled the ENTIRE `common.ff` shared
zone for the first time (188 real scripts, previously never reviewed —
now part of the project's permanent decompiled corpus at
`decompiled/iw5/`, 317 files total, real, useful infrastructure for any
future GSC work beyond this specific question). Confirmed `maps\_vehicle`
is referenced by 28 scripts across this corpus but its own DEFINING
source file is absent from `common.ff` entirely (matches the separate
`_vehicle.gsc`-hunt fork's independent finding — treat as doubly
confirmed). Traced every `_vehicle::_id_2A12`/`_id_2A13` call in the real
`dubai_code.gsc` (Back on the Grid's actual mission script) and found they
ALL apply to scripted AI-controlled vehicles (ambush/collapse
helicopters) — zero GSC-level reference to the player's own manual
turret-mount sequence anywhere in this mission, no `misc_turret`/
`mounted`/`browning`-style keyword tied to player input found at all.
**Conclusion: the `+0x1094`/`DAT_00984d50` setter is very likely NOT
GSC-visible** — probably set purely natively (e.g. inside `mountvehicle()`
or a `beginlocationselection`-style builtin's own C++ implementation when
the player interacts with a specific entity classname, with no separate
GSC-side "enter aim mode" call to find). **Recommended next step: native-
side only** — find what natively CALLS `FUN_0057e360`/`FUN_0057df60`
themselves (an xref sweep on those two function addresses directly, not
more GSC searching), or trace `mountvehicle()`'s own native implementation
for where it might set these flags as a side effect of entering a vehicle.

Also unresolved: Turbulence's
"moves when should be frozen" bug (issue #27 bug #4) doesn't fit this
pattern as cleanly (that's the OPPOSITE problem — movement happening when
it shouldn't, not missing) — best candidate there is branch 1
(menu-active) or a still-unidentified freeze gate; `FUN_0057d430`/
`FUN_0057d7e0` (the real movement/look functions) themselves weren't
checked for an internal early-return that would also gate real keyboard
during a freeze. **UPDATE (2026-07-20): that specific freeze gate IS now
found — see issue #27 bug #4's own entry below. Unrelated to this
issue's own `+0x1094`/`0x00B36210` mounted-aim setter question.**

**Research fork (2026-07-20): xref sweep for the setter came back a clean
negative, static analysis exhausted for now.** Ran `FindCallers.java`
against the existing `MW3.gpr` Ghidra project for both `FUN_0057e360`/
`FUN_0057df60` — each has exactly ONE caller, the already-known
`FUN_0057e480` orchestrator itself (the branch *selection* happens via an
inline flag test inside the orchestrator, not a call from elsewhere, so
"find who calls the branch handler" was a dead end by construction).
Pivoted to a data-reference sweep on the flag's own memory
(`DAT_00b37444`, the base symbol Ghidra resolved for the `+0x1094`
per-client access) — only 3 total references found, and all 3 are READS
(`FUN_0057d7e0`, `FUN_0057e480`, `FUN_0057d430`, all already known).
Ghidra's reference resolver correctly catches this addressing pattern for
reads (confirmed it *can* find this class of access) but found zero
writes anywhere using the same base-symbol scheme. Also re-checked the
GSC builtin dispatch table dump and every other prior Ghidra project
output for `mountvehicle`/`dismountvehicle`/`beginlocationselection` —
zero hits, consistent with this dialect's builtins often being
hash-named rather than literal-string-registered (a string search can't
find `mountvehicle()`'s real native body this way). **Conclusion: the
setter is real (the branches do fire in actual DPV/turret sessions) but
isn't reachable via static Ghidra reference analysis alone** — either it
computes the per-client struct pointer via a different base/register
scheme than the one Ghidra anchored to, or it's genuinely only
discoverable dynamically. **Next step is dynamic, not static**: a live
memory-write breakpoint or before/after memdiff during an actual DPV or
turret-mount session (the same class of technique already used elsewhere
in this project, e.g. Sprint's kbutton hunt) — not more static RE time
until a live session is available.

**Bug found in passing, NOT yet fixed (2026-07-20)**: the SAME research
fork independently re-derived the `+0x1094` address for player 0 as
`0xB363B0 + 0x1094 = 0xB37444` (confirmed via two separate disassembly
sources while researching issue #27 bug #4's freeze flag, see below) —
but `analog_input_hooks.cpp`'s existing `kMissileGuidanceFlagAddr`
constant is `0xB374E4`, off by `0xA0` (160 bytes). Independently
re-verified by hand: `0xB363B0 + 0x1094` really does equal `0xB37444`,
not `0xB374E4`. This means every `[missile-guidance-diag]` log line
collected this session (tied to task #30/Predator Missile guidance) was
reading 160 bytes off from the intended field — that data should be
treated as unreliable until the constant is corrected and re-tested.
One-line fix, not yet applied (this was a research-only pass).

**Fix direction, not yet implemented:** a real fix needs the movement/
look hooks to detect which branch is active this frame (read
`0x00B36210` bits `0x8`/`0x10` and the `+0x1094` bit `0x80000`) and, when
branch 2 is active, feed the controller's right-stick delta into
`cmd+0x3e`/`0x3f` (via the same scale math `FUN_0057d740` uses) instead
of `kPitchAccum`/`kYawAccum` — potentially fixing DPV aim, mortar fire,
turret feel, and missile guidance in one implementation pass rather than
four separate ones. **Recommend re-scoping tasks #25/#26/#27's mortar
angle, and issue #27 bugs #1/#5/#6/#9 to point at this single fix
instead of pursuing each independently** — but confirm live once
implemented, since the `+0x1094` setter and branch-2 applicability to
each specific case are still hypotheses, not proven facts.

**CORRECTION for Predator Missile guidance specifically (2026-07-19,
user-requested RE pass via the killstreak's own GSC + a whole-binary
static scan) — the `+0x1094`/`cmd+0x3e`/`0x3f` unification does NOT apply
to this bug; it has its own, separate, now-mostly-traced native
mechanism.** This was already independently established by the
`controlslinkto` decompile earlier in this file (`clientStruct+0xc` bit
`0x80000`, a genuinely different address from `+0x1094` despite sharing a
bit value) but this section's own text was never updated to reflect it —
fixed now. Summary of what's actually confirmed for missile guidance:

- **GSC-side, settled**: a full re-read of `1555.gsc`'s guidance-phase
  loop (lines 916-937) confirms there is NO per-frame input read at the
  script level at all — it's a plain `while (isdefined(level._id_3C11))
  { wait 0.05; <abort-condition checks only> }`. Whatever steers the
  missile is 100% native, engaged once by `controlslinkto` and read every
  frame by the engine itself, not by GSC bytecode. This settles "is the
  input path GSC-level or native" definitively in favor of native.
- **Native-side, the real per-frame reader chain, found via a
  `FindConstantRefs.java` whole-binary scan for the literal scalar
  `0x80000`**: four functions test `[reg+0xc] & 0x80000` (the exact bit
  `controlslinkto`'s `FUN_005d7f20` sets on `clientStruct+0xc`). One of
  them, `FUN_004554d0`, is the real per-frame per-client dispatcher —
  confirmed via `FindCallers.java` that ITS OWN caller is `FUN_00644ed0`,
  the exact Pmove-tick function this project's PREVIOUS (now-removed) Sprint
  mechanism used to hook, calling it as
  `FUN_004554d0(pml, *pml /* clientStruct */, frameDeltaMs, pml+1,
  someByte)`. Raw disassembly (not just the decompile, which obscures the
  register-passed tail call) confirms: when `clientStruct+0xc` bit
  `0x80000` is set, `FUN_004554d0` skips its normal look/movement dispatch
  entirely and tail-jumps into `FUN_006423d0` with `ECX=pml+4` and
  `EAX=clientStruct`. `FUN_006423d0` reads 3 sequential floats from
  **`pml+0xc`/`+0x10`/`+0x14`** (a Pmove-locals field, NOT the real
  `usercmd_t` this project's own look hook writes to) and angle-wraps
  (anglemod-style) each one into **`clientStruct+0x10c`/`+0x110`/`+0x114`**
  — a concrete, different, more specific target than the old `cmd+0x3e`/
  `0x3f` theory, which is REFUTED as the relevant mechanism for this
  specific bug (that theory's `+0x1094` bit is a genuinely different
  address from the `clientStruct+0xc` bit `controlslinkto` actually sets).
- **Still open, honestly**: whether `pml+0xc/+0x10/+0x14` is a live
  per-frame copy of the real `cmd.angles` this project's look hook already
  feeds (in which case controller look should already reach the missile,
  and the bug is that something upstream stops refreshing it while
  linked) or an independently-fed field that needs this project's OWN input
  written into it directly — the copy site wasn't located in the time
  available for this pass. **Implemented instead of guessing**: a new
  log-and-forward diagnostic, `Hook_MissileGuidanceDispatch` in
  `proxy_d3d9/src/analog_input_hooks.cpp`, hooking `FUN_004554d0` itself
  (a plain `__cdecl` function, safe to hook via a normal MinHook
  trampoline, not the naked-asm register-capture style most of this
  file's hooks need). Gated on `clientStruct+0xc` bit `0x80000` so it logs
  nothing during normal play; change-triggered within that gate so an
  actual guidance sequence doesn't spam the log. Logs `pml+0xc/+0x10/+0x14`,
  `clientStruct+0x10c/+0x110/+0x114`, AND this project's own `kPitchAccum`/
  `kYawAccum` globals side by side — a real Predator Missile playtest with
  this build will show directly whether the pml fields track this project's
  own look input in real time (fix is elsewhere) or stay frozen while
  linked (fix is writing controller look into `pml+0xc/+0x10/+0x14`
  directly). Builds clean (0 warnings/0 errors, full rebuild). Not yet
  live-tested.
- **Not re-investigated this pass, so still standing as-is**: whether
  DPV aiming, mortar aim, and mounted-turret feel (issue #27 bugs #1/#5/
  #6) genuinely share the `+0x1094`/`cmd+0x3e`/`0x3f` mechanism — that
  part of this issue's original hypothesis is untouched by today's
  finding, which is specific to Predator Missile's `controlslinkto` path
  only. Don't assume the same fix covers both without separately
  confirming each.

## 31. Master `notifyonplayercommand`/`notifyoncommand` survey — two distinct builtins found, squadmate call-in's real failure mode identified (2026-07-18, research pass)

**Status:** Research complete, no code changes. Full grep-verified sweep of
all 240 decompiled Survival GSC scripts for every `notifyonplayercommand(`
and `notifyoncommand(` call site.

**Major architectural finding, not previously catalogued: these are TWO
DISTINCT GSC builtins, not one with an optional receiver.** Every single
`notifyonplayercommand` call in this codebase is invoked ON an entity
(`self notifyonplayercommand(...)` or `var_0 notifyonplayercommand(...)`
where `var_0` is a player reference) — player-scoped, zero exceptions.
Every `notifyoncommand` call is invoked bare, with zero exceptions —
level/global-scoped, no entity receiver. **`friendly_support_called` (the
squadmate call-in) uses the BARE/global `notifyoncommand`** (`1574.gsc`
line 1739), while `remote_missile`'s launch/abort, turret-cancel, and
ready-up all use the **player-scoped** `notifyonplayercommand`. This means
the squadmate bug and the Predator Missile bug are gated by two genuinely
different native mechanisms — a fix/finding for one doesn't automatically
transfer to the other.

**Full call-site table** (file:line, builtin, event, bind(s), feature):
`137.gsc:566` notifyoncommand `<id>_is_ready`/`_is_not_ready` on
`+gostand`/`+stance` (generic pre-mission ready-wait); `137.gsc:1231,1265`
notifyoncommand `toggle_challenge_timer`/`force_challenge_timer` on
`+actionslot 1` (Spec Ops challenge timer); `1442.gsc:321` notifyoncommand
`mag_cycle` on `+melee_zoom`/`+sprint_zoom` (laser item mag-cycle);
`1442.gsc:531` notifyonplayercommand `use_laser`/`fired_laser` on
`+actionslot 4`/`+attack`/`+attack_akimbo_accessible` (laser designator);
`1553.gsc:677,688` notifyonplayercommand `controller_sentry_cancel` on
`+actionslot 4`/`weapnext` (turret cancel); `1555.gsc:336`
notifyonplayercommand `switch_to_remotemissile` on a computed actionslot
(Predator Missile equip); `1555.gsc:1302-1313` notifyonplayercommand
`abort_remote_missile`/`launch_remote_missile` (already known, see issue
#29); `1558.gsc:1117` notifyonplayercommand `cancel sentry` on
`+actionslot 4` (a second, distinct turret-cancel copy); `182.gsc:1025`
notifyoncommand `autosave_player_nade` on `+frag`/`-smoke`/`+smoke`;
`1571.gsc:848` notifyonplayercommand `survival_player_ready` on `+stance`
(ready-up, already known); `183.gsc:4537` notifyoncommand
`_cheat_player_press_slowmo` on melee binds (dev cheat, not player-facing);
`1574.gsc:580` notifyonplayercommand `pip_cycle` on `+actionslot 2`
(picture-in-picture); `1574.gsc:1739` notifyoncommand
`friendly_support_called` on `+actionslot 4` (squadmate call-in — see
below); `228.gsc:431` notifyonplayercommand `nag` on `weapnext`;
`228.gsc:1297` notifyoncommand `draw_attention` on `+attack`/
`+attack_akimbo_accessible`; `362.gsc:43` notifyoncommand `flare_button`
on `+frag`/`+usereload`/`+activate`; `dubai_code.gsc:3670`
notifyonplayercommand `tospecops` on `pause`/`+gostand` (note: `"pause"`
as a bind-name argument is unusual, not `+`/`-`-prefixed — worth a closer
look if Campaign→Specops transition input ever matters);
`dubai_finale.gsc`/`dubai_utils.gsc` notifyoncommand `playerjump` on
`+gostand`/`+moveup`.

**Correction to the standing assumption that turret's success is evidence
synthetic input reaches these notify builtins**: turret's actual
PLACEMENT (not just cancel) is confirmed NOT notify-gated at all —
`1558.gsc`'s `_id_3CB3`/`_id_3CBE` (lines 1122-1153, 1377-1392) gate on
`self usebuttonpressed()`/`self attackbuttonpressed()`, direct polled
boolean button-state queries checked every loop iteration, with placement
finalized via a ground-trace check (`self isonground() &&
var_1["result"]`, line 1206), not an event wait. **Turret "working" only
proves direct button-state polling correctly observes this project's
synthetic kbutton writes — it says nothing about whether either notify
builtin can be reached by synthetic input**, since turret placement uses
neither. Only turret's CANCEL uses `notifyonplayercommand` — worth
separately confirming cancel has actually been live-tested (not just
placement) before treating it as a second confirmed-working notify
example.

**`friendly_support_called`'s real failure — resolved with concrete
evidence, and it's NOT primarily an input-reachability bug:**
1. **Listener chain confirmed intact** — `1574.gsc:1739-1740`, the
   `notifyoncommand(...)` call and `self waittill("friendly_support_called")`
   sit on consecutive lines in the same function (`_id_3F24`), itself
   threaded per-call from `_id_3F23`. No dangling/mismatched listener.
2. **Real, concrete map-dependent silent-failure path found**, traced all
   the way through `_id_061C::_id_3DE2` (`1564.gsc:2122-2128` — the exact
   function a prior session flagged as the unresolved "divergence point"):
   `var_5 = _id_0618::_id_3DCA(var_0)[0]; if (!isdefined(var_5)) return
   var_4;` — an explicit defensive early-return for exactly the case where
   the map has no spawn path. This reads `spawn_allies`'s own drop-path
   lookup (`1564.gsc:2071`, `getstructarray("drop_path_start",
   "targetname")` — a per-level struct-array query). **If the specific
   Survival map being played has no `drop_path_start`-tagged entities
   placed in it, this silently returns an empty array and nothing spawns
   — no error, no fallback, completely independent of input device.**

**Practical implication**: before spending more effort chasing this as an
input-reachability problem, check whether the map(s) actually used for
testing have `drop_path_start`/`chopper_boss_path_start` structs present,
or retest on a different Survival map. This is a genuine, evidenced
alternative explanation this project hadn't considered — quite possibly
the whole story, or at least a real confound on top of whatever the
input-device question turns out to be.

**Cross-referenced against a second, independent research pass
(turret-vs-squadmate mechanism comparison) — two non-mutually-exclusive
explanations now on record, not a single resolved answer:** that pass
found turret's initial call-in (not just placement, the WHOLE trigger)
isn't notify-gated at all — it's driven by a generic native `weapon_change`
event, via a shared dispatcher `_id_3CE8()`/trigger loop `_id_3CF5()`
(script `1553.gsc`) that fires a killstreak's handler the instant the
player's current weapon matches the registered streak weapon.
`friendly_support_delta`/`riotshield` are confirmed ABSENT from
`_id_3CE8`'s dispatcher table entirely — they're not weapon-type items at
all, so they never get this free ride; their `notifyoncommand`-gated
trigger is a genuinely different, standalone mechanism. That pass argues
by analogy (same builtin CLASS as `remote_missile`'s `+attack` gate,
which is independently confirmed unreachable by this project's synthetic
input via the long-hold live test) that `friendly_support_called` is
likely blocked the same way — **but this is an inference, not
independently confirmed for `notifyoncommand` specifically** (only
`notifyonplayercommand` has a live-tested negative result). **Both
explanations may be true simultaneously and aren't in tension**: even if
`notifyoncommand`'s reachability were fixed, the map-dependent
`drop_path_start` early-return above would still independently silently
no-op on any map lacking that struct. Treat as two real, stacked
candidate causes, not a single resolved root cause — the map-precondition
angle is the more concretely evidenced of the two (an actual code path
found and quoted, not an analogy) and is the cheaper one to rule in/out
live (just check/vary which Survival map is being tested).

**Also confirmed by the same comparison pass**: `FUN_0057a930` (the
native function this project's D-pad dispatcher calls for its "likely
equipment/killstreak use" branch, previously undecompiled) is NOT a
killstreak-specific call at all — it's a weapon-select fallback that
ultimately calls the same native weapon-SET function (`FUN_0042d6b0`)
`weapnext` uses. This means D-pad Left, for any WEAPON-type streak item
(sentry, remote_missile, precision_airstrike — all three registered by
weapon name in `_id_3CE8`), always resolves to a genuine native weapon
switch, which fires the generic `weapon_change` event `_id_3CF5()`'s
polling loop catches — fully explaining why those three "just work" via
this project's existing D-pad key-synthesis with no notify-gate involvement
at all.

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
  explicit, user-approved exception to this project's "no OS-level input emulation" rule: a
  synthetic F5 keypress via `PostMessage`, gated to Survival maps only. Confirmed working
  live; **the first of THREE deliberate departures from real-engine-call-only input in
  the whole project** (the second being D-pad Left's squadmate call-in, issue #14; the third
  being Back's real `+scores` scoreboard via a synthetic TAB keypress, added 2026-07-17 —
  see issue #28 below), each to be replaced if a native call is ever found. **Correction
  (2026-07-18): this summary previously said "first of two," missing the third exception
  entirely — it was implemented and wired up the same day this summary was originally
  written, but the summary itself was never updated to reflect it.**
- **Sprint stamina/cooldown (issue #6 above):** the real native duration/timer function
  was never found (only the speed-scale consumer, with no timer logic, was traced) —
  implemented as our own 4s-deplete/2s-cooldown layer instead, using real MW3 values,
  with a fixed-duration cooldown timer (not a continuous-float threshold) after catching
  a live regen-flicker bug. Bypassed correctly when the real `player_sprintUnlimited`
  dvar is live-set by a mission. Confirmed working live. Extreme Conditioning's
  `perk_sprintMultiplier` override remains open (see issue #7 above).

---

## 32. Console look input likely had a real acceleration ramp — this project's look currently has none (2026-07-19, web research, IMPLEMENTED same day) — RESOLVED 2026-07-20

**Status:** CLOSED. Implemented 2026-07-19, first shipped at a 200ms default
(matching external research), then live-tested against real hardware across many
values and corrected to **33ms (one 30fps engine frame)** — confirmed live as the
right feel and now the permanent shipped default. See the final entry below for
the full correction.

This project's controller look (`InjectControllerLookAngles`, writes directly to the
pitch/yaw angle-delta accumulator, see the big comment above that function) is a flat
per-frame multiply against `g_modConfig.sensitivity` — zero acceleration curve, zero
smoothing, by deliberate original design (explicitly chosen 2026-07-14 specifically to
avoid inheriting the mouse-input pipeline's own filtering/accel, see that section's
history). User raised a real question this session: did real CONSOLE MW3 (Xbox
360/PS3, 2011) actually have raw, unfiltered stick-to-look response, or was there some
smoothing baked in that this project's "raw" implementation doesn't match?

**Web research finding** (not a native-binary RE finding — no MW3-specific dev
documentation was found, this is inference from the same engine lineage): a technical
blog studying console shooter aim acceleration
(http://drstrangevolt.blogspot.com/2012/12/aim-acceleration-in-console-shooters-part2.html)
found that Modern Warfare 2 and Black Ops (same IW-engine lineage immediately
surrounding MW3 2011) both apply turning speed that ramps LINEARLY from zero to
maximum over approximately 0.2 seconds, on every stick input regardless of deflection
magnitude — even a full stick throw ramps up over that same short window rather than
being instantaneous. This is NOT the same thing as the modern (2019+) exposed
"Response Curve" (Standard/Linear/Dynamic) settings, which postdate MW3 by years and
aren't the mechanism being described here. Given MW3 shares that exact engine
lineage/era, it's plausible (not confirmed via this project's own RE — no iw5sp.exe
disassembly was done for this, purely an inference from external research) that
retail console MW3 had the same or a similar ~0.2s linear ramp, meaning this project's
current "instant, flat" look is actually MORE responsive than real console MW3 was,
not a case of the project needing to REMOVE unwanted smoothing.

**Decision (2026-07-19): log as a planned OPTIONAL config toggle** (a "custom fix," in
the user's words), not a default-on behavior change — this project's current flat
response has already been tuned/played against extensively (see the ADS
look-slowdown fix, issue #8, and the general sensitivity work in issue's #12/#14) and
changing default feel without asking would risk regressing already-confirmed-good
playtest feel. A future `[Look]` config section entry (e.g.
`AccelerationRampMs`, default `0` = current flat/instant behavior, matching this
project's existing pattern of additive, opt-in config toggles like
`SprintStaminaBypassForTesting` was) would let a player opt into a console-accurate
~0.2s linear ramp-up if they want closer console parity, without changing anyone
else's existing feel.

**IMPLEMENTED 2026-07-19** (user's own call: "ill test it if its good enough then it
will be the default"): `GetLookAccelerationScale()` (`analog_input_hooks.cpp`) tracks
`g_lookAccelStartMs`, the tick the stick left neutral (reset to 0 the instant it
returns to neutral, in `InjectControllerLookAngles`'s else-branch), and linearly
scales the look rate by `elapsed / AccelerationRampMs` capped at 1.0 —
`rampMs == 0` disables it entirely (returns 1.0 unconditionally), a clean revert
path. New `[Look] AccelerationRampMs` config value (`mod_config.h`/`.cpp`),
**default 200ms** (matching the researched ~0.2s figure) — set as the ACTIVE
default straight away for this live playtest, not held back as opt-in-only, per
the user's explicit request. Falls back correctly to this default even for an
existing `mw3ncp_config.ini` that predates this key (`GetPrivateProfileIntA`'s own
fallback mechanism). Builds clean.

**200ms CONFIRMED WRONG live, corrected to 33ms — task #32 CLOSED (2026-07-20).**
User live-tested the 200ms default against real hardware across many different
values and concluded the correct figure isn't an arbitrary wall-clock duration at
all — it's tied to this old engine's own locked 30fps tick rate (33.33ms/frame).
The external MW2/Black Ops research's "~0.2s" figure was apparently either an
approximation of a small number of engine ticks, or simply the wrong reference
point for this specific engine build — either way, live-testing against the real
game is the authoritative signal here, not the external research. `mod_config.h`'s
`lookAccelerationRampMs` default corrected `200 -> 33` (one 30fps frame), matching
config-file docs (`mod_config.cpp`'s `WriteDefaultConfig`) and the in-code comment
in `analog_input_hooks.cpp` updated to record the correction. **User-confirmed as
the right feel live — 33ms is now the permanent shipped default.**

---

## 33. Multiplayer feasibility research (2026-07-20) — technical RE, VAC risk, and a real cross-project correction

**Status:** Open, active research. Task: user-initiated MP feasibility investigation, first real step
toward eventually starting `iw5mp.exe` work per the locked "SP+Survival first, then MP" ordering.
Nothing implemented yet — this is groundwork.

### Technical RE feasibility — transfers well

A research fork compared `iw5sp.exe` and `iw5mp.exe` statically (imports, string search, build
provenance):
- `iw5mp.exe` has the same "zero controller input path" shape as `iw5sp.exe` — no `xinput`/`dinput8`
  import, same leftover console-codebase controller strings (`splitscreenactivegamepadcount` etc.).
- All of this project's already-RE'd SP bind names (`+attack`, `+breath_sprint`, `+actionslot`,
  `weapnext`, `+usereload`) exist in `iw5mp.exe` too, clustered in the same shape as SP's canonical
  bind-name table.
- No packing/anti-tamper blocking static analysis — decompiles as cleanly as `iw5sp.exe` (the sibling
  MW32011NSP project already independently decompiled real `iw5mp.exe` functions for its own netcode
  research, corroborating this).
- One real structural difference: `iw5mp.exe` is a 2018-05-02 build vs. `iw5sp.exe`'s 2012-11-30 —
  byte-level signatures won't transfer (different compiler pass), but the same signature-scanning
  APPROACH should. Not yet evaluated: whether MP's netcode-coupled usercmd pipeline (multiple clients,
  prediction/lag-comp) complicates the "hook the per-frame usercmd builder" approach that worked
  cleanly in SP.

### Direct RE pass, same session — bind-name table and dispatch architecture

Live Ghidra work (headless, fresh project at `D:\Tools\ghidra_projects_mp\`) found real detail,
independently corroborated by a parallel session's own `re_notes/iw5mp.md`/
`re_notes/ghidra_scripts/MP_DispatchAnalysis.java` (same bind-table address, same dispatch
candidates, same truncation hiccup — good independent confirmation):

- **Real bind-name table found at `0x008aa3bc`–`0x008aa4e8`**, 4-byte-stride array of string
  pointers, 91 clean `+X`/`-X` pairs (the parallel session's own script used an 8-byte stride by
  mistake and only captured 39 "+"-only entries — reconciled here, 4-byte stride is confirmed
  correct via clean, consecutive, known-bind-name pairs).
- **`FUN_0048c1c0`** is the real bind-name lookup/scan function (walks the table via
  `FUN_005c2a80` string-compare, 91-entry cap, matches SP's canonical-table-scanner concept).
- **`FUN_005a3960`, initially suspected as the SP-`FUN_00438710`-equivalent dispatcher, is NOT
  that** — full decompile shows it's a narrower **bind-alias-expansion helper**: given one of
  exactly 4 input bind names (`+melee`, `+sprint`, `+holdbreath`, `+changezoom`), it fires a short
  list of related secondary binds (e.g. `+sprint` also triggers `+breath_sprint` and
  `+sprint_zoom`) via `thunk_FUN_0048c620`, using the bind NAME as a string argument, not a
  numeric case. Only caller: `FUN_005a3ac0`.
- **`thunk_FUN_0048c620` decompiled out to a dead end for hooking purposes**: it's a UI/options-menu
  **key-name lookup function** — given a bind command, returns the human-readable key name(s)
  currently bound to it (or "KEY_UNBOUND"), for displaying bind info/hints. Not a live input
  dispatcher. Caught via decompile before wasting further time assuming it executed binds.
- **Real architectural finding, not yet exploited**: MP has a genuinely SEPARATE `+holdbreath`/
  `-holdbreath` bind, distinct from `+breath_sprint` — unlike SP, where Hold Breath turned out to be
  folded into the Sprint bind (the whole issue #6/#24 saga). If MP's Hold Breath is a clean,
  dedicated bind with no aliasing bug, it may not need anything like SP's eventual native-kbutton
  force-clear fix at all. Not yet confirmed either way — needs the real per-frame kbutton dispatch
  path found (not yet located; `FUN_005a3960`/`FUN_0048c620` are UI-adjacent, not it).
- **Real next step, not yet done**: find `FUN_005a3960`'s only caller (`FUN_005a3ac0`) and/or do a
  live-debugger breakpoint pass to find the actual per-frame kbutton dispatch that's structurally
  equivalent to SP's `FUN_00438710` — the bind-name table and lookup function are confirmed, but the
  real switch/dispatch consumer is still unlocated.

### VAC/anti-cheat risk — corrected, real and non-zero

Two research forks plus direct user-driven fact-checking converged on a corrected risk picture:

- **A now-corrected error**: this project's own top-level `CLAUDE.md` briefly stated "Confirmed: MP
  Anti-Cheat (VAC) Not Active," citing "Official Valve VAC list confirms MW3 is not on the list" —
  **this was never actually verified and was wrong.** Caught the same day via direct cross-check
  (user-initiated): MW3 (2011)'s own Steam store page genuinely lists "Valve Anti-Cheat enabled,"
  confirmed via a direct quote from a Steam Community discussion thread on the game's own forum.
  `CLAUDE.md` corrected in place; see that file's own "CORRECTED 2026-07-20" section for full detail.
  Sibling project MW32011NSP's `re_notes/vulnerability_research.md` §3 was never actually wrong —
  it treated VAC as active throughout; only the `CLAUDE.md` summary section was inconsistent with it.
- **VAC is real, active, and community-confirmed to still ban players** on this specific title
  (locks Multiplayer access specifically; Campaign/Spec Ops stay playable even if banned) — an
  active, current topic on the game's own Steam forums, not historical.
- **VAC is signature/behavior-based, not blanket injection-detection** — real precedent: Discord,
  OBS, RTSS, MSI Afterburner all inject into game processes constantly without routinely triggering
  bans. No documented case found either way of a benign/QoL tool being banned on MW3 specifically —
  genuinely unknown territory, not "confirmed safe." Community-reported real, P2P-driven
  performance/effectiveness limits ("VAC works a bit for it but not great like it does in other
  games") — a real, community-documented limitation, not a reason to treat the risk as zero.
- **`steam_api.dll` forensic pass** (exports/imports dumped via `dumpbin`): the only VAC-adjacent
  export in this 2011-era SDK build is `SteamGameServer_BSecure` (server-side status query) — and
  **neither `iw5mp.exe` nor `iw5sp.exe` actually imports/calls it or anything auth/ticket-related.**
  Both share an identical baseline Steamworks surface; MP's only additions are game-hosting
  functions (matches its listen-server model), SP's only additions are stats/restart functions.
  Reassuring in the relative sense (MP's Steam integration isn't doing anything riskier than SP's
  already-fine one), but doesn't independently prove VAC is inactive — VAC runs as an independent
  background component, not something the game code has to call into.
- **Retail matchmaking is NOT dead** (corrects an assumption in MW32011NSP's own notes): current
  Steam Charts show ~51-61 concurrent MP players as of this session (down hard from an 86,832 peak
  in 2011, but real and nonzero) — there is a small live population to actually build MP support for.
- **Bottom line, corrected**: "probably fine but unverified, real non-zero risk" — not "genuinely
  dangerous" (VAC's real-world behavior favors signature/behavior detection over blanket
  injection-flagging, precedent from benign overlay tools, stale signature sets on old titles), and
  not "moot" either (VAC is confirmed still enforcing, and P2P-limited effectiveness doesn't mean
  zero effectiveness). Cannot be verified to zero risk without either an authoritative VAC technical
  writeup or accepting some real first-mover risk.

### Fallback considered, not started

User-proposed worst-case fallback if the VAC risk research ever turns up something more concerning:
a custom/separate MP path (own server + connection layer, bypassing retail Steam's live
VAC-monitored session entirely) with an ownership-verification step in place of redistributing
Activision's code — architecturally similar in spirit to Plutonium's own model, but requiring the
user's own legitimately-owned game files rather than a separate account/matchmaking system. Noted
as the documented fallback, not attempted — the current plan (RE pass against the real `iw5mp.exe`,
low-but-uncharacterized risk) is still the first thing being tried.

### A second, separate anti-cheat system found — Demonware's own `bdAntiCheat`, NOT Valve's VAC

Direct Ghidra RE this session found `iw5mp.exe` contains a real, compiled-in anti-cheat system that
has nothing to do with VAC at all: a class family named **`bdAntiCheat`** (confirmed via real
RTTI-mangled class names — `bdAntiCheatResponses`, `bdAntiCheatChallenges`, `bdAntiCheatChallenge`,
`bdAntiCheatChallengeParam` — and a real method `bdAntiCheat::answerChallenges` at `FUN_0070f840`,
confirmed via embedded debug strings citing the actual source file `.\bdAntiCheat\bdAntiCheat.cpp`
and real line numbers). `bd` is Demonware's own internal namespace — Activision's backend/
matchmaking middleware provider for CoD titles of this era, entirely separate from Valve/Steam.
**This means MW3's real anti-cheat exposure has (at least) two independent systems, not one.**

**What was traced this session**:
- `bdAntiCheat::answerChallenges` is an orchestration shell (validates readiness, computes a
  response via a virtual method call, queues an async network task to send it) — its virtual
  dispatch resolves to generic `bdTaskByteBuffer` network-task plumbing, NOT itself a
  hashing/scanning function. The actual challenge-CONTENT computation logic is upstream/unlocated.
- The whole challenge-response chain is **timer-gated and periodic** (compares a live timestamp
  against stored thresholds, re-triggers repeatedly) rather than a one-time connection check — a
  real anti-cheat design pattern (harder to bypass than a single handshake).
- The trigger is gated behind a recurring "network state == 2" check (found at multiple call-chain
  levels) — strongly suggestive of "connected to an active session," not solo play or mere
  matchmaking search, though not yet proven down to the exact bit/enum meaning.
- Broad string search for memory-scan-specific naming (`integrity`, `memoryscan`, `bdSystem`,
  `bdMatchmaking`) came back clean/absent — no named deep-memory-scanning subsystem found under any
  plausible name searched so far.
- **A `"getchallenge"` string sits directly adjacent to `checksum`/`protocol` strings** — this is
  the SAME ordinary Quake3-lineage connectionless-packet challenge the sibling MW32011NSP project
  already documented as anti-spoofing (not anti-cheat). Genuinely unresolved: is `bdAntiCheat`'s own
  "challenge" concept the same thing as this mundane protocol-level challenge, or a distinct, deeper
  mechanism whose content-logic just wasn't in the specific call chain traced? Flagged, not
  resolved — three more research forks are chasing this down (see below), don't treat either
  reading as settled.
- Confirmed real Demonware backend hostnames referenced in the binary:
  `mw3-pc-auth.prod.demonware.net`, `mw3-stun.us.demonware.net` — real infrastructure evidence, not
  itself proof of what gets checked.

**External precedent found, Xbox-specific — the most concrete lead yet, not yet confirmed for PC**:
a real reverse-engineering project, [`CWest07/COD-Demonware-AntiCheat`](https://github.com/CWest07/COD-Demonware-AntiCheat),
documents this SAME `bdAntiCheat` system on Xbox 360, stating its two biggest checks are "flags used
to detect modified consoles" and **"a CRC32 / CRC32 split checksum on the .text (code section)."**
A code-section checksum is exactly the mechanism that would catch MinHook-style inline/trampoline
hooking (which overwrites a target function's own bytes with a jump to a detour — a direct `.text`
modification). The Xbox-specific detection method cited (`GetModuleHandleA("xbdm.xex")`) is
platform-specific and doesn't apply to PC, but the `.text`-checksum *concept* is a standard
cross-platform anti-tamper technique, and since the same class family exists in the PC binary, it's
a reasoned inference (not yet independently confirmed) that it carries over.

**Real, actionable design implication, even before PC confirmation lands**: this draws a genuine
technical line between two categories of hooking technique for any future MP work —
- **Inline/trampoline hooks that patch bytes inside `iw5mp.exe`'s own `.text`** (MinHook's default
  technique when used this way) — plausibly detectable by exactly this documented mechanism.
- **Vtable hooks (swap a function pointer, `.text` untouched) or plain direct calls into existing
  game code** (this project's established `CallKbuttonDown`/`CallKbuttonUp` style — calls existing
  functions directly, modifies nothing) — doesn't trigger this specific check, since no `.text`
  bytes change. This project's SP work has always preferred exactly this style already (see the
  architecture notes in `CLAUDE.md`), which is a reassuring, if coincidental, alignment.

**Separate finding, NOT the same mechanism — don't conflate the two**: a real `sv_pure`-style
Fast-File/IWD checksum system also exists (`"sv_pure...Cannot use modified IWD files"`,
`"Checksum of all referenced Fast Files/IWD files"`, referenced from `FUN_005741f0`/`FUN_006369d0`).
This is classically a Quake3-engine-family SERVER-side check against a CONNECTING CLIENT's asset
files — a check on `.ff`/`.iwd` FILE content, not process/code memory. **This is NOT why the
boot-splice crash happened** (`re_notes/known_issues.md`'s own boot-splice entry, further up this
file — that crash is independently, fully explained by the `FUN_004ca310` link-thunk/MinHook-
trampoline incompatibility, and happened before the hook's own code ever ran, let alone reached any
asset-validation step) — flagging this explicitly since the two are easy to conflate and the
crash's real cause is already settled. Where this DOES matter: any future custom Fast-File/zone
content (the parked button-glyph font-extension work, issue #23/#31) should treat "would this
survive an `sv_pure`-style check" as a real, separate question specifically for ONLINE contexts
(MP, or Survival's online co-op) — solo Campaign/Survival has no server present to enforce this
kind of check against, matching this whole section's own solo-vs-online risk line. Not yet
determined whether this checks on-disk files specifically (this project's in-memory-only zone
injection approach would sidestep that) or loaded zone content more generally — one of three
research forks below is chasing this.

**RESOLVED (2026-07-20) — all three follow-up threads returned, plus one direct trace:**

1. **`.text`-checksum question — NOT confirmed for PC, and real evidence points the other way.**
   Direct decompile of `iw5mp.exe`'s `.text` range (`0x00401000`–`0x007df3ff`, confirmed via PE
   section listing) found the literal terms `crc32`/`crc32split` genuinely exist in this PC binary
   too (matching the Xbox documentation's own naming), alongside `sha`/`md4` in the same string
   cluster — but tracing the ONLY real `crc32(init, buf, len)`-shaped function in the entire binary
   (`FUN_005e0df0`) back through its only caller (`FUN_005e5f30`) found it's a **generic buffered
   file/stream-read function** — both of its two call sites to the CRC32 function are inside a
   chunked file-read loop, incrementally accumulating a running checksum of bytes being read FROM
   A FILE/STREAM, gated by a per-stream "is CRC tracking enabled" flag. This is asset/file-stream
   checksumming (almost certainly the same subsystem behind the `sv_pure`/Fast-File integrity check
   below), not a scan of the running executable's own memory. Since this is the only CRC32
   implementation found anywhere in the binary, and both its call sites are confirmed
   file-stream-only, **this is real, substantive evidence against the specific worry that PC's
   `iw5mp.exe` does a `.text`-section code-integrity scan via CRC32** — not proof that no
   code-integrity check exists via any other undiscovered mechanism, but a real negative result for
   the most concrete lead found, not just an absence of proof.

2. **`iw5sp.exe` also contains `bdAntiCheat`** — same class names, same `.\bdAntiCheat\
   bdAntiCheat.cpp` source reference, confirmed via direct string match (`bdAntiCheat` @
   `0x008d94d3`, `answerChallenges` @ `0x008d94fd`, etc.). This system is not MP-exclusive — it's
   compiled identically into both binaries. Reinforces (again) that the real risk boundary is
   **solo vs. online**, not **SP vs. MP** — the same challenge-response code plausibly activates
   during Survival's online co-op the same way it would in MP, gated on the same kind of
   active-connection state.

3. **`sv_pure`/Fast-File checksum — confirmed server-side-only, and file-based, not memory-based.**
   `FUN_005741f0` is a server-STARTUP dvar registration routine (~50 `sv_*` dvars, including
   `sv_pure` itself, default `1`) — classic Quake3-lineage SERVER config, only relevant when
   something is acting as a server/host. `FUN_006369d0` is the CLIENT-side counterpart, but it
   parses a REMOTE server's advertised info string (for server-browser-style display) — not a
   local self-check of the client's own files. **Net effect: `sv_pure` requires a real
   server-vs-client relationship to have any bite at all — zero enforcement point in solo
   Campaign/Survival — and it operates on IWD/Fast-File CHECKSUMS OF FILES ON DISK, not live
   process memory.** This project's own established approach (in-memory-only zone injection,
   never touching files on disk) would not even be visible to this specific check.

4. **Web research: no PC-specific external confirmation found either way.** Correctly flagged and
   avoided a real trap: most "Modern Warfare 3 anticheat" searches surface the unrelated 2023
   "Modern Warfare III" title's RICOCHET system — a naming collision, not applicable to this 2011
   game. Found real, useful era-context (Demonware's anti-cheat had multiple versions; this looks
   like their in-house v2-era system, distinct from later titles' commercial Arxan protection) and
   confirmed Plutonium's OWN separate anti-cheat triggers specifically on memory edits/injection
   (already known, not new), but no direct PC-specific `bdAntiCheat`/`.text`-checksum discussion
   was found in cheat-development community sources searched.

**Overall bottom line, updated**: the scariest specific hypothesis (a `.text`-section code-integrity
CRC scan on PC, that would directly catch MinHook-style inline hooking) is **not confirmed and has
real evidence against it** — the only CRC32 implementation in the binary is asset-stream-only. This
doesn't eliminate all anti-cheat risk (the actual `bdAntiCheat` challenge-response CONTENT is still
not fully traced, and a different, undiscovered mechanism could exist) — but it meaningfully
de-risks the most concrete, actionable worry raised this session. Combined with the already-established
design implication (prefer vtable hooks / direct calls into existing game code over inline `.text`
patching for any future MP work, which this project's SP work already does by convention), the
practical risk picture for a carefully-built MP implementation is more favorable than it looked
immediately after the Xbox precedent was first found.

### VAC itself — five more research forks (2026-07-20), decisive finding on the last one

Given `bdAntiCheat` came back less concerning than first feared, research shifted back to VAC
specifically (Valve's own system, separate from Demonware's), since VAC was already established as
the primary, still-real risk.

- **VAC's actual mechanism**: user-mode only (no kernel driver, unlike EAC/BattlEye/Vanguard —
  genuinely less deep visibility than modern kernel-level anti-cheats). Does module enumeration and
  scans on `DLL_THREAD_ATTACH` (new DLL loads), but detection itself is signature/pattern-matching
  against KNOWN cheats, not "any unrecognized DLL gets flagged" — real precedent: published,
  non-public hooking-based bypasses since 2018 reportedly still work without bans, meaning novel
  code that doesn't match an existing signature isn't routinely caught. A disassembly-reconstructed
  source project (`danielkrupinski/VAC`) documents VAC's methodology as including both Import
  Address Table integrity checks (doesn't apply to a DLL-search-order proxy — the IAT itself stays
  structurally intact regardless of which physical file gets loaded) AND **virtual method table
  (VMT) pointer validity checks** — genuinely relevant, since this project's technique includes a
  real vtable hook (`IDirect3D9::CreateDevice`). Unclear whether that check is scoped to VAC's own
  internal structures specifically or applied more broadly to arbitrary game-side vtables — a real,
  named overlap, not resolved either way.
- **ReShade precedent, strong and directly sourced**: ReShade's own creator (crosire) stated on the
  official forum: "there has never been a proved case where somebody was banned for using a
  post-processing injector (apart from ENB...)." Valve went further than tolerance — an explicit
  toggle permits ReShade in CS2 specifically, a deliberate allowance. **Scope limit, stated by
  ReShade's own developers**: they deliberately never added game-memory/entity-data reading,
  explicitly because anti-cheat "would likely not appreciate" it — ReShade only ever touches the
  rendering pipeline (color/depth buffers), never raw gameplay memory.
- **x360ce precedent**: a community "list of allowed VAC modifications" explicitly lists x360ce (a
  DLL-replacement controller-emulation tool, swaps `xinput1_3.dll`) as confirmed OK with VAC on Left
  4 Dead 2 — architecturally close to this project's own technique (DLL swap via normal search
  order, not an external injector). Caveat: community-tested folk knowledge, not an official Valve
  statement, and the same source still describes general DLL/EXE tools as "unsafe" as a category —
  x360ce is a specific tested exception, not proof of a blanket rule.
- **No official Valve accessibility-vs-cheat carve-out found** anywhere checked (Steam Support's
  VAC FAQ, Steamworks partner anti-cheat docs, Valve Developer Community's VAC wiki) — VAC policy
  stays framed generically around "advantage over another player," no explicit exception language.
  Steam Input (Valve's own first-party controller-remapping layer) is architecturally unrelated —
  it works via the Steam client/overlay, not DLL injection — so it's not a real technical comparison
  point, just evidence Valve tolerates input remapping as a concept at the platform level.
- **Memory-reading community folk-consensus, blunt and pessimistic**: a separate community thread
  found unhedged consensus — "If you touch the memory of the game you will be VAC banned" — with no
  distinction drawn between read-only scanning and active modification. More pessimistic than the
  signature-based framing above, at least at the level of community risk perception. Lands directly
  on this project's own (already disabled, already earmarked for hard-exclusion from any MP build)
  aim-assist feature specifically, which reads live entity/target memory — reinforces that
  exclusion as a hard line, not just a cautious default.

**DECISIVE FINDING, corrects earlier optimism — the real distinguishing risk factor is DEPTH of
modification, not the loading method.** ReShade (visual-effects-only, never touches gameplay state)
has essentially zero proven bans. **ENB — a similarly-structured `d3d9.dll` proxy that "goes
deeper" than pure visual post-processing — is explicitly named as notably riskier, WITH real
historical precedent: a comparable-depth `opengl32.dll` proxy technique got players banned on
Counter-Strike/Half-Life.** This is the single most important correction from this whole VAC
research arc: **this project's own core technique doesn't stop at visuals — it writes real values
directly into `usercmd_t`/`kbutton_t`, actual gameplay input state, not rendering data. That's
structurally much closer to ENB's risk category than ReShade's.** The earlier optimism from the
ReShade precedent (see above) was real but too broadly applied — ReShade's clean record is
specifically bounded to "visual effects only," and this project's baseline input-remapping work
(movement/look/buttons — everything currently shipped, not just the disabled aim-assist feature)
already goes past that boundary. **This meaningfully recalibrates the overall risk picture back
toward genuine, non-trivial concern** — not because any single new fact is alarming on its own, but
because the closest real-world precedent for "a `d3d9.dll` proxy that manipulates more than
visuals" is the one with an actual documented ban history (ENB), not the one with a clean record
(ReShade). Every other finding in this section (VAC being signature-based, the `bdAntiCheat`
de-risking, x360ce's OK status) still stands — but none of them override this specific, sobering
comparison, and it should be weighted as the most decision-relevant single data point from the
entire VAC investigation.

### Full cross-surface risk matrix (2026-07-20) — 5 parallel forks, verifying every surface separately

Following the aim-assist removal decision above, ran 5 parallel research forks to verify VAC/
anti-cheat risk across every distinct surface this project touches or could touch (solo Campaign,
Survival co-op, MP, the hooking technique itself, and category-level precedent), rather than relying
on one blended risk characterization. Findings:

| Surface | Risk | Basis |
|---|---|---|
| Solo offline Campaign (`iw5sp.exe`, no network) | **Near-zero** | Valve's own partner docs (`partner.steamgames.com/doc/features/anticheat/vac_integration`): VAC "does nothing for single player games" — scanning is tied to connecting to a VAC-secured server, not to launching the exe. `bdAntiCheat`'s `network state == 2` gate is consistent with full dormancy in this surface. |
| Survival online co-op (`iw5sp.exe`, 2-player) | **Low** — closer to solo tier than MP tier | Direct quote, the game's own Steam Community VAC-ban FAQ (steamcommunity.com/app/42690/discussions/0/540739405757612096): a VAC-banned account is blocked from Multiplayer but can still play "Campaign and Spec Ops (along with a partner if you want) with no restrictions." VAC enforcement doesn't reach this mode at all — not just unlikely to trigger, structurally out of scope. |
| MP, retail public matchmaking (`iw5mp.exe`) | **Real, non-theoretical** | Official Activision matchmaking is dead, but retail Steam MP still has a real live population (~50-175 CCU per steambase.io Steam Charts data, live snapshot ~50 in-game as of 2026-07-20), largely routed through third-party dedicated servers (App 42750, community providers). VAC is confirmed active and automatic on this surface. Not started yet — matches the locked "SP+Survival first" ordering. |
| MP, private/self-hosted, host vs. client role | **Same as public MP — no reduction** | Steam's own VAC-ban FAQ: VAC cares whether the mod code *runs on your machine*, not which P2P network role you hold — a host-migrated player isn't exempt just for being the host. This project's hooks run identically on the local machine regardless of role, so host/client status doesn't change exposure. |
| Hooking technique itself (proxy `d3d9.dll`, MinHook, `CreateDevice` vtable hook, inline engine hooks) | **Low, surface-independent** | No clean case found of MinHook itself triggering a ban — the one report (`danielkrupinski/Osiris` issue #2649) is confounded by Osiris being a full aimbot/ESP cheat menu, not attributable to the hooking library. RTSS/MSI Afterburner/OBS hook the identical D3D entry points (`CreateDevice`/`Present`) via Microsoft Detours — functionally the same trampoline-hooking technique as MinHook — at massive scale on VAC-secured games with no bans. VAC's real hook-detection (per `danielkrupinski/VAC`) is scoped to its own module/known system-DLL imports and to *client/engine gameplay* vtables (entity lists, `IVEngineClient` — the standard cheat vector), not a generic scan of every D3D vtable in a process. This project's hook never touches that class of interface. |
| Category precedent (input-remapping/accessibility tools generally, not this project specifically) | **Low** | **x360ce is a near-exact analog** — a proxy DLL that swaps in for `xinput1_3.dll` (same "proxy DLL the game loads instead of the real system one" technique this project uses for `d3d9.dll`) — with a multi-year clean VAC track record, explicit community reasoning being "it does not affect the DLL or EXE files of the game." No confirmed ban case survived scrutiny for reWASD, DS4Windows, Xpadder, or ViGEm-based tools either (the one DS4Windows anecdote found is disputed as likely misattributed to actual cheat software running alongside it). |

**Net read, corrects/refines the "genuine, non-trivial concern" framing above to be surface-specific
rather than blanket:** the project's core technique and every surface it currently ships on (solo
Campaign, Survival co-op) sit at low-to-near-zero risk with strong direct precedent (x360ce,
RTSS/OBS via Detours) — the earlier ENB-driven "recalibration toward genuine concern" was correct
about the general *class* of risk (proxy-DLL depth-of-modification) but this pass adds that VAC's
actual detection surface is scoped to gameplay-relevant vtables/imports, which this project's
`CreateDevice`-then-engine-input-hooks approach doesn't touch — a meaningfully different profile than
ENB's rendering-pipeline-deep modifications. The one surface carrying real, non-theoretical exposure
is **retail public MP**, which is unstarted work per the locked ordering; when MP work does begin,
this table's MP row (not the SP/Survival rows) is the risk profile that actually applies, and host
vs. client role doesn't offer any risk reduction to plan around.

### Second pass, 5 more forks — deeper "in-game" mechanics (2026-07-20)

Follow-up research, going past the cross-surface matrix above into VAC's actual runtime mechanics
and a real binary re-trace, rather than precedent/inference alone.

- **Ban-wave delay is real, ~3-4 weeks typical (multiple sources: Steam Support's own VAC FAQ,
  vac-ban.com, the July 2017 CS:GO 40k-account wave, danielkrupinski/Osiris issue #2745).** VAC
  deliberately withholds bans in a silent-flag-then-batch pattern specifically to prevent
  cheat-developers correlating a specific change to detection. **Consequence: this project's own
  ~1-week public clean record (as of this writing) carries close to zero evidentiary weight on its
  own** — it hasn't cleared even one full wave cycle yet. This does NOT undermine the older
  cross-tool precedent (x360ce, RTSS/OBS) above, since those have had years to clear many wave
  cycles — but this project's own short track record should not be cited as independent supporting
  evidence for at least several more months. No evidence found that wave-delay is severity-scaled
  (i.e. no basis to assume a low-severity tool surfaces later than an obvious cheat if flagged at
  all).
- **Independent same-engine corroboration, different source than the earlier Steam-FAQ finding**:
  a real, named MW3-specific tool (`marvinlehmann/CoD-SCZ-FoV-Changer` on GitHub, a runtime
  memory-writing FOV changer supporting MW3 Special Ops among other CoD titles) states in its own
  README that its supported CoD titles' singleplayer/Spec-Ops executables don't run VAC or
  PunkBuster at all — corroborating the "VAC doesn't reach Campaign/Survival" finding from a
  completely unrelated source. FOV-changer tools generally (the closest existing same-engine QoL-mod
  category) are widely reported safe on CoD MP too (an Infinity Ward developer is quoted as not
  banning FOV-mod users), with credible reports attributing the rare "banned while using an FOV
  changer" claims to bundled actual cheat functionality, not the FOV injection itself. **No
  bespoke same-engine controller-support precedent found either way** — this project appears to be
  genuinely first-of-kind in that specific niche, neither a positive nor negative data point.
- **VAC's runtime scan mechanics, real technical detail (danielkrupinski/VAC's own README, general
  anti-cheat/security literature)**: VAC does re-scan repeatedly throughout a session, not just at
  connect (exact cadence is server-directed and undisclosed). Its actual injected-code detection
  heuristic — missing PEB module-list entry, non-file-backed VAD/`MEM_PRIVATE` pages, a thread
  entrypoint outside any loaded module's range — is the standard signature for
  `CreateRemoteThread`/manual-map-style runtime injection. **This project's proxy DLL, loaded via
  the game's own normal `LoadLibrary` DLL-search-order at launch, doesn't match that signature at
  all** — it's file-backed, PEB-registered, with legitimate module bounds, structurally identical to
  the real system `d3d9.dll` or any legitimate launch-time overlay (Steam/Discord overlay, RGB
  software). Risk does not compound with session duration — each scan is an independent check
  against a module that looks the same every time, not a cumulative-exposure model.
- **Load-timing as a protective factor — confirmed real in general anti-cheat/EDR literature**
  (arxiv.org/pdf/2408.00500, Black Hills InfoSec, Palo Alto Unit42 on DLL proxying specifically):
  a DLL present since process launch via the OS's normal loader is explicitly documented as harder
  to distinguish from benign activity than `CreateRemoteThread`-style injection, which has
  "obvious behavioral signatures." No VAC-specific source found confirming or debunking this exact
  distinction by name, but the general mechanism lines up cleanly with this project's technique.
- **Direct binary re-trace of `bdAntiCheat` (real Ghidra RE this pass, both binaries)**: only
  `bdAntiCheat::answerChallenges` is locatable via debug-string xrefs in either binary
  (`FUN_00714fa0` in `iw5sp.exe`, `FUN_0070f840` in `iw5mp.exe` — identical shell logic in both:
  validates readiness, virtual-dispatches, queues an async task). **No self-hash/self-integrity
  function found in either binary** — negative result, consistent with the earlier CRC32
  file-stream-only trace. A `CreateToolhelp32Snapshot`/`Module32First`/`Module32Next` call chain
  was found and initially looked concerning, but full tracing re-attributes it as a **stale-PID/
  improper-shutdown single-instance check** — it reads a PREVIOUS run's PID from a lockfile and only
  walks THAT old process's modules to check if it's still alive, never the CURRENT process's own
  loaded-module list. **Real, direct evidence against "the engine enumerates its own modules looking
  for our proxy d3d9.dll."** An adjacent telemetry-capable system (`bdEventLog::recordEvents`,
  same task-queue plumbing) exists and is confirmed live-wired (not dead code) in both binaries, but
  what it actually reports wasn't resolved this pass — flagged open, not a finding either way.

**Net effect of this second pass**: reinforces the low-risk read on solo Campaign/Survival with
real mechanism-level evidence (load-order legitimacy, no self-hash, module-enum re-attributed away
from a real concern) rather than precedent-only inference — and adds one honest caveat that cuts
the other way: this project's own short public track record isn't real supporting evidence yet, so
don't cite "no bans reported" about MW32011NCP itself as if it carries the same weight as the
multi-year x360ce/RTSS comparisons. Revisit that specific caveat again once the project has several
more months of public history.

### Third pass, 6 more forks — closing every remaining chaseable open thread via direct RE (2026-07-21)

User asked to keep digging until a definitive answer, explicitly via RE rather than further precedent-only
research. Five forks ran direct Ghidra disassembly against the specific gaps the first two passes left open;
a sixth external-research fork chased the two open precedent questions to their most concrete source; a
seventh follow-up fork closed the one new loose end the RE forks turned up. Every item below is
disassembly-backed or a direct-source read, not inference, unless labeled otherwise.

- **`bdEventLog::recordEvents` payload — fully resolved, not just de-risked.** Traced the complete chain in
  `iw5sp.exe` (`FUN_0070f800`/`FUN_0070f430`/`FUN_0070fb70` → enqueue `FUN_00413d70` → init `FUN_004d0920`,
  registered as `"LSP_Logging_Init"`, dvar `dw_logging_level` **default 0 = off**, sampled, minutes-interval
  submission). The only literal payload string found anywhere in the reachable chain is
  `"results_matchmaking time: %d num_results: %d"` — a matchmaking-latency diagnostic, logged only once a
  connection is active (same `network state == 2` gate as `bdAntiCheat`). No module list, memory address, or
  process/thread data anywhere in this system. `iw5mp.exe` has the structurally identical functions at
  different addresses (`FUN_007075e0`/`FUN_007079b0`) — not independently re-traced call-site-by-call-site,
  flagged as inferred-by-structural-identity, not a gap that changes the verdict.
- **`bdAntiCheat`'s actual challenge-response CONTENT — fully resolved, and it's inert.** The virtual call
  inside `answerChallenges` (`FUN_00714fa0` SP / `FUN_0070f840` MP) resolves to a 2-entry vtable whose real
  target, `FUN_00715a30`, is confirmed via its own embedded debug string to be
  **`bdTaskByteBuffer::allocateBuffer`** — generic buffer-size/alignment bookkeeping, not a challenge object
  at all. The actual response bytes are two **hardcoded literal constants** (`0x26`, `2`) written directly in
  `answerChallenges` itself via a bounds-checked buffer-write helper — not computed from any read of process,
  module, or memory state. Identical constants, identical call shape, in both binaries. This closes the
  single biggest remaining unknown from the prior passes: the challenge-response mechanism does not scan
  anything, dynamic or otherwise — it's a fixed protocol type-tag/version-ack.
- **No self-hook / D3D9-vtable / IAT integrity check found in either binary.** Dedicated search found: no
  code that reads or compares its own `IDirect3DDevice9`/`IDirect3D9` vtable pointer *values* outside normal
  `(*vtable)[n](...)` call sites; no IAT walk anywhere; every `GetProcAddress`/`GetModuleHandleA/W`/
  `LoadLibraryA` call site (48/13/7/8 in SP, 40/9/7/7 in MP) is mundane MSVC CRT plumbing targeting standard
  system DLLs, none referencing `d3d9.dll` or any hooked engine function by name (`"d3d9.dll"` has zero code
  references in either binary — it's read only by the OS loader's import directory); a scan for `CMP`/`TEST`
  against the JMP-hook opcode byte (`0xE9`) found exactly one hit per binary, both confirmed false positives
  inside the CRT's own `_memcmp`. **New, MP-only finding**: `iw5mp.exe` statically imports
  `SetWindowsHookExA`/`UnhookWindowsHookEx`/`CallNextHookEx` (absent from `iw5sp.exe`) but a full-memory
  pointer scan found **zero references of any kind** to any of the three anywhere in code or data — a dead
  transitive import (plausibly pulled in whole by a statically-linked Demonware/Steamworks library), not an
  active mechanism.
- **Full process-introspection API audit, both binaries.** Entire API families confirmed **not imported at
  all** (zero attack surface): `Process32First/Next`, `EnumProcessModules(Ex)`, `EnumProcesses`,
  `ReadProcessMemory`, `WriteProcessMemory`, `NtQuerySystemInformation`, `NtQueryInformationProcess`,
  `VirtualQueryEx`, `GetModuleHandleExW`, `LoadLibraryW/ExA/ExW`. The one `CreateToolhelp32Snapshot`/
  `Module32First/Next` chain already traced for `iw5mp.exe` is now confirmed to exist **identically in
  `iw5sp.exe` too** (byte-identical logic, only the `OpenProcess` access mask differs) — extends, doesn't
  change, the prior verdict: it walks a *foreign* PID's modules to check a stale lockfile, never enumerates
  the current process's own module list. `VirtualQuery`'s only real hits besides that chain are the
  confirmed-genuine CRT symbol `__ValidateEH3RN` in both binaries, plus one SP-only extra use (see next
  item).
- **SP-only Vectored Exception Handler on `STATUS_SINGLE_STEP` — chased to ground, confirmed benign.**
  `FUN_006c1690` self-locates its own `.text` bounds via the identical `VirtualQuery`-loop pattern
  `__ValidateEH3RN` uses, then registers `FUN_006c0ec0` via `AddVectoredExceptionHandler`. Full decompile of
  the handler and its dispatch target (`FUN_0043bd20`) shows a thread-ID-keyed, mutex-guarded frame-record
  lookup — classic MSVC CRT/SEH continuation machinery (most consistent with `/fp:except`-style precise
  floating-point-exception handling), not an anti-debug trick: it reads no process/module state and compares
  against no "expected clean" code bytes. It fires **only** on an actual `STATUS_SINGLE_STEP` exception
  (trap flag set) inside its own bounds — MinHook's hook install (thread-suspend + trampoline write) never
  sets the trap flag, and a plain JMP-patch executes as ordinary instructions, so neither this project's
  install step nor its hooks running during normal play can trigger it; it would only ever fire under an
  actual attached debugger single-stepping through that address range. Confirmed genuinely absent from
  `iw5mp.exe` (re-checked directly, not assumed) — a real SP/MP asymmetry, not a prior oversight.
- **External: `danielkrupinski/VAC`'s VMT check, read directly — resolves the `CreateDevice`-hook overlap
  question in this project's favor.** The repo's only "VMT" content describes VAC validating the integrity
  of **its own internal `VacProcessMonitor` object**, sourced from a filemapping `steamservice.dll` itself
  creates, checking whether ITS OWN 6 method pointers fall within `steamservice.dll`'s own base range. No
  mention anywhere of `IVEngineClient`, `IBaseClientDLL`, D3D interfaces, or any Source-engine-specific
  concept — the whole writeup is engine-agnostic and process/handle-level, not a scan of arbitrary
  game-vtables. Directly answers the open question from the first VAC-forks pass: nothing in VAC's documented
  methodology would touch this project's `IDirect3D9::CreateDevice` vtable hook.
- **External: `CWest07/COD-Demonware-AntiCheat`'s `.text`-CRC32 claim, read in full — narrower than
  previously assumed, MW3 (2011) never named.** The full repo (3 files, Xbox 360 only) explicitly scopes the
  CRC32 mechanism to **Ghosts (2013), Advanced Warfare (2014), Black Ops III (2015)**, with a separate variant
  for **Black Ops II (2012)**. MW3 (2011) predates every title actually documented and is not mentioned
  anywhere in the repo (README, source, issues, or commit history). Combined with this pass's own direct
  finding of no self-hash/self-integrity function anywhere in either MW3 binary, the `.text`-checksum concern
  is now weaker than "reasoned inference carrying over" — no such mechanism has been located in MW3 (2011)
  specifically after two dedicated RE passes plus this narrower-than-assumed source citation.
- **External: a real MW3-PC-MP-specific precedent, more concrete than anything found before.**
  `AgentRev/CoD-FoV-Changers` — a real, MW3(2011)-specific memory-writing tool with an explicit MP build
  targeting `iw5mp.exe` directly — states plainly **"MW3: VAC-safe"** (the same author labels a different
  title "UNSAFE," so this isn't blanket optimism) and reports **3 ban emails out of 10,000+ users** across
  2011-2018. Technique differs from this project's (external `OpenProcess`/`WriteProcessMemory`, not DLL
  injection — a different signature), so it's not a perfect match, but it's the single most concrete
  MW3-PC-MP "large user base, years of history, near-zero ban rate" data point found across this whole
  investigation. Separately, one real MW3 VAC ban report was found (Steam Community forum, tied to an FPS
  unlocker) — but community discussion attributes it to a genuine gameplay advantage the tool grants in this
  old engine (higher jump/movement speed), not to the hooking/modification technique itself, so it doesn't
  implicate an input-remapper with no discernible movement/aim advantage.

**Net effect of this third pass — the honest ceiling on "definitive":** every concrete, chaseable question
raised by the first two passes has now been closed by direct disassembly or a direct-source read, not
inference: no self-hash, no D3D9-vtable check, no IAT walk, no hidden telemetry content, no anti-debug
mechanism relevant to this project's hooking technique, and the one external claim that would have mattered
most (`.text`-CRC32) doesn't actually name this title. This is as close to "definitive" as static analysis of
the current retail binaries can get. **What it cannot do, and what no amount of further RE against these two
files can close**: VAC's actual server-side decision logic is proprietary, undisclosed, and runs outside
either binary — this pass proves there's nothing IN THE GAME'S OWN CODE that flags this project's technique,
not that Valve's separate, external, closed-source scanner will never flag it by some mechanism this binary
has no part in. The ~3-4 week ban-wave lag caveat from the second pass still stands unchanged. Absent a live,
long-duration, multi-wave-cycle public track record (which this project does not yet have), this is the
correct stopping point for RE-based investigation — further digging into these two binaries has run out of
concrete, unresolved threads to chase.

### Fourth pass, 4 more forks — is official matchmaking actually dead, or does something unofficial run it now? (2026-07-21)

User question, not VAC-risk-focused this time: is it true that official MW3 (2011) servers were "taken down
long ago" with only unofficial/P2P infrastructure remaining even on retail Steam? The existing "Official
Activision matchmaking is confirmed dead" line (cross-surface matrix, MP row, above) was sourced only to "a
2011-era community thread" — worth re-verifying directly rather than continuing to cite loosely. Ran RE +
local-file archaeology, a live read-only DNS/TCP reachability probe, and two external-research forks. Real
answer is more layered than a single yes/no.

- **Server discovery is architecturally split into two independent systems, confirmed via decompile
  (`iw5mp.exe`)**: (1) **Steam's own master-server/browser layer** — `net_masterServerPort` (default `27017`,
  dvar description literally "UDP port for Steam server browser") and `net_authPort` (`8766`, "UDP port for
  Steam authentication"), Valve-operated, separate from Activision entirely; (2) **Demonware's
  auth/lobby/STUN layer** — a real, decompiled state machine (`FUN_0063bae0`) that connects to
  `mw3-pc-auth.prod.demonware.net` first, then (on a status-code-700 success) to a newly-found
  `mw3-pc-lobby.prod.demonware.net`, using ports `18409`/`3074`, with STUN hosts (`mw3-stun.us/eu.demonware.net`)
  for P2P NAT traversal. These two systems can be, and appear to be, independently alive or dead.
- **Steam's server-browser layer: CONFIRMED non-functional at the application level**, via a direct quote
  from a retail dedicated-server operator's own Steam Community post, showing the game client's own output:
  **"No Steam Master Servers found. Server will LAN visible only."** Real players work around this via
  direct `steam://connect/IP:port` links or manually-added favorite-server slots (this project's own
  `players2/config_mp.cfg` on the live install has all 16 favorite slots empty — no personal history, but
  confirms the mechanism exists as a workaround players use).
- **Demonware's auth/lobby layer: CONFIRMED to resolve via DNS, and to accept TCP connections, but to give
  zero application-layer response** — a live, read-only probe (this pass, not inferred): both
  `mw3-pc-auth.prod.demonware.net` and `mw3-stun.us.demonware.net` resolve to real IPs (`185.34.107.28`,
  `185.34.107.128`); port 443 times out on both; port 80 completes a TCP handshake but returns literally
  zero bytes to an HTTP request or a raw post-connect wait, on two independent probe methods. **Direct
  comparison point**: a current, actively-used Activision auth host for newer titles
  (`auth3.prod.demonware.net`, adjacent IP block) responds on port 80 in ~200ms with a real
  `HTTP/1.1 503 Service Unavailable` — proof that host has a live application behind it. MW3's own
  subdomains show no such response at all. **Reading**: the shared Demonware network/hosting layer is up
  (DNS + TCP listener, likely a shared load balancer), but MW3(2011)'s specific auth/lobby *service* isn't
  answering — silently dead at the application layer, not a fully decommissioned domain.
- **Correction to the existing citation**: the Steam thread this project's notes previously described as
  "a 2011-era community thread" confirming matchmaking's death is actually dated **July 24, 2018** — fetched
  directly this pass. Treat "matchmaking broke by 2018" as the sourced claim going forward, not "2011-era."
- **Surprising counter-finding — Activision has NOT walked away from this title's backend entirely.** A
  scheduled maintenance window on **July 2, 2025** explicitly listed MW3 (2011) among 10 CoD titles taken
  offline together for ~4 hours (newgamenetwork.com/pcgamesn.com coverage, corroborated). This is real,
  recent (≈1 year old at time of writing) evidence of active Activision operational involvement with this
  title's backend — contradicts a clean "abandoned long ago" framing, even though the specific per-title
  auth/lobby service is non-responsive today. Most likely explanation: MW3 shares underlying Demonware
  infrastructure/maintenance windows with newer titles as a batch, without anyone specifically restoring or
  prioritizing MW3(2011)'s own service.
- **Cross-reference (2026-07-21): `iw5mp_server.exe` (App 42750) was installed and directly RE'd this same
  session**, by the sibling `MW32011NSP` project, comparing its server-side code against `iw5mp.exe`'s own
  built-in host role — near-identical structure (same source) but not byte-identical, no shared addresses,
  one real asymmetry (`iw5mp_server.exe` has an extra `"queryserverinfo"` OOB command absent from
  `iw5mp.exe`). Not VAC-relevant, but directly relevant to this file's own "What retail players actually use
  today" finding below, since it's the same binary referenced there. Full detail in `MW32011NSP`'s own
  `re_notes/vulnerability_research.md`, "Extended to `iw5mp_server.exe`" section.
- **What retail players actually use today, converging evidence from two independent research angles**: a
  small pool of **community-run, third-party dedicated servers** (via the free `iw5mp_server.exe` Steam Tool
  app, App ID 42750) — GameMonitoring.net lists 16 total, 11 currently online, reached via direct-connect or
  favorites, not server-browser discovery. A direct player quote (Steam Community "Is this game dead?"
  thread) distinguishes this from the real population: **"MW3 has only unranked server[s] that need... to be
  activated in the main menu... Any ranked (regular) lobby uses [a] P2P system"** — i.e. the
  community-dedicated-server pool is a low-population unranked side mode; the actual live population plays
  ranked public matches via **peer-to-peer / host-migration** (consistent with the threat-model note already
  in `CLAUDE.md`'s MW32011NSP section). No evidence found of a community-run Demonware-matchmaking-emulator
  specifically for MW3 (2011) — and real evidence the community deliberately avoids building one: the
  `IW5M`/AlterIW project explicitly states it does NOT emulate matchmaking, citing that a prior project
  (`alterIW`) received a real Activision C&D specifically for doing so.
- **Population is real and live right now**, not just "some old CCU stat": SteamCharts (steamcharts.com/app/42690)
  shows **55 playing at the moment of this check**, ~99 average and 188 24h-peak this period — matches and
  slightly refines the existing "~50-175 CCU" figure already on record.

**Net answer to the user's question**: partially right, more nuanced than "long ago, only unofficial
remains." **Right**: the actual matchmaking/server-discovery service is genuinely, confirmably broken — not
just quiet — verified two independent ways (a direct client-output quote AND this pass's own live probe
showing silence at the application layer). **Not quite right**: "long ago" is closer to ~7-8 years (sourced
to 2018) than "since 2011," and Activision has NOT fully abandoned the title's backend infrastructure — they
performed a real, dated maintenance action touching MW3 as recently as mid-2025, even though it evidently
didn't restore per-title matchmaking. The live population (~55-99 CCU) isn't purely "unofficial" either — it
plays through the base game's own real P2P/host-migration public-match code path, just without a working
master-server discovery layer in front of it; the only genuinely third-party/community piece is the small
unranked dedicated-server pool. **No change to this project's own risk posture or locked ordering** — MP
work is still unstarted, VAC is still confirmed active regardless of matchmaking health, and this finding is
informational (corrects a loosely-sourced citation, adds real mechanism detail) rather than decision-changing.

---

## 34. Glyph-patch mechanism test (`InjectFontGlyphPatchTest`, LB+RB+A) still not visually provable — wrong font targeted, corrected; no safe way found yet to actually see it (2026-07-21)

**Status: open, not resolved.** Set out to close the loop on the LB+RB+A
glyph-array-patch mechanism test (task #6/#31/#32) by making its effect
actually visible on screen — instead found the test's target,
`fonts/bigfont`, was picked on a wrong guess, and while that's now corrected
with a real, useful finding, the underlying "make it visible" problem is
still open.

**Real `textfont` enum resolved** (fresh Ghidra decompile of
`FUN_005181e0`, the real int-to-`Font*` selector every itemDef text draw
call goes through): `2`=bigfont, `3`=smallfont, `4`=boldfont,
`5`=consolefont, `6`=objectivefont, `7`=normalfont (also the fallback for
any unhandled value), `8`=extrabigfont, `9`=hudbigfont,
`10`=hudsmallfont, anything else = auto-selected by measured text width.
Cross-checked against a tally of every real `textfont` line across all 512
`.menu` files in the existing full `ui.ff` dump
(`D:\Tools\OpenAssetTools\zone_dump`): `3` (smallfont) 4243 uses, `9`
(hudsmallfont) 866, `1` (auto) 150, `6` 12, `4`/`2`/`10` 3 each, `5` once.

**`fonts/bigfont` (the mechanism test's target) is confirmed real but
confirmed NOT the main menu's title/button-list font** as the 2026-07-18
plan assumed ("best single guess for menu-title text") — that text actually
uses smallfont/hudsmallfont. Bigfont's only 3 real uses anywhere in `ui.ff`
are in `ui/ui/brightness_adjust.menu` (the brightness-calibration screen),
which only opens when `!getprofiledata("hasEverPlayed_MainMenu")` — a
real, one-time-per-profile gate, confirmed by tracing its only trigger in
`ui/ui/player_profile.menu`. Not a repeatable, on-demand test vehicle as
previously assumed.

**Forcing that screen open synthetically was considered and rejected, not
attempted**: this project's own `SetDvarByName`+`SetPlayerMenuFlags`+
`OpenMenuByName` recipe is already documented (this file, `InjectZoneLoadDebugTest`'s
own comment, and `analog_input_hooks.cpp`) as producing a **garbled render**
when called from the WndProc/`SetTimer` tick, regardless of content — a
real, pre-existing dead end, not something this pass discovered new risk
in. Re-attempting it here for `brightness_adjust` would just re-hit the
same known-broken path for no new information.

**What's actually still needed, neither done this pass**: (a) a genuinely
fresh player profile would naturally retrigger `brightness_adjust` once
through completely ordinary play — not attempted, resetting/faking the
user's own profile progress wasn't this pass's call to make; or (b) find
the real native render call site that picks a font for actual gameplay
interact-hint text (e.g. the weapon-pickup/swap hint string built by
`FUN_00568110`) and retarget the whole glyph-patch mechanism at THAT font
instead. Traced `FUN_00568110` fully — it only builds the hint STRING via
`FUN_005098e0` and never itself touches a font global or the `textfont`
selector; the actual font choice happens downstream at a still-unfound call
site. Left for whoever picks up the bind-resolver-hook work (`FUN_0061f6f0`,
still unstarted, see the 2026-07-18 fork-research section in
`ui_assets.md`) since that's the natural place this gets resolved anyway.

**No code behavior changed** — `InjectFontStructDebugTest`/
`InjectFontGlyphPatchTest` still target `fonts/bigfont` exactly as before
(the struct-layout proof they provide is font-agnostic, no reason to
re-derive it against a different font just for this). Only doc/comment
corrections landed: a correction comment in `analog_input_hooks.cpp` right
above the font-struct-diagnostic code, a flagged known-gap comment above
`InjectFontGlyphPatchTest` itself, and the full writeup in `ui_assets.md`'s
2026-07-21 entry. Builds clean (comment-only diff, verified via MSBuild,
Win32 config, 0 warnings/0 errors). **No live testing performed or possible
this pass** — no game-automation capability available to this session.

**Follow-up pass (2026-07-21, later session): static trace pushed further, then
pivoted to a live diagnostic instead of chasing the trace to its end.** Traced
`FUN_00568110`'s one real caller, `FUN_005682f0` — confirmed as the actual
interact-hint/HUD-element drawer (health-pickup icon animation, weapon-swap hint
text, etc., matching the `PLATFORM_PICKUPHEALTH`/`PLATFORM_PICKUPNEWWEAPON`
strings already known from earlier research). Its hint-text draw call,
`FUN_0051f6c0`, was followed downward through `FUN_005342a0` → `FUN_0051b100`
(writes an **opcode-`0x11` "print text" entry into a deferred render-command ring
buffer**, `DAT_021ddf30` — text drawing here is NOT immediate, it's queued for
later processing, a real structural finding not previously documented) →
`FUN_00691ca0` (the real ring-buffer **consumer**, walks entries via their real
size field at `+0x2`, confirmed via matching offset math against the writer) →
`FUN_00690c80` → `FUN_0047dfa0` (the already-confirmed real glyph-lookup
function). **Conclusively confirmed**: the font is NOT selected via the generic
`textfont`-int/`FUN_005181e0` menu-itemDef mechanism for this class of text at
all — it's threaded as an explicit argument the entire way down (`FUN_00690c80`'s
4th parameter, confirmed as the real `Font_s*` via `*(undefined4*)(param_4+0xc)`
matching the confirmed `Font_s.material` field at `+0xC` exactly), sourced from a
generic, data-driven HUD-element render pipeline. Traced one level further up
(`FUN_005682f0`'s own font-carrying parameter came from `FUN_00459d80`, itself
reachable only from `FUN_005096d0`, a 24-parameter generic HUD-element dispatcher
with per-element-type special cases) before concluding that continuing to chase
the static trace to its ultimate origin (whatever populates this specific
element's font field — a data table or native/GSC hud-element-creation call, not
yet located) was less efficient than a direct empirical answer.

**Pivoted to a live, read-only diagnostic instead — `Hook_DrawGlyphText` on
`FUN_00690c80` (disassembly-confirmed ordinary function: plain `PUSH EBP; MOV
EBP,ESP; AND ESP,0xfffffff8; SUB ESP,0x94` prologue, EBP-relative stack args
throughout, no thunk involved — safe to hook by this project's own established
standard).** Installed as a permanent hook (wired live, since it only ever reads
and forwards, never mutates — same safety class as the boot-thunk diagnostic),
gated for LOGGING only by a new `[Experimental] HudFontIdLogging` config toggle
(default on). Reads the real `Font_s*` passed as `FUN_00690c80`'s 4th argument on
every call, and logs its `fontName` string (reusing the already-confirmed
`DiagFont` struct layout) whenever it **changes**, deduped so a busy HUD session
doesn't spam the log. Since this function is the universal glyph-draw call for
ALL on-screen HUD/menu text (not just interact hints), the next real play session
will show, empirically and directly, every real font name actually rendered —
including, whenever a genuine interact hint appears on screen, exactly which one
that is — without needing to finish the static trace at all. Builds clean (0
warnings/0 errors, full rebuild, MSBuild Win32/Release). **Not yet live-tested**
— next launch's `proxy_d3d9.log` should show one or more `[hud-font-id]` lines
within the first few seconds of reaching any menu or HUD, which is the concrete
next step whenever this is picked up. The existing `InjectFontStructDebugTest`/
`InjectFontGlyphPatchTest` (targeting `fonts/bigfont`) are unchanged — this is a
new, additional, parallel diagnostic, not a replacement.

**LIVE-TESTED (2026-07-21), real font usage data gathered — retargeted the
struct/patch mechanism tests at the real winner.** The `hud-font-id` diagnostic
above got its first real playtest: a long, clean, ~18,500-line Survival session
(`so_survival_mp_underground`), no crash, clean exit. Real tally of every font
logged:
```
fonts/hudBigFont    7929 uses
fonts/smallFont     4860 uses
fonts/hudSmallFont  2277 uses
fonts/extraBigFont  1648 uses
fonts/objectiveFont 1360 uses
fonts/bigFont        117 uses
```
`hudBigFont` is the dominant real HUD font by a wide margin — confirms the
theoretical retarget candidate this entry already flagged (`objectiveFont`, on
name alone) wasn't the only real option, and gives a data-backed reason to try
`hudBigFont` first. `bigFont`'s 117 uses are consistent with the earlier
one-time-brightness-screen finding (117 is plausible for 3 static itemDefs
redrawn every frame across however many seconds that screen was ever on
screen this session, not evidence it's a real interact-hint font).

**Retargeted the mechanism, added alongside the bigfont versions, not
replacing them** (`analog_input_hooks.cpp`): `InjectFontStructDebugTest_HudBigFont`
(read-only struct dump, `LB+RB+X`) and `InjectFontGlyphPatchTest_HudBigFont`
(borrowed-UV reallocate-and-repoint mechanism test, `LB+RB+B`) — identical
mechanisms to the already-proven bigfont versions, just calling
`FindOrLoadFont("fonts/hudbigfont")` instead. Distinct combos from every existing
one (`LB+RB` and `LB+RB+A` stay bigfont-only) so nothing can collide. Builds
clean (0 warnings/0 errors, full rebuild, MSBuild Win32/Release). **Not yet
live-tested** — next session should hold `LB+RB+X` for 2s to confirm hudBigFont's
struct layout matches the already-confirmed bigfont one (expected, since
`FUN_0047dfa0`'s lookup logic is generic across all 9 registered fonts), then
`LB+RB+B` to confirm the patch mechanism applies cleanly. Unlike bigfont,
hudBigFont is a genuinely repeatable, always-visible test surface (7929
real draws in one session) — once a future pass gets byte `0x81` into any
hudBigFont-rendered string, this is actually checkable on screen, closing the
long-standing "can't even see the effect" gap this whole issue opened with.

**`objectiveFont` (1360 real uses) intentionally NOT retargeted this pass** —
one font at a time keeps each test's result unambiguous. If hudBigFont's
struct dump or patch test comes back wrong/implausible, `objectiveFont` is the
next real candidate to try, via the exact same pattern (copy
`InjectFontStructDebugTest_HudBigFont`/`InjectFontGlyphPatchTest_HudBigFont`,
swap the font-name string, pick two more distinct combos).

**Explicitly NOT attempted this pass**: rebuilding the offline extended-atlas
asset (`bigfont_glyph_ext.ff`'s equivalent for `hudBigFont`) with REAL new
glyph pixel content. That's a separate, much bigger undertaking — a full
`Unlinker`/`Linker` pipeline pass following `ui_assets.md`'s already-documented
"Font pipeline" steps, but against `hudBigFont`'s own real material/atlas
(dump it fresh via `Unlinker.exe`, confirm its atlas dimensions/format, extend
the canvas and rescale existing glyphs' UVs exactly as already done for
bigfont's `gamefonts_pc` atlas, rebuild via `Linker.exe` under unique asset
names to avoid the same interning collision already solved for
`bigfont_glyph_ext.ff`). The borrowed-UV mechanism tests above prove the
PATCH mechanism works against hudBigFont; real pixel content is still a
separate, unstarted step, same as it always was for bigfont.

**LIVE-TESTED (2026-07-21), found a real bug: hardcoded 0x81 collided with an
existing hudBigFont glyph, fixed with runtime codepoint discovery.** The
`LB+RB+B` patch-mechanism test above got its first real playtest. Log showed:

```
[hudbigfont-patch-test] codepoint 0x81 already exists at index 128 -- aborting, nothing to insert
```

**Root cause:** unlike `fonts/bigfont`, `fonts/hudBigFont` already has a real
glyph defined at codepoint 0x81 — this font has 254 real glyph entries total
(confirmed via `InjectFontStructDebugTest_HudBigFont`'s struct dump), almost
certainly full extended-Latin coverage for localization. The test's own
abort-on-collision logic is correct and intentional (never clobber a real
existing glyph) — the actual bug was purely that `0x81` was a bad hardcoded
"surely unused" assumption for this specific font, so the insert-and-repoint
code path never executed and the test produced no visible change, silent
failure by design rather than a crash.

**Fix (`InjectFontGlyphPatchTest_HudBigFont` only — the bigfont version,
`InjectFontGlyphPatchTest`, is untouched and still hardcodes 0x81, which is
fine there since bigfont has no entry at 0x81):** replaced the hardcoded
`const unsigned short kNewCodepoint = 0x81;` with a runtime scan. The
existing insertion-point search loop (over the sorted `[96, oldCount)` tail)
was extended, not duplicated, to also track a `candidate` codepoint starting
at `0x81`: whenever a real entry's `letter` equals the current candidate,
the candidate is taken, so it's bumped to the next value and the walk
continues; the first entry whose `letter` is greater than the (possibly
bumped) candidate proves that candidate is free and gives the correct sorted
insertion index at the same time, in the same single pass. If every
codepoint from `0x81` through `0xFF` turns out to be taken, the code logs a
clear abort message (`"every codepoint from 0x81 to 0xFF is already taken"`)
and returns rather than looping forever or writing out of bounds. The two
log lines that referenced a hardcoded `0x81` (`"codepoint 0x%02X already
exists"` and the final `"patch applied"` message) now report whatever
codepoint was actually chosen at runtime via the same `kNewCodepoint`
variable, now computed rather than literal.

**Incidental fix required alongside:** the final "patch applied" log message
is long (~330 characters) and was previously passed to `LogFromController`
as a plain string literal, so the function's `char buf[200]` never had to
hold it. Converting that message to a `sprintf_s` (needed to interpolate the
runtime codepoint) would have overflowed a 200-byte buffer and invoked
`sprintf_s`'s invalid-parameter handler (i.e. `abort()`) at runtime — `buf`
was widened to `char buf[400]` in the same commit to make that safe.

**NOT yet re-verified live** — this is a code fix awaiting the next
playtest, not a confirmed-working fix. Builds clean (0 warnings/0 errors,
full rebuild, MSBuild Win32/Release, verified this pass). Next session should
hold `LB+RB+B` again and confirm the log now reports a genuinely free
codepoint (expected to be `0x82`, the next value up, since hudBigFont's real
coverage is unknown beyond "254 entries, has 0x81") and that the insert/
repoint path actually executes this time instead of aborting.

**LIVE-TESTED, runtime-codepoint fix confirmed working** — the `LB+RB+B`
patch-mechanism test got its second real playtest with the fix above in
place. Log showed:
```
[hudbigfont-patch-test] built replacement array (254 -> 255 entries), inserted codepoint 0xA0 at index 159, repointing live Font_s now
```
No crash. Notably the free codepoint found was `0xA0`, not the `0x82`
predicted above — hudBigFont's real coverage runs contiguously from `0x81`
through `0x9F` with no gaps (`0x9D`/`0x9E`/`0x9F` line up exactly with the
`ps_l1`/`ps_r1`/`ps_l2` controller-glyph codepoints already known from the
button-glyph substitution table elsewhere in this project — consistent with
this font already carrying real, in-use extended-Latin-plus-glyph coverage
for other platforms' builds, not a sparse/arbitrary set). The insert/repoint
path executed this time (previous test aborted at the `0x81`-collision
check); mechanism confirmed sound end-to-end, glyph-array level. Still
unconfirmed: whether the newly-inserted glyph at `0xA0` actually RENDERS —
nothing in the game draws that byte yet, which is the exact gap the
visibility-test entry below addresses.

**Visibility-test pass (2026-07-21, this session): implemented and shipped —
`InjectFontGlyphVisibilityTest_HudBigFont`, `LB+RB+Y`.** Closes the
long-standing "can't even see the effect" gap this whole issue opened with,
without touching the glyph-patch mechanism itself.

*Research first, per this task's own instructions*: re-confirmed the full
real draw-call chain already documented above (`FUN_00568110` →
`FUN_005682f0` → `FUN_0051f6c0` → `FUN_005342a0` → `FUN_0051b100` [queues an
opcode-`0x11` entry into deferred ring buffer `DAT_021ddf30`] →
`FUN_00691ca0` [ring-buffer consumer] → `FUN_00690c80`/`Hook_DrawGlyphText`
[already hooked, permanently installed, read-only until now] →
`FUN_0047dfa0` [glyph lookup]) is unchanged and still the right target — no
new Ghidra pass was needed since this chain, and `Hook_DrawGlyphText`'s own
disassembly-confirmed safety (plain prologue/epilogue, no thunk, already
proven live via 7929 real calls/session with zero crash), were already
established by the earlier passes in this same issue.

Two approaches were weighed, per the task brief:
- **(a) Hook the already-installed `Hook_DrawGlyphText` and, only when armed
  by a new combo, rewrite a LOCAL COPY of the very next matching call's
  string to append the inserted codepoint, forwarding the copy instead of
  the original.** Chosen. Reuses an already-live, already-safe hook with no
  new `MH_CreateHook` call at all; the real buffer the game owns is only
  ever read, never written; one-shot, SEH-wrapped, falls back to a normal
  unmodified draw call on any exception.
- **(b) Find a simpler real string source (e.g. the "screenshot" console-
  command anchor technique) known to route through hudBigFont, and inject
  there instead.** Investigated and rejected: the existing 2026-07-15
  investigation record (see the comment block above
  `InjectControllerWeaponNext` in `analog_input_hooks.cpp`) already proved
  `Cbuf_AddText`/`Cmd_ExecuteString` (found via that exact "screenshot"
  anchor) drive the CONSOLE COMMAND dispatcher, which does not print text
  through `FUN_00690c80` at all — real HUD/interact-hint text reaches that
  function via the completely separate, data-driven deferred-render-
  command-ring-buffer path traced earlier in this issue. There is no known,
  always-available "print this exact string via hudBigFont" call site to
  anchor on, so (b) would have meant finding and calling some new,
  not-yet-confirmed-safe function — strictly more risk than (a) for no
  clear benefit. Not attempted.

**Implementation** (`proxy_d3d9/src/analog_input_hooks.cpp`): three new
globals declared just above `Hook_DrawGlyphText` (`g_hudBigFontPtr`,
`g_hudFontPatchInsertedCodepoint`, `g_hudFontVisibilityArmed` — declared
early because the hook needs to see them, same relative-ordering convention
this file already uses for its other font-test state). `Hook_DrawGlyphText`
gained a block at its top: if armed and `fontArg` is confirmed by POINTER
IDENTITY to be the exact, already-patched hudBigFont `Font*`, builds a
`char[512]` local stack copy of the real text (`strnlen`-capped, SEH-
wrapped), appends the inserted codepoint + null terminator, and forwards
that copy to the real trampoline instead of the original — then returns,
skipping the normal unmodified forward at the bottom of the function for
that one call only. `InjectFontGlyphPatchTest_HudBigFont` (`LB+RB+B`) now
also records `g_hudBigFontPtr`/`g_hudFontPatchInsertedCodepoint` immediately
after a successful patch (single source of truth for "what did we actually
insert"). A new function, `InjectFontGlyphVisibilityTest_HudBigFont`, gated
behind a THIRD distinct combo (`LB+RB+Y` — never collides with `LB+RB`,
`LB+RB+A`, `LB+RB+X`, or `LB+RB+B`), arms the injection on a 2s hold; if the
patch test hasn't run yet this session (`g_hudBigFontPtr == nullptr`), it
logs a clear message and still consumes its one-shot trigger rather than
silently no-op'ing, matching every other font-test combo's convention in
this file. Wired live into `InjectMenuInputTick`.

Tagged `[hudbigfont-visibility-test]` throughout. Builds clean (0
warnings/0 errors, full rebuild, MSBuild Win32/Release, verified this
pass). **NOT yet live-tested** — next session should: hold `LB+RB+B` (if
not already done this session) to patch and record the codepoint, then
hold `LB+RB+Y` for 2s while any real hudBigFont text is on screen (HUD is
up during normal gameplay/Survival) and watch both the log (should show
"armed injection firing" then "forwarded the modified copy... check the
screen now") and the actual screen for a visible borrowed 'A' glyph
appended to whatever HUD text was on screen at that moment (ammo counter,
compass, etc. — exact element depends on what's drawn in the very next
`FUN_00690c80` call after arming, which isn't individually selectable).

**Live-tested (2026-07-21): ran clean, no visible glyph appeared.** Log
confirmed the full pipeline fired exactly as designed with no crash/exception:
`built replacement array (254 -> 255 entries), inserted codepoint 0xA0 at
index 159` followed by `armed injection firing -- real hudBigFont draw call
text was "Armor         250" (len=17), appending codepoint 0xA0 ...` and
`forwarded the modified copy to the real draw call with no exception`. User
confirmed no borrowed glyph actually rendered on screen. Root-caused below.

**Follow-up pass (2026-07-21, later session): root-caused via fresh Ghidra
disassembly of the whole draw chain — real bug found, one level upstream of
the glyph lookup itself.**

**Correction to this sub-entry's own investigating pass:** the pass below was
run from a git worktree that, due to a branch-point staleness issue unrelated
to this project's own code, did not yet contain
`InjectFontGlyphVisibilityTest_HudBigFont`/the mutating `Hook_DrawGlyphText`
(both are real, already committed on `main` as of the `LB+RB+Y` feature commit
— they are exactly the code whose live test is described in the paragraph
above). The investigating pass's own claim that this code "isn't in this repo
as committed" was true only of its own stale checkout, not of the project.
The disassembly findings below are about the real, unchanging game binary
(`iw5sp.exe`) rather than about this project's own source, so they remain
fully valid despite that staleness — corrected here so the record is accurate
for anyone reading this later.

**Method**: re-used this project's own existing Ghidra project
(`D:\Tools\ghidra_projects_glyphrenderfork\MW3.gpr`, program `/iw5sp.exe`,
already analyzed) headlessly via `analyzeHeadless.bat` + this repo's own
`re_notes/ghidra_scripts/DecompileFuncs.java`, `-process iw5sp.exe -readOnly
-noanalysis` (skips re-running auto-analysis, ~2s per invocation instead of
~50s) so nothing was written back to the shared project file. Decompiled, in
order: `FUN_0047dfa0` (glyph lookup), `FUN_00690c80` (the draw-call this
project already hooks), `FUN_004db3e0`/`FUN_005323c0` (the char-decode chain
feeding the lookup), `FUN_004e4010`/`FUN_004b99f0` (caret/color-code escape
helpers), and `FUN_00691ca0`/`FUN_0051b100` (the ring-buffer consumer/writer
pair already named in this issue's earlier trace).

**Q1 — live lookup or stale cache?** `FUN_0047dfa0` does a genuine LIVE
per-character lookup against the real array on every call — confirmed via
disassembly, not inferred:
```c
int FUN_0047dfa0(int param_1 /*Font_s* */, uint param_2 /*codepoint*/) {
  if (param_2 - 0x20 < 0x60)                      // direct-indexed ASCII 0x20-0x7F
    return *(int*)(param_1+0x14) + (param_2*3 - 0x60)*8;
  iVar2 = *(int*)(param_1+8) - 1;                 // glyphCount-1, read fresh
  iVar4 = 0x60;
  if (0x5f < iVar2) {                             // binary search over [0x60, glyphCount)
    do {
      iVar1 = (iVar4+iVar2)/2;
      uVar3 = (uint)*(ushort*)(*(int*)(param_1+0x14) + iVar1*0x18);  // glyphs[iVar1].letter, fresh read
      if (uVar3 == param_2) return glyphs + iVar1*0x18;
      if (uVar3 < param_2) iVar4 = iVar1+1; else iVar2 = iVar1-1;
    } while (iVar4 <= iVar2);
  }
  return *(int*)(param_1+0x14) + 0x150;           // fallback: FIXED index 14 (='.' in the
}                                                  // direct-indexed range) -- not a "tofu"/blank
                                                   // glyph, an ordinary period.
```
`param_1+8` (glyphCount) and `param_1+0x14` (glyphs pointer) are re-read from
the live struct on every single call — no cached/precomputed table anywhere in
this function. This exactly matches `analog_input_hooks.cpp`'s own `DiagFont`
layout (`glyphCount` at `+0x08`, `glyphs` at `+0x14`) and `DiagGlyph` layout
(`letter` as `unsigned short` at `+0x00`, confirmed 24-byte/`0x18` stride via
the existing `static_assert`) — struct offsets are correct, not the bug.
**Answer: live lookup, mechanism is sound at this layer; the bug is elsewhere
in the chain, not a stale cache.**

**Q3 answered before Q2 (checked first per the task's own priority order) —
signed-vs-unsigned char handling.** Traced the full character pipeline a
drawn byte actually goes through before reaching `FUN_0047dfa0`:
`FUN_00690c80`'s per-character loop calls `FUN_004db3e0(&local_78, 0,
local_3c)` (`local_3c` = a forced-case mode: 0=none, 1=upper, 2=lower, derived
from the draw call's own flags) to decode the next character, and (for the
ordinary, non-escape-code path) whatever it returns is passed **byte-for-byte,
unmodified**, straight into `FUN_0047dfa0` as `param_2` — the two functions
communicate via a **bit-reinterpret through a `float`, not a numeric
conversion**: `FUN_00690c80` declares the intermediate as `float fVar7 =
(float)FUN_004db3e0(...)`, but the values it's later compared against
(`1.31722e-43`, `1.4013e-44`, `1.82169e-44`) are denormalized floats whose raw
32-bit patterns equal the small integers `94`('^'), `10`('\n'), `13`('\r')
respectively — i.e. this is the classic idTech NaN/denormal-boxing trick
(pass a tagged int through an FPU-register-sized slot by reusing its exact
bit pattern), not a real int→float value cast. Confirmed this preserves the
byte's numeric value exactly, both in and out:
- `FUN_004db3e0` → `FUN_005323c0`: for a non-DBCS codepage (`DAT_01bf6944==0`,
  the expected case for an English retail install — no CJK double-byte
  handling), `FUN_005323c0` reduces to `param_1 = param_1 & 0xff; ...; return
  param_1;` — an explicit **unsigned** mask-and-return of the raw byte, no
  sign extension anywhere. `0xA0` in, `0xA0` (160) out, every time.
- `FUN_004db3e0`'s own case-folding tables (`iVar1` = a locale/codepage-variant
  selector at `*(int*)(DAT_01bf6938+0xc)`, values seen: `0`, `6`, `7`): checked
  byte `0xA0` against all three variants' upper- AND lower-case tables by
  hand from the decompile — **`0xA0` is returned completely unmodified in
  every single branch** (`iVar1==0` returns unconditionally; `iVar1==6`/`7`'s
  explicit-case switches don't list `0xA0` and its default/range checks all
  evaluate false for it). `0xA0` survives the whole chain intact regardless of
  locale variant or forced-case flag.
- Byte `0x81` (the codepoint the ACTUALLY-committed `InjectFontGlyphPatchTest_HudBigFont`
  uses) is **not quite as clean**: under `iVar1==6` (one of the two non-default
  locale table variants) **with lowercase forced** (`param_3==2`), there's an
  explicit `case 0x81: return 0x83;` — so `0x81` specifically has one real,
  narrow corruption path in this function, though it requires both a non-
  English-default locale table AND a lowercase-forcing draw call to trigger,
  neither of which is expected for ordinary English HUD ammo-count text. Not
  ruled out with 100% certainty (didn't trace what sets `iVar1` at runtime),
  but very unlikely to be the actual explanation for a clean English retail
  session. Worth noting as a concrete, if minor, reason to prefer `0xA0` over
  `0x81` for any future glyph-injection codepoint choice — `0xA0` has no such
  collision in any branch checked, `0x81` has exactly one.
- `FUN_0047dfa0` itself declares `param_2` as `uint` and does every comparison
  unsigned (`uint uVar3 = (uint)*(ushort*)(...)`) — no signed-char comparison
  exists anywhere in the actual lookup.

**Answer: the signed-char hypothesis does NOT pan out** for this pipeline —
confirmed via disassembly, not assumed. Every stage that touches the byte's
numeric value does so as an explicit unsigned quantity (`& 0xff` masks, `uint`
locals, `ushort` array reads), and the float hand-off between
`FUN_004db3e0`/`FUN_0047dfa0` is a bit-preserving reinterpret, not a value-
converting cast, so it doesn't silently renumber the byte either. `0xA0`
specifically has zero known corruption paths in this entire chain; `0x81` has
exactly one, narrow and unlikely to apply here.

**Q2 — off-by-one/insertion-order bug?** Re-checked the ACTUAL committed
insertion code (`InjectFontGlyphPatchTest_HudBigFont`, the only committed
patch function) against `FUN_0047dfa0`'s binary-search assumptions: the
insertion loop (`for (i=96; i<oldCount; ++i) { if (glyphs[i].letter >
kNewCodepoint) { insertAt=i; break; } ... }`) does a correct ascending-order
sorted insert — first index whose `letter` exceeds the new codepoint, matching
the binary search's own ascending-order narrowing (`uVar3 < param_2 ?
search-right : search-left`). **No off-by-one or reversed-comparison bug
found in the code that's actually committed.**

**So what actually explains "ran clean, nothing rendered"? Found the real
answer one level upstream of the glyph lookup, in `FUN_00690c80`'s own draw
loop — a length/count parameter captured at ENQUEUE time, long before any
hook on the draw call itself could ever influence it.** `FUN_00690c80`'s
per-string draw loop is gated by BOTH null-termination AND an explicit
decrementing counter:
```c
iVar6 = param_10;                                  // param_10 = a character COUNT, not just a hint
while (cVar4 != '\0' && iVar6 != 0) {
    ...
    iVar6 = iVar6 - 1;
}
```
Traced `param_10`'s real origin through the two ring-buffer functions this
issue's earlier session already named:
- `FUN_0051b100` (the writer, called once at the moment the HUD element's text
  is originally queued): computes `_Size = strlen(param_1)` **at that exact
  moment**, sizes the ring-buffer entry to hold EXACTLY `_Size` bytes plus a
  null terminator (`uVar5 = (_Size+0x54) & ~3`, no reserved slack), and stores
  the caller's own `param_2` argument into the entry at byte offset `+0x20`.
- `FUN_00691ca0` (the reader/consumer, called every frame the queued entry is
  drawn): reads that same stored value straight back out of the entry
  (`*(undefined4*)(iVar1+0x20)`) and passes it as `FUN_00690c80`'s `param_10`
  — i.e. **the exact same count captured once at enqueue time, replayed
  unchanged on every subsequent draw**, confirmed by matching byte offsets
  between the writer's stores and the reader's loads field-by-field.
- This project's own `Hook_DrawGlyphText` sits on `FUN_00690c80` itself — by
  construction, downstream of this entire enqueue/dequeue round trip. Any hook
  here that appends a byte to a COPY of the string but leaves `param_10`
  (the loop's actual stop condition) at its original, pre-append value
  guarantees the loop's `iVar6` hits zero and exits **exactly at the original
  string's length** — one character short of the appended byte — regardless
  of what the modified buffer actually contains past that point. The buffer
  edit is real and safe; the loop simply never gets there.

**This one architectural fact fully explains every observed symptom**: no
crash (the append itself was always memory-safe, a local-stack copy never
touching engine memory), no exception (`iVar6` reaching 0 is a completely
normal, silent loop exit, not a fault), and — this is the detail that
independently corroborates the count-cap explanation over a "found the wrong
glyph" explanation — **not even `FUN_0047dfa0`'s own fallback glyph (a plain
period, index 14, per Q1's disassembly above) rendered at the tail of the
string.** If the loop HAD reached the appended byte and merely resolved it to
the wrong glyph (e.g. via a real Q2-style insertion bug), a period would still
have appeared at the end of "Armor         250" — the total absence of even
that fallback character is exactly what a loop that never visits the extra
position at all would produce, and is hard to explain any other way.

**Conclusion for whoever fixes the real visibility-test hook next**: splicing
a byte into a draw-string COPY at `FUN_00690c80`/`Hook_DrawGlyphText` is not
sufficient by itself — the hook must ALSO increment `param_10` (the
`DrawGlyphTextFn` typedef's own `param_10` argument in
`analog_input_hooks.cpp`, already threaded through
`InjectFontGlyphVisibilityTest_HudBigFont`'s existing hook) by exactly 1 to
match the appended character, or the real draw loop will silently stop one
character short every time, with no crash and no visible symptom to
distinguish it from "the mechanism doesn't work at all." (`param_12`, used
only for a cursor/caret-blink comparison per the disassembly, does not appear
to need the same treatment, but wasn't separately stress-tested.) Also prefer
codepoint `0xA0` over `0x81` going forward per the Q3 finding above — `0x81`
has one narrow, locale/case-mode-dependent corruption path in `FUN_004db3e0`
that `0xA0` does not (already the case: the committed patch test was fixed to
runtime-discover `0xA0` in an earlier pass this same session).

**No code change made this pass** — this pass was purely disassembly/root-
cause work (see the correction note above re: this pass's own stale worktree).
The actual one-line fix (increment `param_10` alongside the string-copy
append in the already-committed `Hook_DrawGlyphText` mutation branch) is a
concrete, scoped follow-up, not yet attempted. Builds untouched by this pass
(doc-only change, no rebuild needed for this entry's own content).

---

## 35. Bind-resolver text hook (`FUN_0061f6f0`) — LOG-ONLY first pass IMPLEMENTED, not yet live-tested (2026-07-21)

**Status:** Built, builds clean (0 warnings/0 errors), NOT yet live-tested. Task #6's
other half (button-glyph text substitution), first safe increment.

Implements the first, deliberately incremental step of the fully-researched plan
already documented in `re_notes/ui_assets.md` ("Text-swap hook (`FUN_0061f6f0`)" and
"`FUN_0061f6f0`'s real calling convention, disassembly-confirmed" sections, both
2026-07-18/19). That research concluded the hook is safe to install (a structurally
different situation from the two hooks that crashed the game live this project —
the rumble dispatcher hook, issue #24, and the boot-zone-splice hook, issue #22/#30)
and explicitly recommended prototyping log-only first, with no output-buffer
mutation, before ever attempting the real glyph substitution. This entry is that
first increment, nothing more.

**What's installed** (`analog_input_hooks.cpp`, `Hook_0061f6f0` + `BindResolverLogAfterCall`):
a MinHook inline hook on `0x0061f6f0`, installed unconditionally at DLL load (permanent
hook, not a manually-triggered debug combo — this function fires naturally whenever the
game resolves bind-hint text). The naked shim stashes `EAX` (context)/`ECX` (bind-name
context) and the `[esp+8]`/`[esp+0xc]` stack args into globals, overwrites the incoming
return-address slot with a local label instead of pushing a new one (so the trampoline
call doesn't shift the stack args by 4 bytes — see the shim's own header comment for
the full mechanics), tail-jumps into the real trampoline with the byte-for-byte original
frame intact, then once the trampoline's own `ret` hands control back, runs a normal
C++ logging function before resuming the real caller exactly where it would have
resumed had this hook never existed. Real text resolution is completely untouched in
this pass — no buffer write happens anywhere.

**Logging behavior**: `BindResolverLogAfterCall` logs `EAX`, and `ECX` treated
tolerantly (this project's own prior research explicitly flagged that ECX's exact
identity as a safely-dereferenceable C-string pointer was never fully confirmed — "likely
EAX... not confirmed identical to contextA" — so this does NOT assume it's a string;
every dereference is validated via the existing `LooksLikeValidPointer` range check and
wrapped in `__try`/`__except`, same coarse-grained SEH pattern already established
elsewhere in this file, degrading to a raw hex log if anything looks unsafe). The
`[esp+8]` output buffer is read the same way after the real call, expected to contain
`"KEY_UNBOUND"`, a single key name, or `"%s KEY_OR %s"` per the resolver's documented
real behavior. Deduped against the last-logged resolved text (only logs on a change,
not every frame a hint happens to be re-resolved on screen) to avoid flooding the log
during normal play, on top of a full off-switch: `[Experimental] BindResolverHookLogging`
in `mw3ncp_config.ini` (default `1`) silences the logging entirely without touching the
hook itself — the hook stays installed and forwarding either way, so toggling this
carries no behavior risk.

**Not attempted in this pass, by design**: any output-buffer mutation, any glyph
codepoint substitution, any `ButtonLayout`-aware key-name matching. All of that is the
next real increment once this log-only pass is confirmed safe and its logged output is
inspected against a live session.

**Not yet live-tested** — no game-automation tooling is available in this working
context to launch the game or exercise a controller, so this cannot be confirmed
working end-to-end here. Next step whenever this is picked up: launch the game, trigger
a real interact-style hint (e.g. approach a weapon pickup), and confirm (1) the hook
installs (`MH_OK` in `proxy_d3d9.log`) without regressing boot, (2) real hint text still
displays correctly on screen (proving the trampoline forwarding is transparent), and
(3) the logged `EAX`/`ECX`/resolved-text lines look sane against what's actually
displayed.

**LIVE-TESTED (2026-07-21), partial pass — safe but the captured data isn't usable
yet.** Full user playtest with this build: `MH_OK` on both create+enable, and the rest
of the session ran completely normally afterward (`stance-diag` heartbeats every
~500ms, fire-press/release, `missile-guidance-diag`, `hold-breath-diag-v2`, `ads-fov-diag`
all continuing exactly like every known-good log, no detach, no gap) — confirms (1) and
(2) above: the hook doesn't regress boot or gameplay, and real hint-text resolution
stays transparent (the trampoline forwarding works). **(3) fails**: the hook fired
twice during play (two bursts of ~11-40 calls each, so it's genuinely being exercised,
not dead code), but every capture read as implausible:
```
[bind-resolver-diag] EAX(ctx)=0x0084E2DC ECX(bindCtx)=0x00000000 (not a plausible pointer) | limitTo1=0 resolvedText="<buffer ptr 0x00000100 not plausible>"
```
`ECX` reads as a flat `0`, and the `[esp+8]` output-buffer pointer reads as `0x100` —
neither is plausible under the documented calling convention (`ECX`=bind-name context,
`[esp+8]`=real output buffer). `EAX` itself looks like a real, plausible pointer and
differs sensibly between the two bursts (`0x0084E2DC` vs `0x0082A6E4`), so the shim is
clearly executing and reading SOMETHING real — just not what this convention predicts
for `ECX`/`[esp+8]`. Two live candidate explanations, not yet distinguished: (a) this
specific call site is the 4th, previously-undocumented caller flagged in the
2026-07-18 fork research (`FUN_00622970`, suspected key-rebind-capture UI shape) which
may not conform to the same register convention as the 3 known hint-resolution
callers; or (b) a real bug in the shim's own register-stash timing/offsets. **Also
found**: the per-change dedup did not actually suppress the repeated identical lines
within each burst (all ~11-40 lines per burst are byte-identical) — a second, smaller
bug in the same code, independent of the register-capture issue. **Status: hook stays
installed (safe, no behavior risk since it never mutates anything), but the log-only
data isn't yet a trustworthy foundation for the real glyph-substitution work — needs a
follow-up debugging pass on the shim before that's true.**

**ROOT-CAUSED via fresh Ghidra disassembly (2026-07-21, follow-up pass) — hypothesis
(a) confirmed correct, hypothesis (b) refuted; dedup bug also fixed.** Ran
`FindCallers.java` against `FUN_0061f6f0` (bindresolver Ghidra project) to get all 4
real callers' decompiles, then `DumpDisasm.java` on the two most relevant
(`FUN_00622970` and `FUN_004fafd0`, a known-good hint-resolution caller) to compare
their real push sequences byte-for-byte, since the decompiler alone doesn't reliably
show register-vs-stack argument shape for a function whose real convention isn't a
plain `__cdecl` signature.

`FUN_004fafd0`'s disassembly confirms the documented convention exactly: `EAX`/`ECX`
loaded from its own stack params right before the call (register args), then three
stack pushes landing at `[esp+4]`/`[esp+8]`/`[esp+0xc]` at `FUN_0061f6f0`'s entry —
`[esp+8]` genuinely is that caller's real output-buffer pointer.

`FUN_00622970`'s disassembly (single call site, `00622970 -> CALL 0x0061f6f0 @
006229a7`) is the smoking gun:
```
00622993  MOV ECX,dword ptr [EDI]        ; ECX = *EDI (real bind-ctx reg, per convention)
00622995  PUSH 0x0                       ; -> [esp+0xc] at entry (flag=0, consistent)
00622997  LEA EAX,[ESP + 0x4]            ; EAX = &local_100, a REAL valid stack buffer
0062299b  PUSH 0x100                     ; -> [esp+8] at entry: the literal 0x100 (SIZE, not a pointer!)
006229a0  PUSH EAX                       ; -> [esp+4] at entry: the REAL buffer pointer
006229a1  MOV EAX,dword ptr [ESI + 0x114] ; EAX = real context reg, per convention
006229a7  CALL 0x0061f6f0
```
This caller pushes its real buffer pointer at `[esp+4]` and the buffer's SIZE (`0x100`)
at `[esp+8]` — the reverse of every other caller — which is an exact, disassembly-
confirmed match for the live symptom (`[esp+8]` reading as `0x100`). `EAX`/`ECX`
register setup is textbook-correct here too (real context/bind-ctx values), which is
exactly why hypothesis (b) — a bug in this hook's own register-stash offsets — is
refuted: the shim reads the textbook-correct offsets for the convention every OTHER
caller uses; this ONE caller's own real argument shape is just genuinely different. The
live `ECX=0` is also now explained rather than mysterious: `FUN_00622970`'s body
(`DAT_01c0b1ac`/`DAT_01c0b1b4` guard, `"@MENU_BIND_KEY_PENDING"` string literal)
confirms the 2026-07-18 fork research's prediction exactly — a key-rebind-capture UI
("waiting for the next physical key press to bind"), which has no "current bind name"
to resolve while capture is pending, hence a null `*EDI`.

**Fix applied** (`analog_input_hooks.cpp`): `BindResolverLogAfterCall` now checks
`g_bindResolverRealRetAddr` against `kMenuBindKeyCaptureCallerRetAddr` (`0x006229AC`,
the real return address immediately following `FUN_00622970`'s one call site,
confirmed via the same disassembly) and skips logging for that caller specifically —
logging a single one-time note instead of either flooding the log with meaningless
data or silently looking like the hook stopped firing. The hook itself, its
installation, and its behavior for the 3 real hint-resolution callers are unchanged.

**Dedup bug also fixed, same pass**: the old code only compared/updated
`g_bindResolverLastLoggedText` when `bufReadOk` was true (a successful buffer read) —
so whenever the buffer pointer looked implausible (`bufReadOk == false`, exactly the
case that flooded the log with the `FUN_00622970` noise before the fix above), dedup
silently never engaged in either direction (no compare, no update), so every single
call logged unconditionally. Fixed by comparing/updating on `textBuf`'s content
directly regardless of `bufReadOk` — `textBuf` always holds a deterministic string by
that point (real resolved text, a faulted-read marker, or the not-plausible marker), so
this is correct in every case, not just the happy path. The now-unused `bufReadOk`
variable was removed rather than left dead.

**Build**: clean, full rebuild, 0 warnings/0 errors (MSBuild, Win32/Release).
**Not yet live-tested** — this fix could not be launched against the running game in
this pass (no game-automation tooling available); confidence comes from the
disassembly comparison above (both callers' real push sequences traced instruction by
instruction against the confirmed `FUN_0061f6f0` entry-state offsets), not a fresh
playtest. Next real launch should confirm: (1) the `FUN_00622970` skip-note logs
exactly once (not per-frame), and (2) any genuine hint-resolution call from the 3
normal callers now produces a plausible `ECX`/buffer read where it previously would
have (this pass didn't change what those 3 callers pass, only how the OTHER caller is
handled, so this should already have been working for them — worth confirming
directly rather than assumed).

**RESIDUAL MISS found in the NEXT real playtest (2026-07-21, follow-up investigation)
— fix mostly works, one still-unexplained occurrence, real logging improvement
shipped, root cause NOT conclusively found.** A subsequent full session (~18,500 log
lines, clean boot, clean exit) showed the fix working far better than before — only
ONE `[bind-resolver-diag]` garbage line the entire session (vs. 51+ across two bursts
pre-fix), confirming the dedup fix holds. But that one line still showed the exact
pre-fix symptom (`ECX=0`, buffer=`0x100`), and the `kMenuBindKeyCaptureCallerRetAddr`
skip-note **never appeared anywhere in that log** — meaning the retaddr comparison
evaluated false for that one call, i.e. the fix didn't catch it.

**Investigated fresh, from scratch, not by re-trusting the prior pass's conclusions.**
Ran a brand-new headless Ghidra enumeration of every real reference to `FUN_0061f6f0`
(`ghidra_projects_bindresolver`, a from-scratch `getReferencesTo` scan, not reusing any
cached "4 callers" claim as given) — **confirms exactly 4 real call-type references
exist, byte-for-byte matching the prior research**: `00622037`(`FUN_00622020`),
`006229a7`(`FUN_00622970`), `004be084`(`FUN_004be070`), `004fafe4`(`FUN_004fafd0`). No
5th caller anywhere in the binary — hypothesis "an uncatalogued 5th caller with a
similarly-shaped push exists" is **refuted**. Also dumped `FUN_00622970`'s ENTIRE body
fresh — confirmed only the one known `CALL 0x0061f6f0` exists inside it, no second call
site — that hypothesis is **refuted** too. And the instruction immediately following
that one call site is, byte-for-byte, `006229ac  LEA ECX,[ESP + 0xc]` — confirming
`kMenuBindKeyCaptureCallerRetAddr = 0x006229AC` is **exactly correct**, not a stale or
mistranscribed value. The shim's own read ordering was also re-traced by hand
(`EAX`/`ECX` registers read first, then `[esp+8]`/`[esp+0xc]`, then `[esp]` saved and
overwritten last) — no ordering bug found; every stashed value is read before anything
that could invalidate it.

**One lead pursued, not confirmed**: `FUN_00622970` is a "waiting for the next physical
key press to bind" capture screen (per its own body/strings) — the kind of UI that
plausibly runs a Windows message pump while blocked waiting for input, which could
cause genuine reentrancy into `FUN_0061f6f0` (a second, nested call clobbering the
shared globals mid-flight, before the first invocation's own `BindResolverLogAfterCall`
reads them). A shallow call-graph check (its one real caller, `FUN_006256a0`; the two
helper functions `FUN_0061f6f0` itself calls internally, `FUN_004d6da0`/`FUN_0061f590`)
found no directly message-pump-shaped call one hop deep — but this does NOT rule out a
pump several hops further down (e.g. inside `FUN_00622100`, the other function
`FUN_00622970` calls, or deeper inside `FUN_0057e770`) — genuinely unconfirmed either
way, not chased further given the effort-to-certainty tradeoff at this depth.

**Root cause NOT conclusively found.** With only one post-fix occurrence to go on,
there's no way to distinguish "the fix has a real, rare failure mode" from "the fix
works correctly and this was some other anomaly" from a single data point. Rather than
guess, shipped a concrete, safe improvement instead: **`BindResolverLogAfterCall`'s
diagnostic line now includes the actual observed `retAddr=0x%08X` value directly**
(previously only inferable, never logged). The next time this garbage shape appears,
the log will show the real address that failed to match `0x006229AC` — either
confirming it's the same caller (pointing hard at reentrancy or some other runtime
effect) or revealing it's a genuinely different address (pointing at a caller/edge case
this pass didn't find). This closes the loop with real data next time, instead of
another round of inference. Builds clean (0 warnings/0 errors, MSBuild, Win32/Release).
**Not live-tested** — no game-automation tooling available in this pass either.

**Glyph-substitution groundwork ADDED 2026-07-21, OFF by default, independent of the
font-loading work** (task #6, parallel to the safe-loading investigation in issue
#23): built the other half of the button-glyph feature — the actual key-name-text →
glyph-codepoint substitution logic in `BindResolverLogAfterCall`, plus a new
`GlyphStyle` config option (`mod_config.h`/`.cpp`, same enum/INI pattern as
`ButtonLayout`/`StickLayout`; values `Xbox360`/`XboxModern`/`PlayStation`, matching
`assets/button_glyphs/`'s own real file-prefix convention exactly) so a player can
pick their preferred icon look independent of physical controller brand (XInput can't
tell them apart on Windows). New `[Experimental] BindResolverGlyphSubstitution`
toggle, **default `0`, deliberately**: no font asset the running game can currently
load renders these codepoints (issue #23's safe-loading problem is still open), so
turning this on today would replace readable key-name text with missing-glyph boxes,
a regression not an improvement.

**Design, four stages** (`analog_input_hooks.cpp`, right before
`BindResolverLogAfterCall`): (1) a real key-name string (e.g. `"MOUSE1"`, `"SHIFT"`,
`"F"`) → a `LogicalAction` enum mirroring `ButtonMap`'s own fields, sourced directly
from this project's own RE-confirmed real default keyboard binds
(`players2/config.cfg`, tabulated in `iw5sp.md`'s "Button mapping" section — NOT
guessed) — covers `MOUSE1`→Fire, `MOUSE2`→Ads, `G`→Lethal, `Q`→Tactical, `F`/`R`→
ReloadUse (both real keys resolve to the SAME `PhysicalInput`, since this project's
own X handles interact-vs-reload via hold/tap), `1`/`2`→WeaponSwitch, `SPACE`→Jump,
`CTRL`→CrouchProne, `SHIFT`→Sprint, `E`→Melee, `ESCAPE`→Pause, `TAB`→Scoreboard, plus a
separate fixed D-pad-direction table (`N`/`5`/`3`/`4` → Up/Right/Down/Left,
preserving the real, already-documented `5`→slot-2-not-slot-5 quirk). (2)
`LogicalAction` → `PhysicalInput` via the EXISTING `g_buttonMap` (already correctly
resolved per the player's real `ButtonLayout`/`FlipTriggers`) — reused rather than
re-implementing layout logic. (3) `PhysicalInput` + `GlyphStyle` → a real glyph asset
name, restricted to files that actually exist in `assets/button_glyphs/` — a missing
combination returns empty/false rather than guessing. **Real gap found and left
unmapped, not papered over**: the Xbox360 asset set has no left-stick-click/right-
stick-click icons at all (only `xboxmodern_ls`/`_rs` and `ps_l3`/`_r3` exist) — Sprint
(LS) and Melee (RS) have no Xbox360-style glyph, so that specific (key, style)
combination correctly falls through to "no substitution available." (4) glyph asset
name → a single-byte codepoint, via a table of PROVISIONAL placeholder values
(sequential unused extended-ASCII bytes, `0x82`-`0xA9`, deliberately skipping `0x81`
since that's already spoken for by the existing `InjectFontGlyphPatchTest` mechanism
test) — only one codepoint has ever actually gone through the real font-build
pipeline so far, so there is no finalized scheme yet; whoever finishes issue #23's
font-loading work should reconcile these against whatever the shipped font actually
assigns, not assume this table is already authoritative.

**Substitution mechanics**: runs on every real hint-resolution call (not gated by the
existing log-dedup check, since the real hint is re-resolved every frame it's on
screen and needs the substitution every time, not just on frames this function
happens to log), gated on its own config flag + `Controller_IsConnected()` (new,
small helper added to `controller_input.h`/`.cpp` — no such "is a controller active"
flag existed anywhere in this codebase before, confirmed by checking) + the resolved
text being at least 1 character with a real mapping. **Safety invariant**: only ever
writes 2 bytes (codepoint + null terminator) into the real output buffer — since a
real single-key resolution is never an empty string, this can never exceed whatever
the trampoline's own just-completed write already used in that exact buffer, without
needing to know its real allocated size; combo binds (`"%s KEY_OR %s"`) are naturally
excluded since the lookup only matches single, exact key names. Wrapped in
`__try`/`__except` around the write itself, same paranoid-but-correct pattern as
every other real-memory touch in this file. **Refactored the surrounding function
along the way** (necessary, not scope creep): the old code returned immediately if
`bindResolverHookLogging` was off, which would have also silently blocked
substitution from ever running whenever logging was disabled — the two toggles are
supposed to be independent, so the early-return was moved to only gate the actual
`LogFromController` calls, not the substitution logic itself.

Builds clean (0 warnings/0 errors, full rebuild, MSBuild Win32/Release). **Not
live-tested** — no game-automation tool available, and the feature is off by default
regardless, so there's nothing to observe live yet even if it were tested. This is
pure preparatory groundwork, ready to enable the moment issue #23's font-loading
problem is solved and the correct render font is confirmed (a separate, parallel
thread this session).

---

## 36. Local splitscreen co-op — user roadmap idea, NOT YET INVESTIGATED (2026-07-21)

**Status: idea only, zero RE work done.** User suggested adding local splitscreen
(real dual-player, same-screen, same-machine co-op) to the project's scope as a way
to bring back more of the console experience — MW3's Xbox 360/PS3 builds shipped
real local splitscreen for Special Ops co-op. Logged here so it isn't lost, not
because any investigation has started.

**The one existing, relevant lead**: this project's very first investigation (see
`CLAUDE.md`'s "Key technical finding," 2026-07-13) already found real strings in
`iw5mp.exe` — `splitscreenactivegamepadcount`, `attachedcontrollercount`,
`@PLATFORM_USECONTROLLER1` — confirmed genuine leftovers from the shared console
codebase. At the time, these were characterized specifically as "not a working PC
INPUT path" (i.e. no XInput/DirectInput import exists to actually read a second
controller) — that finding stands and is unrelated to reading a second pad, which
this project already solved generally via its own XInput polling. **What was never
checked**: whether any of this splitscreen-adjacent code, or anything near it, still
drives an actual second local player simulation and a second rendered viewport —
a completely different and much larger question than "can we read a second
controller," which is genuinely open and untouched.

**Why this is a big ask, flagged honestly rather than undersold**: real local
splitscreen needs, at minimum, a second local client-side player/prediction state,
a second camera/view, and a split or divided render target — architecturally closer
to standing up a second client pipeline than to any hook this project has built so
far (all of which inject into a SINGLE existing player's input/usercmd/view-angle
path). Even if the console build's dual-viewport code is dormant and intact in the
PC binary (unconfirmed), reviving it would likely be one of the largest single
undertakings in this project's history — bigger in kind, not just degree, than the
controller-glyph or options-menu work.

**Not yet done, first real steps whenever this is picked up**: (1) confirm via
Ghidra whether `iw5sp.exe` specifically (not just `iw5mp.exe`, since Special
Ops/Survival — the project's actual scope — is the SP binary) carries the same or
equivalent splitscreen-leftover strings/cvars at all; (2) trace real xrefs on
whichever splitscreen cvars/strings exist to see if they reach any still-intact
second-viewport render call or second-player simulation entry point, or whether
that code was stripped/dead-ended on the PC build the same way the controller input
path was; (3) only after that feasibility question is answered should any actual
scoping/design work begin. Nothing here is implementation-ready — this is
deliberately just the roadmap entry and the one known lead.

**Second, independent lead found (2026-07-21, docs survey pass)**: `survival_mode_overview.md`
already documents Survival's real structure as built for **2-player co-op specifically** —
`ui_eog_player1_bestscore`/`_player2_bestscore`, `surHUD_performance`/`_p2` HUD fields — with
no evidence of >2-player support anywhere in that mode's own data. This doesn't confirm a
dormant dual-viewport/dual-simulation path exists (that's still the open feasibility
question above), but it's a second, independent, real signal consistent with 2-player local
co-op being a genuinely-supported design target somewhere in this engine, not purely
speculative from the leftover strings alone.

## 37. WaW-style animated dev clan tags — feasibility research (2026-07-21)

**Status: research/scoping pass only, per explicit user framing this is a
brand-new standalone feature idea. No code shipped this session** — every
candidate implementation path found has at least one genuinely unresolved
unknown, so per this project's own "no placeholder hooks" and "verify live"
bars, nothing here clears the bar for even a minimal read-only diagnostic yet.
This entry supersedes nothing — it's additive to, and directly builds on, the
existing clan-tag research already recorded in `ui_assets.md`'s "WaW-style
colored/animated clan tags" section (2026-07-18), which this pass re-read in
full rather than re-deriving.

### 1. Does MW3 have its own native clan-tag mechanism? Yes — and it's a bad fit, for reasons already found

This was already thoroughly RE'd in `ui_assets.md` (2026-07-18 session); summarized
here for this entry's own completeness, not re-investigated from scratch:

- MW3's real clan-tag system is **entirely Activision Elite-branded** — confirmed
  strings `use_eliteclan_tag`, `eliteClanTagText`, `clear_eliteclan_tag`,
  `clanPrefix`, dozens of `elite_clan_*`/`eliteclan_*` strings. There is no plain
  `clantag`/`cg_clantag`-style free string dvar independent of Elite — direct string
  search for that pattern in the earlier pass found nothing beyond the Elite-prefixed
  set.
- **It is not a simple local string.** Ghidra decompile of `FUN_00580250`/
  `FUN_00581be0` (the only real cross-references to `eliteClanTagText`) showed both
  implement a **bitstream session-SYNC protocol**: per-session-member slot data
  (`clanPrefix`, `useEliteClanTag`, `eliteClanTagText`, plus MP/SO stats) read/written
  via a generic dvar-style accessor, diffed against cached per-slot values, and pushed
  as outgoing NETWORK delta strings (`"ectatx \"%s\""`, `"ecuta %i"`) when changed. The
  clan tag is **networked lobby-member presence state**, not a saved/settable local
  string — this is a materially bigger blocker than simple color-code stripping.
  `popup_callsign` (a separate, non-Elite menu, `FUN_0054fe20`) is confirmed grouped
  with `"menu_xboxlive"`/`"menu_xboxlive_privatelobby"` in a live-session-state gate —
  strong evidence it's part of the (dead) online-session UI flow too, not an
  isolated local-only menu.
- Whether the "generic dvar-style accessor" these functions use is a real registered
  console dvar (reachable via this project's existing `GetDvarString`/`GetDvarInt`
  helpers, `analog_input_hooks.cpp`) or a custom per-session-slot struct accessor with
  a similar calling shape is **still unconfirmed** — not re-traced this pass, and
  matters a lot: if it's the latter, `GetDvarString("eliteClanTagText")` would not
  return anything meaningful even as a pure diagnostic.
- Even setting that aside, **this project's scope (SP/Survival, `iw5sp.exe`) runs
  entirely offline against a backend already confirmed dead elsewhere in this
  project** (Elite's social platform, same family of dead service as the
  matchmaking/master-server findings in issue #33). A live SP/Survival session is
  very unlikely to ever populate a real, non-empty `eliteClanTagText` value at all —
  so even a perfectly safe read-only hook on this path would almost certainly log
  nothing useful in this project's actual target modes. Low safety risk, but also low
  information value — not a good diagnostic candidate on cost/benefit grounds alone,
  separate from the accessor-type question above.
- **The one real local-only alternative already found**: `self.playername`, a GSC
  entity field with **no GSC-side assignment anywhere in the 317-file SP/Survival
  corpus** (read-only from script's perspective — strongly implying native
  engine auto-sync, the common id-engine pattern of fields like `self.health` being
  populated directly from the internal player struct). Confirmed real usage sites:
  `137.gsc:562` (`self._id_1819 settext(self.playername)`) and `181.gsc:704`
  (`var_3._id_16C6["name"] = var_3.playername`), both inside the Special Ops co-op
  pre-mission "waiting for players" ready-up screen. Displayed via an ordinary 2D HUD
  text-string builtin — genuinely a better animation-feasibility fit than a 3D
  world-space nameplate would be (see §3). **But**: only confirmed used on one
  pre-mission lobby screen, not during actual gameplay/HUD; the field's native
  population function and entity-struct offset were never traced; whether a remote
  co-op partner's copy is itself network-synced is unconfirmed. Not well-understood
  enough yet to hook, read-only or otherwise.

**Conclusion for §1**: MW3 does have a real, working, native "clan tag" concept, but
it is networked Elite-session presence data, tied to a dead backend, and not simply
reachable as a local string the way WaW's was. The one local-only candidate
(`self.playername`) is too narrowly scoped and too poorly understood (native
population point unknown) to build on directly yet.

### 2. WaW's real dev clan tags — what's actually documented (web research, this pass)

**Consistent with this session's starting background** (Treyarch shipped ~22 hidden
animated/special clan-tag magic-word strings, reachable only via a since-patched
save-data/timing glitch — not a legitimate in-game unlock — pure GSC/UI-script-layer
content on WaW's own engine, no engine-level hooking involved on Treyarch's side).
This pass's own web research corroborates the shape of this and adds specific,
citable examples, but **no single source enumerates all ~22 codes with exact visual/
timing specs** — treat the following as the real, verifiable subset, not the full
list:

| Code | Documented effect | Source |
|---|---|---|
| `....` (four dots) | An "o"/moving dot bounces back and forth through the dots next to the tag | [GameFAQs Q&A](https://gamefaqs.gamespot.com/xbox360/944199-call-of-duty-world-at-war/answers/49283-other-colored-clan-tags), [TheTechGame](https://www.thetechgame.com/Archives/t=471659/waw-clan-tags-glitch-codes.html) |
| `****` (four asterisks) | A "+" bounces through the tag the same way | same sources |
| `MOVE` | Tag scrolls/bounces left-to-right across the screen | same sources |
| `RAIN` | Animated rainbow of colors scrolls through the tag | same sources |
| `CYCL` | Colors scroll left-to-right through the tag, then the tag itself disappears | same sources |
| `CYLN` | A red "laser"/highlight sweeps through the tag, alternating letter-by-letter | same sources |
| `GOLD` | Solid gold-colored tag (static, not animated) | same sources |
| Static color words (`blue`, `Cyan`, `Grn`/`grn`, `Red`, `YELW`/`yelw`, `RNBW`) | Solid single-color tags; distinct from the animated set above, and some (`RNBW`-style) were legitimately reachable, not glitch-only | [GameRevolution](https://www.gamerevolution.com/guides/49518-call-of-duty-world-at-war-online-clan-tag-codes) |

Additional corroborating detail found this pass: community sources describe Treyarch
having "put in twenty-two unique clan tags because of the wait for patching glitches
on the PlayStation 3" — i.e. the ~22 figure and the "these were left in because a fix
was already queued and PS3 patching lead times were long" rationale are both
real community claims, not this project's own invention, though **no primary
Treyarch/Activision source was found confirming the exact number 22 or the full
roster** — this remains community lore with consistent secondary corroboration, not
a verified developer statement. **None of these codes function on a patched client**
(commonly cited as v1.04+) — consistent with the "glitch, since closed" framing in
this session's background. **No source found specifies an exact animation frame
rate/timing** for any of the positional effects (bounce speed, scroll speed, etc.) —
"1:1 timing fidelity" will require eyeballing archival video, not a discoverable spec
(none of the fetched pages included frame-rate detail; several candidate fan-wiki/
forum pages, e.g. the CoD Fandom wiki's own `Clan_Tag` article and the NextGenUpdate/
Se7enSins/Neoseeker threads, returned HTTP 402/403 to direct fetch this pass and were
only readable via search-result snippets — the codes above are the ones that
survived to a citable snippet, not necessarily the complete real set).

### 3. Integration plan for MW32011NCP

**Recommendation: do not attempt to reuse either of MW3's own native clan-tag paths
(Elite-networked, or the narrow `self.playername` field) as the mechanism — build a
fully separate, project-owned overlay instead.** This mirrors this project's own
precedent in issue #6 (Sprint stamina/cooldown): when the real native timer/mechanism
couldn't be found or was a bad fit, the intentional design chosen was **a project-
owned timer/state layer, not a workaround** — same shape of decision applies here.

Proposed pieces, in dependency order:

1. **Config surface** — a new `[Experimental]` (or standalone `[ClanTag]`) config
   entry in `mod_config.h`/`.cpp` (same file/pattern as `ButtonLayout`/`GlyphStyle`),
   e.g. a free-text `DevTag` string plus an effect selector matching the table in §2
   (`Bounce`, `Plus`, `Move`, `Rain`, `Cycl`, `Cyln`), default off/empty so the
   feature is fully inert unless a player opts in.
2. **Our own animation-timer state machine** — a new, standalone source file (e.g.
   `clantag_overlay.cpp`, not touching `analog_input_hooks.cpp`,
   `d3d9_hook.cpp`'s existing hooks, or the bind-resolver/hudBigFont code per this
   task's explicit scope fence) that advances an animation-phase counter once per
   real frame/tick and rebuilds the display string from it (moving the bounced
   character's index for `....`/`****`, rotating a color-code table for
   `RAIN`/`CYCL`/`CYLN`, etc.) — this part is ordinary, low-risk string/timer logic,
   not an RE unknown.
3. **A real per-frame drive point** — needs to advance every real frame during actual
   gameplay/menus. Two candidates, both with open problems:
   - Piggyback the confirmed-firing `FUN_0057de60` per-frame usercmd hook already in
     `analog_input_hooks.cpp` purely for **timing** (increment our own counter there,
     no drawing) — safe and cheap, but per this project's own finding, that hook
     **halts while the game is paused**, so animation would freeze during pause
     unlike a real always-running effect. Acceptable for a first cut, not "1:1."
   - The `WndProc`/`SetTimer`-driven `WM_TIMER` path (`d3d9_hook.cpp`) already proven
     to keep running during pause (used for Start's pause-menu open/close) is a
     better timing source for uninterrupted animation, but has never been used to
     drive anything other than menu-state polling — reusing it for a continuous
     animation counter is untried, not just unproven-risky.
4. **Actually drawing the text on screen — the single biggest open unknown.** This
   project has **no confirmed-working per-frame render hook today.** The obvious
   candidate, `IDirect3DDevice9::Present`, is **confirmed dead in this exact binary**
   (see `CLAUDE.md`'s architecture-direction §2 and `d3d9_hook.cpp`'s own comments —
   a fire-counter diagnostic showed the detour never fires, likely because Steam
   Overlay already owns that vtable slot). The only vtable hook currently installed
   and confirmed alive is `IDirect3D9::CreateDevice`, which fires once at device
   creation, not per-frame — useless for drawing. Two untried options, **neither
   attempted this pass**:
   a. Hook `IDirect3DDevice9::EndScene` (a different vtable slot than `Present`,
      historically a common alternative overlay hook point for exactly this class of
      external-hook conflict) and draw our own text there via `ID3DXFont`/
      `ID3DXSprite` or raw vertex/text primitives — entirely independent of the
      game's own HUD/text-draw system. Untested whether `EndScene` suffers the same
      fate as `Present` in this binary (plausible, since overlays commonly hook both,
      but not confirmed either way).
   b. Instead of a new render hook, call the game's **own** native 2D HUD-text-draw
      function (the one the MOTD ticker / `self.playername` HUD text ultimately
      resolves to — not yet located/named) directly, passing our own computed
      string, from within an already-firing hook. This avoids needing any new render
      hook at all, but has its own unresolved risk: it's unconfirmed whether calling
      a HUD-draw native function is safe/valid from a non-render call context (e.g.
      from inside the usercmd-build tick, which is NOT guaranteed to run between the
      device's `BeginScene`/`EndScene` or with whatever globals the draw function
      expects already set up) — could crash if the internal state it assumes isn't
      actually valid at that call site. Needs a dedicated Ghidra trace of that
      function's real callers/expected call context before it's touched, not an
      assumption either way.
5. **Color rendering** — if path 4b (piggybacking the game's own text-draw call)
   pans out, the `RAIN`/`CYCL`/`CYLN` color effects likely inherit the already-
   confirmed `^1`-`^7` color-code renderer used elsewhere in this project's HUD/menu
   text research "for free" (not independently re-verified for whichever specific
   draw function ends up being used). If path 4a (our own raw D3D9 draw) is used
   instead, color cycling has to be implemented ourselves character-by-character —
   more code, but no dependency on the game's renderer at all.

### Open risks / unknowns, ranked by how much they block the whole feature

1. **No confirmed-alive per-frame render hook exists in this project today** — the
   biggest blocker. `EndScene` is untried. Until this is resolved, there is no way to
   put ANY custom pixel on screen every frame, regardless of which clan-tag mechanism
   is chosen.
2. Whether the game's own native HUD-text-draw function (path 4b) is even safely
   callable from a non-render hook context — genuinely unknown, not yet traced.
3. Whether `eliteClanTagText`'s "generic dvar-style accessor" is a real dvar
   (`GetDvarString`-reachable) or a custom struct accessor — moot given the decision
   to not build on this path, but worth resolving if a future pass reconsiders it.
4. No authoritative source for WaW's exact per-effect animation timing/frame rate —
   "1:1" fidelity will be achieved by eye against reference footage, not a spec.
5. Whether all ~22 of WaW's original dev tags are even fully known publicly — this
   pass found a solid, citable ~7-code subset (§2), not a complete roster; several
   candidate primary sources (CoD Fandom wiki, NextGenUpdate/Se7enSins/Neoseeker
   forum threads) were blocked from direct fetch (HTTP 402/403) this pass.

**Bottom line**: this is a real, buildable feature in principle, using only
techniques this project already has proven elsewhere (project-owned timers, MinHook,
config-driven toggles) — but it is gated on one genuinely unresolved engine-hooking
question (a working per-frame render hook, or confirmation that piggybacking a native
HUD-draw call from a tick hook is safe) that has nothing to do with clan tags
specifically and would need to be resolved first, generically, before this or any
other "draw something custom every frame" feature in this project can move forward.
Per this task's own instructions, no diagnostic hook is being shipped this pass —
the candidates considered (hooking the Elite bitstream-sync functions, or
`self.playername`) were both rejected above on low-information-value or
not-well-understood-enough grounds respectively, not implemented and left untested.
