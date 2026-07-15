# Known Issues — `iw5sp.exe` (Campaign/Survival)

Tracked as tasks in the working session; this file is the standalone reference so they
don't stay buried in `iw5sp.md`'s investigation log. Update status here as each is
resolved. Last updated 2026-07-15.

---

## 1. Buy-station + pause menu completely breaks movement (HIGH SEVERITY)

**Status:** Open, not yet investigated.

**Symptom:** After using a buy station (requires mouse/keyboard per the existing
menu-navigation limitation), opening the pause menu and then exiting it leaves the
player completely unable to move. Damage/death still processes normally (confirmed by
dying afterward) — only movement input stops responding.

**Why this is worse than previously documented:** `README.md` and `iw5sp.md` already
document a narrower "menu-gate cursor" limitation — that buy-station/menu interaction
needs mouse/keyboard, and that the pause menu resets the relevant gate state itself
"unlike other menus" (see the `InjectAllControllerInput` comment header in
`analog_input_hooks.cpp`). This is different and worse: not a cursor/interaction
inconvenience, a full movement lockout that persists after the menu closes.

**What we know so far:** `InjectAllControllerInput` unconditionally clears gate bit
`0x10` at `0x00B36210` every single frame, regardless of any other state — this was the
"SETTLED" fix from the original menu-gate investigation, chosen specifically because it
worked from level start with no click needed. This new symptom implies some *other*
piece of state — set specifically by the buy-station menu, and not cleared correctly
when the pause menu is opened and closed afterward — is what's actually blocking
movement, not the bit we already force-clear.

**Confirmed still playable despite this:** user reached wave 4 on Terminal even with
this bug present, presumably by avoiding buy stations or by finding a workaround
(exact workaround not documented — worth asking).

**Next step:** live reproduction (buy station → pause → resume) with diagnostic
logging around the gate bit and any other candidate menu-state flags, to find what
actually changes and stays broken across that specific sequence. Almost certainly needs
the same kind of investigation that found the original gate bit (raw disassembly of
whatever function handles the buy-station's specific menu type, since it may set
additional state the generic pause-menu path doesn't share).

---

## 2. No real one-shot command dispatcher found yet

**Status:** Open. Three dead ends found so far (2026-07-15 session).

**Blocks:** Y (weapnext), Start (pause/togglemenu). Also relevant to any future one-shot
command work (toggleprone already works via a different mechanism — direct usercmd bit
forcing for the 3-state stance ladder — so it's not blocked by this, but a real
`toggleprone` command would have been the "correct" native mechanism if we'd found it
first).

**What we know:** `weapnext`, `togglemenu`, `toggleprone`, `vote yes`, `vote no` are all
one-shot commands (no `+`/`-` pair) living in a flat name-only table (base ~`0x00929fa4`
region, established via cross-checking against confirmed binds' real addresses). Every
one of these strings' code references are pure `DATA` references — no function
anywhere hardcodes an individual command name — meaning they're all executed through a
single generic command-table/hash-lookup dispatcher (a real
`Cmd_ExecuteString`/`Cbuf_AddText`-equivalent) that string-reference tracing cannot
reveal, since the command name only ever exists as *runtime data* passed into that
generic function, not as a literal operand anywhere per-command.

**Dead ends ruled out:**
- `FUN_004d6da0` → `FUN_0057e770`: turned out to be a HUD/UI keybind-display formatter
  (looks up "KEY_UNBOUND"-style text for on-screen prompts), not a dispatcher.
- `FUN_00567a00`: turned out to be the stance-hint icon/text lookup (which bind is
  currently assigned to crouch/prone, for HUD display), not a dispatcher.
- `FUN_00541020`/`FUN_0057e710`/`FUN_0054b9f0`: real key-event message-pump handlers,
  but none contain a generic "look up and execute a named command" branch — they only
  route to the special-bind dispatcher (`FUN_00438710`) or do nothing further.

**Next step (not yet tried):** find where a *typed console command* gets parsed and
executed (the developer console input path) — that code must call the real generic
dispatcher directly with a raw string, which the key-event path may obscure. Also
consider live memdiff/pointer-chasing tied to an actual one-shot action's observable
side effect (similar to how Sprint/Reload were eventually found), rather than more
static string-reference tracing.

---

## 3. Remaining unassigned controller inputs

**Status:** Open, tracked as tasks #3–#6 in this session.

| Input | Intended action | Blocker |
|---|---|---|
| Y | `weapnext` | Needs issue #2 resolved first |
| Start | `pause`/`togglemenu` | Needs issue #2 resolved first |
| Back | Likely `+scores` (scoreboard) | Not yet investigated — may need its own bit/offset confirmation, same as any other kbutton-array entry |
| D-pad (all 4) | `+actionslot 1-4` variants | Bits are known from the confirmed usercmd-bit table but individually unconfirmed against real gameplay effects — old table-order-guessed identities (inventory wheel, weapon attachment, NVG toggle) are unreliable and need live playtest confirmation the same way every other button on this project has required |

---

## Resolved this session (for contrast — see `iw5sp.md` for full write-ups)

- **Sprint (L3):** real `pm_flags` bit (`0x4000`), forced via a Pmove-entry hook plus a
  reassert hook one level deeper. Confirmed working live. Also fixed: sprint no longer
  engages while crouched/prone (auto-stands first, matching console).
- **Reload (X):** real static `kbutton_t` found via memdiff + a new pointer-scan
  feature, wired up via the same `CallKbuttonDown`/`CallKbuttonUp` technique as ADS.
  Confirmed working live.
