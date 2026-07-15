# Known Issues — `iw5sp.exe` (Campaign/Survival)

Tracked as tasks in the working session; this file is the standalone reference so they
don't stay buried in `iw5sp.md`'s investigation log. Update status here as each is
resolved. Last updated 2026-07-15.

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

## 2. No real one-shot command dispatcher found yet

**Status:** Likely RESOLVED (2026-07-15), built and deployed, awaiting live verification.

**Found via:** external research, per explicit user direction to use it instead of
continuing blind RE. A community write-up on classic CoD-engine reverse engineering
(kiwidog.me) describes the standard technique: search the binary for a hardcoded
`"screenshot"` command string, since that dev command is almost always issued through
the real `Cbuf_AddText`-equivalent somewhere. We already had this exact anchor sitting
in earlier session data without recognizing it — `FUN_004dfd30` (decompiled hours
earlier while chasing callers of the special-bind dispatcher `FUN_00438710`) contains
`FUN_00457c90(*param_1, "screenshot\n")`.

**`FUN_00457c90` confirmed as `Cbuf_AddText(int clientIndex, const char* text)`:**
lock-protected (`FUN_00428af0`/`FUN_00528fe0` acquire/release critical section `0x1f`),
per-client text-buffer append. Special-cases a `"p0 "` prefix to redirect the target
client index (irrelevant for SP — always client 0). Computes the string length, then
indexes a per-client 12-byte bookkeeping struct (`base`/`capacity`/`writeOffset` at
`&DAT_017507e4/e8/ec + clientIndex*0xc`), and if there's room, appends the string
(including its null terminator) at the current write offset, advancing the offset by
the string length only (not length+1) — so the buffer is a flat concatenated command
stream and the *caller* must supply a trailing `\n` (confirmed by `"screenshot\n"`
including one; nothing here adds it automatically). This is exactly the textbook
Quake3-lineage `Cbuf_AddText` behavior.

**Calling convention:** confirmed via the real call site in `FUN_004dfd30`
(`PUSH 0x827e64` [the string] ; `PUSH EAX` [client index] ; `CALL 0x00457c90` ;
`ADD ESP,0x8`) — plain `__cdecl`, arguments in normal declared order. No register-passed
weirdness at all, unlike almost every other hook in this project — callable directly as
an ordinary C function pointer, no naked-ASM hook stub needed.

**Implementation:** `analog_input_hooks.cpp` declares
`using CbufAddTextFn = void(__cdecl*)(int, const char*);` bound to `0x00457c90`, and
calls it directly (not hooked — we're just *invoking* the real engine function) from
two new edge-triggered injectors:
- `InjectControllerWeaponNext()` (Y button) → `CbufAddText(0, "weapnext\n")`
- `InjectControllerPauseMenu()` (Start button) → `CbufAddText(0, "togglemenu\n")`

Both fire once per rising edge only (not every frame held) — since this appends into a
growing buffer rather than setting a hold-state, holding the button down would spam the
command into the buffer every single frame.

**Not yet done:** live verification (does Y actually cycle weapons in-game, does Start
actually open/close the pause menu the same way real ESC does, and does it interact
safely with the buy-station gate-window fix from issue #1). Once confirmed, this also
unblocks a "real" native `toggleprone` via the same mechanism if ever wanted (current
prone/crouch ladder already works fine via direct usercmd-bit forcing, so no urgency
there) and gives us a general-purpose way to invoke any one-shot console command
natively for future buttons.

**Dead ends ruled out along the way (superseded by the above, kept for the record):**

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
- `FUN_00478ad0` (`"Reliable command buffer overflow"` string): looked exactly like a
  classic `Cbuf_AddText`, but turned out to be `SV_AddServerCommand`-equivalent — a
  server→client reliable-command *queue* (per-client ring buffer, `MAX_RELIABLE_
  COMMANDS`-style masking). Sends things like print/scoreboard messages from server to
  client; not the local client-side one-shot input dispatcher we need (weapon
  switching needs no server round-trip).
- **memdiff on `togglemenu` (ESC), false lead:** edge-sequence mode narrowed to 218
  candidates, all showing the identical `0xFF`/`0x00` alternating pattern, densely
  packed across a single ~56KB region. Pointer-scanning a diverse sample (including
  two addresses that looked "isolated" from the repeating cluster) found all 10 sampled
  addresses point to the *same* 2MB memory block via the *same* 12 static references.
  Dumping that block's contents (new `memdiff dump` mode) revealed it's **Steam API's
  own internal protobuf message data** (`CMsgNetworkDevicesData`,
  `CCloud_PendingRemoteOperation`, `CMsgFactoryResetState`, `CSteamOSManagerState`,
  etc.) — completely unrelated to the game's menu system. This was a false
  correlation: Steam's background overlay/networking activity happened to change on a
  cadence that coincidentally lined up with real human press-and-pause timing, not
  because the pause menu toggled. Worth remembering as a general risk for this
  methodology — background OS/Steam processes can produce consistent-looking but
  spurious correlates.

**Next step (not yet tried):** find where a *typed console command* gets parsed and
executed (the developer console input path) — that code must call the real generic
dispatcher directly with a raw string, which the key-event path may obscure. If
retrying memdiff on a one-shot command, consider excluding known Steam/non-game module
memory ranges from the scan to reduce false correlates like the one above.

---

## 3. Remaining unassigned controller inputs

**Status:** Open, tracked as tasks #3–#6 in this session.

| Input | Intended action | Blocker |
|---|---|---|
| Y | `weapnext` | Wired via issue #2's fix (2026-07-15) — awaiting live confirmation |
| Start | `pause`/`togglemenu` | Wired via issue #2's fix (2026-07-15) — awaiting live confirmation |
| Back | Likely `+scores` (scoreboard) | Not yet investigated — this is a hold (`+`/`-` pair) command, not a one-shot, so it needs its own real `kbutton_t` found the same way ADS/Reload were, not the `Cbuf_AddText` mechanism above |
| D-pad (all 4) | `+actionslot 1-4` variants | Bits are known from the confirmed usercmd-bit table but individually unconfirmed against real gameplay effects — old table-order-guessed identities (inventory wheel, weapon attachment, NVG toggle) are unreliable and need live playtest confirmation the same way every other button on this project has required |

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
