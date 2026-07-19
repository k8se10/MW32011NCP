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
    warnings/0 errors, full rebuild). **Not yet live-tested.**
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
during a freeze.

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

## 32. Console look input likely had a real acceleration ramp — this project's look currently has none (2026-07-19, web research, not yet implemented)

**Status:** Open, logged as a planned optional config toggle, not yet built.

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

**Not yet implemented**: no code changes made this pass, pure documentation of the
finding + the planned toggle shape. Real next step when picked up: implement the ramp
in `InjectControllerLookAngles` itself (track time-since-stick-left-neutral per axis,
scale the per-frame angle delta by `min(1.0, elapsedMs / AccelerationRampMs)` when the
config value is nonzero), default the config value to `0` (off) until/unless
live-testing confirms players want it on by default.
