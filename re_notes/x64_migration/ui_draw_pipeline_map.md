# Native UI/HUD draw pipeline — x64 architecture map (2026-09-14)

**Purpose, stated directly by the user**: not tied to closing any one specific
gap — a broad, general-purpose trace of the ENTIRE native UI/HUD draw call
chain, so that "once we have that, all UI work becomes much easier." Every
future gameplay-hint, menu-hint, scoreboard, or HUD-element feature this
project builds should start by checking this map before re-deriving any of
it from scratch. Raw Ghidra output backing every claim below lives in
`re_notes/x64_migration/ui_pipeline_trace/` — this file is the organized,
lookup-friendly summary; that folder is the receipts.

**Methodology**: `analyzeHeadless.bat -process iw5sp.exe -readOnly -noanalysis`
against the existing `re_notes/ghidra_project_x64/iw5sp_x64_proj.gpr` (not
re-imported, not modified structurally), using this project's own
`FindCallers.java`/`DecompileAt.java` scripts — same toolkit and policy as
every other x64 RE pass in this file's own directory. Per the locked
2026-09-03 signature-scanning policy (`CLAUDE.md` §5/§10.3): every address
below is a coordinate against THIS specific x64 binary build, for RE
reference only — never hardcode one of these into shipped hook code; resolve
via signature scan at runtime like every other hook target in this project.

**Confidence key** used throughout: **CONFIRMED** = read directly off a real
decompile in this pass. **INFERRED** = a reasoned guess from naming/shape/
call context, not independently proven — flagged so nobody treats it as
settled.

---

## 1. The three-tier architecture (new finding this pass)

Before this pass, this project's own knowledge of the native UI pipeline
started and ended at `FUN_14029a2b0` (the shared low-level text-draw
primitive this project's own `Hook_DrawTextX64` already hooks) and its 22
direct callers, documented narratively in `drawtext_hook_x64.md`. Tracing
those 22 callers' OWN callers this pass revealed the real shape sitting
above them — not one dispatcher, but three separate systems feeding the same
shared leaf:

```
FUN_1401d83a0  (per-frame entry, not traced further this pass)
    |
    v
FUN_1401d7480  (CONFIRMED) -- per-player frame setup: view/damage/killcam
    |             state, a numbered UI-event dispatch (FUN_1401e95b0, event
    |             codes 0x11/0x12 seen -- NOT independently traced further,
    |             a real lead for a future pass), THEN near its own end:
    v
FUN_140039f40  (CONFIRMED) -- the per-frame "HUD element tick": ~25 direct
    |             sub-draw calls in a fixed order (ammo/compass-style
    |             readouts, the FPS/debug overlay, mission-objective feed,
    |             COOP_WAITINGFORPLAYER -- see section 4's correction),
    |             THEN drives the numbered-element dispatcher below for
    |             every currently-ACTIVE HUD element (the actual call site
    |             linking FUN_140039f40 to FUN_140052220 was not isolated to
    |             one exact line this pass -- INFERRED from FUN_140052220's
    |             own single caller not yet being traced; both functions are
    |             real and both are confirmed to reach FUN_14029a2b0, the
    |             exact link between them is the one open thread here).
    |
    v
FUN_140052220  (CONFIRMED) -- "Are we not seeing this" IS this: the master
              numbered-HUD-element dispatcher. A single switch(param_11)
              over ~100 distinct element-type IDs (0x5-0xd0) -- THE central
              map, full table in section 3. Every gameplay hint this
              project has ever substituted (Mantle, Pickup/Swap/
              PickupHealth, Throwback, Hold Breath, Reload) is ONE case each
              in this exact switch -- confirmed, not inferred, this is the
              same function `drawtext_hook_x64.md`'s Stage (c) already
              anchored on via `PLATFORM_MANTLE`, now mapped in full instead
              of just the two cases that investigation needed.
```

**A separate, parallel system, NOT part of the chain above** (its own top
caller was not traced further this pass, but it clearly does not run
through `FUN_140052220`'s numbered-element switch):

```
FUN_1402a7660  (CONFIRMED) -- entity/name-tag compositor: iterates a linked
    |             per-entity list, player-slot-indexed state
    |             (param_3 + 0x50 + entityIndex*4), decides per-entity
    |             whether to draw a plain name tag or hand off to a
    |             specialized path (FUN_1402aa710/FUN_1402ac5d0).
    v
FUN_1402a9520  (CONFIRMED) -- per-entity label text builder: resolves a
    |             live player/bot name or a scripted string reference
    |             (`param_2+0x170`/`+0x150`/`+0xc4`-driven branches, GSC-
    |             string-table-adjacent), calls the subtitle renderer
    |             below AS PART of building its own label, then draws via
    |             either `FUN_14029a2b0` or `FUN_14029a4d0` (a sibling
    |             variant, not traced this pass) depending on a "is this
    |             the locally-controlled/focused entity" check.
    v
FUN_1402a9dd0  (CONFIRMED) -- subtitle renderer. Loads `video/subtitles.csv`
              on first use, resolves per-line text (including a real
              `[{...}]` bracket-token substitution parser, structurally the
              SAME token style Sentry-Place's own marker uses on x86 --
              worth remembering if Sentry-Place's x64 string is ever found),
              and draws via either `FUN_14029a2b0` (plain) or
              `FUN_14029a610` (a richer variant taking icon/portrait
              params -- x86's own `Hook_DrawGlyphText` header comment
              already flagged `FUN_14029a610`'s sibling as confirmed DEAD
              for gameplay hints specifically; here it's clearly alive for
              subtitles/name-tags, a genuinely different call site, not a
              contradiction of that earlier finding).
```

**Practical read for future UI work**: gameplay/menu HINT prompts (button
prompts, mission text, weapon warnings) go through `FUN_140052220`'s numbered
switch — that's the table to check first. Player names, killfeed-style rows,
and subtitles go through the separate `FUN_1402a7660` chain instead — a
different system, don't look for them in the switch table.

---

## 2. The shared bottom of the pipeline (already known, cross-referenced not re-derived)

Every path above funnels into the same two already-documented pieces —
not re-explained here, just anchored so this map is self-contained:

- **`FUN_14029a2b0`** — the universal text-draw primitive, this project's own
  `Hook_DrawTextX64` target. Full discovery trail: `drawtext_hook_x64.md`.
- **`FUN_1401b7c90` → `FUN_14008d020`** — the real position/alignment
  transform, called from INSIDE `FUN_14029a2b0` after this hook's own
  interception point. Full trail: `drawtext_hook_x64.md`'s "CORRECTION,
  2026-09-13" section; the live implementation is
  `ComputeRealDrawPositionX64` (`analog_input_hooks_x64.cpp`).
  **New corroboration this pass**: `FUN_140052220` case `0x60` calls
  `thunk_FUN_14008d020` directly and by name (not just inferred from
  `FUN_14029a2b0`'s own internals) to pre-compute a waypoint marker's real
  screen position before a later draw — independent confirmation this is a
  real, general-purpose transform utility other native UI code calls
  directly, not a private implementation detail of the text-draw path alone.
  **`color1`/`color2` (this project's own hook parameter names) are
  confirmed, again, to be the 11-way alignment-mode enum** `FUN_14008d020`
  dispatches on — `FUN_140052220`'s own call sites pass small literal byte
  values (`0`, `1`, etc.) into these exact argument slots throughout, never
  anything RGBA-shaped, consistent with the existing finding.

---

## 3. `FUN_140052220`'s full case table — the master map

**CONFIRMED** via full decompile (`decomp_140052220.txt`, already on record
before this pass — this section organizes it as a lookup table rather than
raw decompiled C for the first time). `param_11` is the element-type ID.
Cases `0x7`-`0xce` not listed below are real, enumerated IDs with an empty
`break` body in THIS dispatch context — either unused by `iw5sp.exe`
specifically, or handled by a different code path this function doesn't
own; not confirmed further this pass.

| ID | Handler | What it draws (confidence) | This project's status |
|---|---|---|---|
| `0x05` | `FUN_1400506a0` | Gated numeric HUD readout, multi-branch (ammo-count/compass-style — **INFERRED**, exact identity not pinned down) | Not substituted |
| `0x06` | inline | A gated texture/blend call (`FUN_14028c2b0`), no text draw | N/A |
| `0x14` | `FUN_1400514e0` → `FUN_1400519b0` | **CONFIRMED**: the native stance-change hint row(s) — up to 3 simultaneous rows (stand/crouch/prone), each matched against the player's OWN currently-bound key for that action (`+gostand`/`togglecrouch`/`+prone` etc.) via `PLATFORM_STANCEHINT_STAND`/`_CROUCH`/`_PRONE` templates | **Substituted (2026-09-15)** — all three rows show `g_buttonMap.crouchProne`'s real glyph, own dedicated slot each |
| `0x47` | inline | Hold Breath hint (`PLATFORM_HOLD_BREATH`) | **Substituted** (glyph icon) |
| `0x48` | `FUN_14004fa00` | Pickup/Swap/PickupHealth/Throwback family | **Substituted** |
| `0x4f` | `FUN_140050c30` | **CONFIRMED AND NAMED (round 3, 2026-09-14)**: the native low-Health warning icon. `DAT_14053b088` (this case's own gate variable) is confirmed via its WRITER functions (`FUN_140039a90`/`FUN_14005d610`/`FUN_14005db20`, all traced round 3) to be the exact show/hide timer for a named HUD element literally called `"Health"` (passed as a string to a generic named-element show/hide API, `FUN_1402adad0`/`FUN_1402ad500` — see the new section 3.5). `FUN_14004f3a0` (the ratio this case scales by) disassembled raw (decompiler showed a bare `void` with no body, a real -noanalysis limitation): it's `clamp((float)*(param+0x160) / (float)*(param+0x168), 0.0, 1.0)` — a current/max ratio, exactly the shape a health fraction would have | Not substituted |
| `0x50` | inline | Mantle hint (`PLATFORM_MANTLE`) | **Substituted** |
| `0x51`/`0x52` | inline, falls through to `0x53` | A gate/param-massage step before the shared single-hint draw | See `0x53` |
| `0x53` | `FUN_140051850` | **CONFIRMED**: a generic single-line hint draw, gated on the same "controller/gamepad hint visible" flag (`FUN_140051f80`) `0x54`/`0x5a` also check — likely the shared native path several distinct prompts alias through | Not substituted |
| `0x54` | inline | Another `FUN_140051f80`-gated single hint, builds via `FUN_140071790` (string builder, not traced further) | Not substituted |
| `0x5a` | `FUN_14003a590` | **CONFIRMED**: a per-slot (array-indexed by `param_1`), fading numeric readout — shape strongly suggests a HUD element with its own fade-out timer (a notification/counter with a lifetime — **INFERRED** exact identity, e.g. hit marker or pickup-confirm text) | Not substituted |
| `0x5f`/`0x60` | inline | **CONFIRMED**: real-world-to-screen waypoint/marker positioning — `0x60` specifically calls the real position transform (`thunk_FUN_14008d020`) directly, see section 2 | Not substituted |
| `0x61` | inline | Death-quote / `"game message"` caption (calls `FUN_1402afa60`, NOT `FUN_14029a2b0` directly — a different draw variant for this one case) | Not substituted |
| `0x62` | `FUN_140050f80` | **CONFIRMED AND NAMED (round 3)**: same `"Health"` element as `0x4f` above (identical `DAT_14053b088` gate) — a second aspect/phase of the same low-health warning, adding a secondary fade keyed on the health ratio crossing one of two distance-style thresholds | Not substituted |
| `0x63` (99) | `FUN_140057e30` | **CONFIRMED**: `CGAME_MISSIONOBJECTIVES` header label, single line, gated on an active-objective check | Not substituted |
| `0x64` (100) | `FUN_140057f80` | **CONFIRMED** (partial): the objective TEXT body itself (distinct from the header above), resolves via `FUN_140289a60(..., "objective text", 0)` | Not substituted |
| `0x65` | `FUN_140057d40` | **CONFIRMED (round 2)**: icon-only (`FUN_140078c90`/`FUN_14028c4d0`, no text), same "is an objective currently active" gate as `0x63`/`0x66` (mission-objective family) | Not substituted |
| `0x66` | `FUN_1400586f0` | **CONFIRMED (round 2)**: also icon-only, same objective-active gate as `0x63`/`0x65`, computes a screen-edge-relative offset (`FUN_14008d930`/`FUN_14008d2f0`) — likely an off-screen objective-direction arrow, paired with `0x63`'s text label and `0x64`'s body text | Not substituted |
| `0x67`/`0x68` | `FUN_140077940` | **CONFIRMED (round 2, 2026-09-14)**: the ICON draw of the SAME Lethal/Tactical grenade-HUD element identified below — resolves a per-inventory-slot icon handle (`FUN_140052120`/a per-client array walk keyed by `param_6`) and draws it via `FUN_14028c2b0` (a blend/texture call, no text) | Not substituted |
| `0x69`/`0x6a` | `FUN_140077510` | **CONFIRMED, upgraded from round 1's guess**: part of the same Lethal/Tactical grenade-HUD element family (see `0x6b`/`0x6c` below for the confirming evidence) — this specific case draws a NUMBER, not a name: `FUN_1402cb690` is a generic per-frame ring-buffer `sprintf`-style formatter (traced this round, `decomp_batch3.txt`), called here with `&DAT_1403e9b0c` as the format string against a value from `FUN_140078070(&DAT_14052a084, param_7)` — almost certainly the held AMMO COUNT for the currently-equipped grenade type (**INFERRED** specifically "count," the surrounding shape is confirmed) | Not substituted |
| `0x6b`/`0x6c` | `FUN_140077b60` | **CONFIRMED AND IDENTIFIED (round 2, INDEPENDENTLY RE-CONFIRMED round 3) — this is the Lethal/Tactical grenade-type HUD indicator, native name `"offhandinfo"`.** Round 3's unrelated trace of `DAT_14053b098` (this case's own gate, traced for a different reason — it also gates the danger-family investigation) landed on the exact same element via a completely different path (the writer functions' own literal string), independently corroborating round 2's label-table read with zero shared assumptions between the two findings. Dumped the real table this case indexes (`(&PTR_DAT_1404c0bc8)[param_7]`, `dump_ptr_dat_1404c0bc8.txt`/`readstring_lethaltac_0_1.txt`) and got real, unambiguous reference-key strings: index 0 = `""` (no grenade equipped), 1 = `WEAPON_FRAGGRENADE`, 2 = `WEAPON_SMOKEGRENADE`, 3 = `WEAPON_FLASHGRENADE`, 4/5 = unused (null). `param_7` is the currently-equipped Lethal/Tactical grenade-type enum, drawn as its real localized name via the same `FUN_14029f120` resolver every other substituted hint in this project already uses | Not substituted — **the single most concrete new lead this map has produced, see section 5** |
| `0x6d`/`0x6e` | `FUN_140077700` | **CONFIRMED (round 2)**: a fourth aspect of the same grenade-HUD family — same gates (`FUN_140025480`/`FUN_140025490(&DAT_14052a084, param_6)`, `DAT_14053b098`), but keyed on `_DAT_14053ac80` (a "currently active/selected" index check, `param_6 == *(int*)(&DAT_1404e8e60)[idx]+0x68`) and drives a countdown-style icon flash (`FUN_14039ca20`) — likely the "just switched grenade type" or low-ammo flash animation, not text | Not substituted |
| `0x6f` | inline | Conditional icon-only draw (`FUN_140078540`, no text) | N/A |
| `0x70` | inline | A fade-in element gated on `DAT_14052a088` (an apparent GAME-STATE enum — excludes states 2/3/6/7; **INFERRED** meaning, not decoded this pass, but this variable recurs across MANY cases below as the same style of gate and is worth a dedicated future pass to enumerate its real states) | Not substituted |
| `0x71` | `FUN_140050410` | **CONFIRMED**: weapon/stance-BLOCKED warning messages — `WEAPON_NO_AMMO`, `GAME_STAND_BLOCKED`, `GAME_CROUCH_BLOCKED`, `CGAME_PRONE_BLOCKED`/`_WEAPON`, `WEAPON_TARGET_TOO_CLOSE`, `WEAPON_LOCKON_REQUIRED`, `WEAPON_TARGET_NOT_ENOUGH_CLEARANCE` — a real `switch(DAT_140539e68)` selecting WHICH warning is currently active, one shared draw call site. **A previously-undocumented 8th case also confirmed this pass**: a dynamically-built message (`FUN_1402ca430`/`FUN_14029f0d0`), not a fixed localization key, not chased further | **Partially substituted (2026-09-15)** — the 3 stance-blocked messages get a `g_buttonMap.crouchProne` glyph prefix; the 4 weapon/target-related ones deliberately left alone, no clean 1:1 button mapping |
| `0x72` | `FUN_140051250` | **CONFIRMED AND NAMED (round 3) — the native Sprint Meter.** `DAT_14053b094`, this case's own gate, is confirmed via the same writer-function trail to be the exact show/hide timer for the named element `"sprintMeter"`. **Round 2's guess ("ammo warning, evidenced by an `"iw5_"` weapon-name check") is corrected here, not deleted** — the `"iw5_"` prefix check is real and still happens in this exact function, but it selects which per-weapon animation-speed table to scale the meter's own fill-rate against, not the element's identity; the element itself is the sprint meter, confirmed by name, not weapon-related at all. Directly relevant to this project's own extensive native Sprint work (both architectures) | Not substituted |
| `0x73` | inline | Another fade-gated icon draw (`FUN_140051e10`) | Not substituted |
| `0x74`-`0x79` | `FUN_140031910`/`1400319e0`/`1400316f0`/`140031bc0`/`140031540` | `0x78` = **CONFIRMED Reload** (already substituted, per `drawtext_hook_x64.md` Stage (c)). `0x77` (`FUN_1400316f0`) **CONFIRMED**: a gated readout structurally identical in shape to `0x05`/`0x54` (same `DAT_14052a130 & 0x200000` gate, same `sprintf`-into-buffer-then-draw pattern) — **INFERRED** ammo/compass-style, not pinned down. `0x74`/`0x76`/`0x79` **CONFIRMED (round 2), all icon-only, no text**: `0x74` a plain gated icon blend; `0x76` the same real transform pair (`thunk_FUN_14008d020`) section 2 already covers, with a screen-edge clamp; `0x79` gates on a real per-client "has an active challenge/unlock notification" check (`FUN_140018980`/`FUN_140030540`) and draws via a `(&DAT_14052a2f6)[slot*0xc]`-indexed table — likely the in-game challenge/unlock popup icon (**INFERRED**) | `0x78` substituted; rest not |
| `0x91`/`0x92` | `FUN_140036720` | **CONFIRMED**: objective distance/direction markers — `CGAME_OBJECTIVE_ABOVE`/`_BELOW` plus a live `"%.1fm"`-formatted distance | Not substituted |
| `0x96`-`0x99`, `0x9b`/`0xbe`, `0x9f`-`0xa3`, `0xa4`, `0xa5`, `0xa6`, `0xaa`-`0xae` | a cluster of ~14 similarly-shaped handlers (`FUN_140034250`/`140034570`/`140035e30`/`140037e40`/`1402ddc60`/`140034ce0`/`140035a80`/`140034980`/`140035740`/`1400346a0`/`140033dc0`/`1400384a0`/`140036 5e0`/`140031160`/`140030a70`) | **CONFIRMED AND NAMED (round 3) — this entire cluster is the native Compass.** Every member of this cluster (confirmed via `DescribeRefs.java` on `DAT_14053b084`, the exact same named-element gate section 3.5 traced to the literal string `"Compass"`) reads that one global — not a guess, every real reader of the Compass show/hide timer IS this cluster, member-for-member. Shape: most take a `(param_1, 0 or 1, param_2, ...)` signature — the SAME handler function often reused for TWO adjacent case values differing only in that leading 0/1 flag (e.g. `0x97`→`FUN_140034570(...,0,...)` and `0xb4`→`FUN_140034570(...,1,...)`) — a real "two variants of the same marker kind" pattern (**INFERRED** which — friendly/enemy blip, primary/secondary marker, etc. are the leading guesses). `0xa4` (`FUN_140033dc0`) is directly confirmed a directional marker ("Above"/"Nearby"/"Below" text) | Not substituted — real controller-UI repositioning/re-skinning work on the compass now has a confirmed, complete case list to start from |
| `0xb4`-`0xbd` | (see cluster above) | Same cluster, the "flag=1" variants | Not substituted |
| `0xc9`/`0xca`/`200` | `FUN_1400678e0`/`1400668b0`/`140067870` | Not traced this pass | Unknown |
| `0xcf`/`0xd0` | `FUN_1400674d0`/`140067610` | **CONFIRMED identity from cross-reference, not re-traced this pass**: this project's own existing header-comment research (`analog_input_hooks_x64.cpp`'s Reload-investigation trail) already identified these two exact addresses as "vehicle boost/throttle/brake/fire" — a real, independently-confirmed match to this table's own `param_11` values, not a coincidence. Directly relevant to DPV/Goalpost mortar/Goalpost M2 turret UI (2026-09-14's own killstreak-fix session) | Not substituted — **candidate for a future vehicle-HUD glyph pass** |

**A structural observation worth carrying forward**: the `0x9b`-`0xbe`-style
cluster's repeated "same function, leading flag 0 vs. 1" pattern, and the
`0x69`-`0x6c` table-indexed-lookup pattern, are both real, reusable SHAPES —
recognizing either shape quickly in a future decompile (without redoing
this whole trace) is itself part of what this map is for.

---

## 3.5. A native named-HUD-element show/hide registry (new finding, round 3)

Tracing `DAT_14053b088` (`0x4f`/`0x62`'s own gate variable) to its WRITERS
rather than just its readers — `FUN_140039a90`, `FUN_14005d610`,
`FUN_14005db20`, all newly decompiled this round — found something bigger
than the one variable it was chasing: a small, generic API
(`FUN_1402adad0` = show, `FUN_1402ad500` = hide, `FUN_1402acbf0` = a third
variant, name-string first argument each time) that every one of these
"fade timer" globals is paired with. Each global is a per-element timestamp
gating exactly one named string:

| Global | Native element name | Case(s) in section 3 |
|---|---|---|
| `DAT_14053b088` | `"Health"` | `0x4f`, `0x62` |
| `DAT_14053b08c` | `"weaponinfo"` / `"weaponinfo_lowdef"` | **Not part of `FUN_140052220`'s switch at all** — its one real consumer, `FUN_140051e90`, is called from `FUN_14028f3c0`, a completely separate dispatch chain never traced further this round. A genuine architecture refinement: not every named element goes through the numbered dispatcher this whole map has centered on |
| `DAT_14053b084` | `"Compass"` | `0x91`/`0x92`, `0x96`-`0x99`, `0x9b`/`0xbe`, `0x9f`-`0xa6`, `0xaa`-`0xae`, `0xb4`-`0xbd` — the ENTIRE compass/entity-marker cluster from section 3, confirmed member-for-member via `DescribeRefs.java` (every real reader of this global is a cluster member, no exceptions found) |
| `DAT_14053b090` | `"stance"` | Cross-confirms the `0x14` stance-hint family (section 3) shares this same show/hide system, though `0x14`'s own draw reads different globals directly |
| `DAT_14053b094` | `"sprintMeter"` | `0x72` |
| `DAT_14053b098` | `"offhandinfo"` | `0x67`-`0x6e` (independently re-confirms round 2's Lethal/Tactical grenade-HUD finding — see that row's own note) |
| `DAT_140539e34` | `"objectiveinfo"` | `0x63`-`0x66` (mission-objective family) |
| (inline literal, `FUN_140039a90`) | `"challenge"`, `"voiceMenu"` | Not traced to a case this round |

**What this means for future UI work**: this named-element registry is
almost certainly a real, general-purpose HUD element index (very possibly
the same one MW3's own GSC layer exposes via `hidepart`/`hud_*`-style
script functions, though that wasn't independently confirmed this round) —
a plain string name is enough to show/hide any of these elements. Two real
leads this surfaces for later: `"weaponinfo"`/`"Compass"` now have exact
identities but no case number yet mapped in section 3's table (a future
round should search `FUN_140052220` for whichever case reads
`DAT_14053b08c`/`DAT_14053b084` to close that gap); and this API
(`FUN_1402adad0`/`FUN_1402ad500`) is itself a plausible, low-risk hook
point for any future feature that needs to show/hide a whole native HUD
element by name rather than intercepting its individual draw calls.

---

## 4. Correction: `COOP_WAITINGFORPLAYER` is NOT the Survival ready-up trigger

Recorded per this project's own "correct visibly, don't delete" convention.
While tracing `FUN_140039f40`'s body (see section 1), one of its direct
sub-draws resolves and draws `"COOP_WAITINGFORPLAYER"` gated on
`FUN_140266a20(param_1) != 0.0`. Investigating this specific gate was
initially considered a possible lead for the real native Survival ready-up
trigger — a mechanism this project has never found on EITHER architecture
despite extensive prior hunting (see `CLAUDE.md`'s "Survival ready-up (hold
Y)" section, and this file's own 2026-09-14 ready-up port, which uses a
substitute detection signal specifically because the real native trigger
remains unknown). **User correction, direct**: this is "the coop waiting for
other player text that appears," a real but SEPARATE native HUD element —
not the ready-up trigger, and was not independently investigated further as
a ready-up lead. `FUN_140266a20` itself was not traced this pass. Flagged
here so a future session doesn't waste time re-chasing this specific
function under the ready-up hypothesis without new evidence.

---

## 5. Concrete new opportunities this map reveals

Not implemented this pass — this is a map, not a feature branch. Ranked by
how directly actionable each one is:

1. **Lethal/Tactical grenade-type indicator (`0x67`-`0x6e`, confirmed round
   2) — now the single most concrete lead in this whole map.** A real,
   decompile-confirmed HUD element: an icon (`0x67`/`0x68`), an ammo count
   (`0x69`/`0x6a`), the grenade's real localized name — `WEAPON_FRAGGRENADE`/
   `_SMOKEGRENADE`/`_FLASHGRENADE`, table dumped and read directly, not
   guessed (`0x6b`/`0x6c`) — and a switch/low-ammo flash (`0x6d`/`0x6e`).
   Unlike every other lead in this section, this one has zero remaining
   identity risk — the next step is purely implementation: does controller
   Lethal/Tactical SWITCHING already work on x64 (check the existing parity
   tracker), and if so, does this element's name text deserve the same
   substitution treatment Mantle/Pickup already get.
2. **Weapon/stance-blocked warnings (`0x71`, `FUN_140050410`)** — the exact
   same structural shape (`ShouldDrawGlyphOverlay_Exported() && !IsMenuActiveX64_Exported()`
   gate, a live-resolved template, one shared draw call) as the already-
   substituted Mantle/Pickup/Hold-Breath/Reload cases. `WEAPON_LOCKON_REQUIRED`/
   `WEAPON_TARGET_TOO_CLOSE`/the stance-blocked messages are all plain
   `FUN_14029f120(pcVar10)` resolves with no substituted key-name marker at
   all (no `"&&1"` span to find) — meaning these are candidates for a
   controller-specific icon PREFIX (e.g. an X/A-button glyph before the
   message) rather than the substitution-in-place technique the marker-based
   hints use. Lowest RE risk of anything in this list — the detection
   template strings are already known verbatim from this pass.
3. **Stance-change hint (`0x14`/`FUN_1400519b0`)** — real native rows for
   "you can currently stand/crouch/prone," each independently matched
   against the player's own live keybind. A genuine gap: this project has
   CrouchProne's own INPUT working on x64 (per the 2026-09-05 parity work)
   but never wired controller-glyph substitution for this specific native
   PROMPT. Real key-name templates (`PLATFORM_STANCEHINT_STAND`/`_CROUCH`/
   `_PRONE`) already confirmed this pass — same "&&1"-style substitution
   shape as Mantle, should port with the same technique.
4. **Objective distance/direction markers (`0x91`/`0x92`, `0xa4`, and the
   `0x96`-`0xbd` marker cluster)** — real compass/waypoint UI, structurally
   confirmed to already use the real position-transform utility
   (`thunk_FUN_14008d020`, section 2) directly. Relevant to any future work
   on repositioning/re-skinning on-screen markers for controller play.
5. **Vehicle HUD (`0xcf`/`0xd0`)** — cross-referenced, not re-traced, boost/
   throttle/brake/fire gauges. Directly relevant to the DPV/Goalpost mortar/
   Goalpost M2 turret work already shipped this same day
   (`Hook_MountedAimTick`) — a natural next step if that feature ever needs
   its own HUD glyph treatment.
6. **Health (`0x4f`/`0x62`) and Sprint Meter (`0x72`) glyph/positioning
   work** — both now have confirmed native identities (round 3) and were
   already flagged in this list under the wrong, vaguer "danger indicator"
   framing; re-flagging here under their real names. Sprint Meter
   specifically is a strong candidate given this project's own extensive
   native Sprint investment on both architectures.
7. **`"weaponinfo"`'s own separate dispatch chain (`FUN_14028f3c0` →
   `FUN_140051e90`)** — round 3 confirmed this element does NOT go through
   `FUN_140052220` at all, a genuinely different code path from everything
   else in this map. Not traced further this round; worth a dedicated look
   if a future feature needs to touch the native weapon-info HUD readout.

None of the above should be started without first checking whether the
underlying INPUT already works (per the existing parity audit/known-issues
trackers) — this map is about the DRAW side only; several of these element
types may already have zero native input path on controller at all, which
would need its own investigation before a glyph substitution is even
meaningful.

---

## 6. Raw output index

All backing decompiles/caller-lists for this pass live in
`re_notes/x64_migration/ui_pipeline_trace/`:

- `decomp_batch1.txt` — full decompiles: `FUN_1400316f0`, `FUN_1402abec0`,
  `FUN_14003a590`, `FUN_1400506a0`, `FUN_140051850`, `FUN_140077510`,
  `FUN_140077b60`, `FUN_1402a6300`, `FUN_1402a9520`, `FUN_1402a9dd0`,
  `FUN_140050410`, `FUN_1400519b0`, `FUN_140057e30`, `FUN_140057f80`,
  `FUN_140266a20`.
- `decomp_batch2.txt` — `FUN_1401d7480`, `FUN_1402a7660`, `FUN_1400514e0`.
- `callers_14029a2b0_full22.txt` — the complete 22-caller decompile dump for
  the shared draw primitive (previously only excerpted in
  `drawtext_hook_x64.md`).
- `callers_140039f40.txt`, `callers_140050410.txt`, `callers_1400519b0.txt`,
  `callers_140077510.txt`, `callers_1401d7480.txt`, `callers_1402a9520.txt`,
  `callers_1402abec0.txt`, `callers_1402b1090.txt` — single-target
  `FindCallers.java` runs backing the "single caller" claims throughout
  section 1.
- `back_shortcut.txt`, `friends_shortcut.txt` — pre-existing output from an
  earlier, separate menu-corner-hint investigation, folded in here since it
  covers the same general UI-pipeline territory (not re-verified or
  re-described as part of THIS pass — see `drawtext_hook_x64.md`'s own menu
  corner-hint section for that trail).

**Round 2 (same day, "keep digging"), the Lethal/Tactical grenade-HUD find**:

- `decomp_batch3.txt` — full decompiles: `FUN_1402cb690`, `FUN_140050c30`,
  `FUN_140050f80`, `FUN_140057d40`, `FUN_1400586f0`, `FUN_140077940`,
  `FUN_140077700`, `FUN_140051250`, `FUN_140031910`, `FUN_1400319e0`,
  `FUN_140031540`, `FUN_1400678e0`, `FUN_1400668b0`, `FUN_140067870`,
  `FUN_1400514e0` — filling in every remaining "not traced this pass" cell
  from round 1's table.
- `dump_ptr_dat_1404c0bc8.txt` — raw qword dump of the `0x6b`/`0x6c` label
  table (`DumpRawQwords.java`), the direct evidence for the grenade-HUD
  identification.
- `readstring_lethaltac_0_1.txt` — the two table entries `DumpRawQwords`
  didn't auto-resolve as strings, read directly (`ReadStringAt.java`):
  confirms index 0 is the empty string (no grenade equipped) and index 1 is
  `WEAPON_FRAGGRENADE`.
- `describerefs_1404c0bc8.txt` — cross-reference dump
  (`DescribeRefs.java`) confirming which functions read `DAT_14053b098`/
  `DAT_14052a088` (the two gate variables recurring across most of this
  table), used to sanity-check the "shared family" groupings in section 3.

**Reusable technique from this round, worth naming for next time**: when a
decompile shows `(&PTR_DAT_<addr>)[indexVar]` feeding a string-resolver
call, don't stop at "table-indexed lookup, identity unknown" — dump the
actual table contents (`DumpRawQwords.java` for a quick look,
`ReadStringAt.java` for any entry it doesn't auto-resolve) before writing
the finding down as INFERRED. This is what turned round 1's vaguest, most
speculative row into round 2's most concrete one, for the cost of two small
script runs.

**Round 3 (same day, "keep digging" again), the named-element registry
find**: `decomp_danger_family.txt` (`FUN_140051e10`, `FUN_14004f3a0`,
`FUN_140052110`, `FUN_140025480`, `FUN_140025490`); `disasm_14004f3a0.txt`
(raw disassembly, needed because `-noanalysis` decompiled this one as an
empty `void` despite it clearly computing and returning a float —
`clamp(a/b, 0, 1)` is only visible in the raw XMM instructions);
`decomp_writers_danger.txt` (`FUN_140039a90`, `FUN_14005d610`,
`FUN_14005db20` — the actual discovery, a generic named-element show/hide
API used by seven-plus native HUD elements); `describerefs_danger_and_state.txt`
and `describerefs_weaponinfo_compass.txt` (the two `DescribeRefs.java` runs
that closed the Compass-cluster mapping and found `"weaponinfo"`'s separate
dispatch chain); `callers_140051e90.txt` (confirming that separate chain's
real caller, `FUN_14028f3c0`).

**Second reusable technique from this round**: when `-noanalysis`
decompiles a function as an empty/no-op `void` despite a caller clearly
using its return value, don't take the decompile at face value — raw
disassemble it (`DumpDisasm.java`). This is a real, repeatable `-noanalysis`
limitation (float returns via XMM0 without a parameter-ID pass), not a sign
the function does nothing. And more generally: when chasing one gate
variable's identity, decompile its WRITERS, not just its readers — a
writer function tends to name what it's writing (`FUN_1402adad0(...,
"Health")` is unambiguous in a way that reading `DAT_14053b088` compared
against a timestamp never would be on its own).

See `re_notes/known_issues_x64.md` issue #1 for this map's place in the
project's own live-status tracking.
