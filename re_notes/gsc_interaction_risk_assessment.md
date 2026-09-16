# GSC-VM interaction risk assessment (2026-09-16)

Direct request, following the ready-up/buy-station investigation
repeatedly hitting the same wall this session (the Survival stats-recap
screen appears to be GSC-driven, matching this project's own established
prior finding that ready-up's text is "Survival-script-driven, not in
`code_post_gfx.str` at all"): "how risky is modifying GSC (it needs its
own risk assessment) — yes we know mods target it but also like almost
all in-game stuff does too so how do we stay indistinguishable."

This is a real, dedicated question deserving its own evidence base, same
standard as `security/re_notes/vulnerability_research.md`'s own web-research
pass before any implementation — no code written, no binary RE done here,
this is the evidence gathering step. Web research only; not independently
verified against this project's own binary. Treat accordingly.

**Scope note, important**: this assessment covers GSC-VM *interaction* in
general — reading live script-local state (the exact thing already closed
for the main mod, `CLAUDE.md`'s Plugin API section, issue #89) AND actually
calling/triggering existing script functions AND injecting genuinely new
script behavior. These are three different action classes with different
risk profiles, deliberately not collapsed into one verdict below.

---

## 1. The real anti-cheat context for THIS specific game

Already established, standing project research (`CLAUDE.md`, `security/`'s
own vulnerability_research.md): **VAC is confirmed active on `iw5mp.exe`**
(MW3's own Steam store page historically listed "Valve Anti-Cheat enabled"),
signature-based, not heuristic. New this pass, confirmed via general VAC
architecture research (not MW3-specific, but VAC's mechanism is the same
across titles): **VAC is a usermode anti-cheat with no kernel driver, and
its detection is signature scanning of running-process memory against a
database of known-cheat byte patterns — not a generic behavioral/heuristic
system.** It does not (cannot, by this architecture) flag "GSC execution
happened" as a category — GSC execution is the ENGINE'S OWN NORMAL
operation, running constantly in every session, real or modded (GSC is
literally how nearly all of MW3's gameplay logic — Campaign scripting,
Survival wave logic, killstreaks, everything — is implemented; a session
with zero GSC execution would mean the game isn't actually running).

**Modern Call of Duty titles (Black Ops 6/7, Warzone) use RICOCHET, a
completely different, kernel-level anti-cheat system — confirmed NOT
applicable to MW3 (2011), which predates it by over a decade and has never
used it.** Do not let RICOCHET-era community discussion of anti-cheat
bleed into reasoning about this specific, much older title — MW3's real
and only ban-relevant system is VAC as described above.

**Practical consequence of "signature-based, not behavioral"**: the real
risk from ANY technique (memory read, memory write, GSC-VM interaction)
is not "does this touch X category of data," it's "does this resemble a
byte pattern VAC's database already has a signature for." A widely-known,
publicly-distributed tool used by thousands of people is far likelier to
have an existing signature than genuinely custom, narrowly-scoped code
this project writes from scratch and never distributes as a standalone
injector — the same reasoning this project's own standing input-remapping
risk assessment already rests on (`CLAUDE.md`'s VAC research section).

## 2. Real-world precedent: GSC modification is the PRIMARY, decade-plus-old modding method for this exact game

Confirmed via direct web research (not assumed): **GSC injection/mod menus
are the dominant, long-established modding method for MW3 (2011)
specifically** — real, public, actively-maintained projects exist
(`mcabcon/IW5-EnCoReV16`, a GSC mod menu for MW3 multiplayer; multiple
GSC-loader/injector tool threads on CabConModding and Se7enSins spanning
years). This is not a fringe or theoretical technique for this title — it
is THE established community modding method, comparable in maturity/scale
to this project's own controller-input-remapping approach being the
established method for that separate problem.

**The community's own real, observed risk distinction — genuinely useful,
matches this project's existing policy shape closely**:
- **"Official"/legitimate script-loading methods** (through a private
  server's own sanctioned script-loading path, e.g. Plutonium's own
  scripts folder, or LAN-mode use) — described in community sources as
  allowed/safe.
- **Injectors used against real, VAC-secured official matchmaking** —
  described with real, direct warnings ("you WILL 100% get VAC banned if
  you use it... never inject while not in LAN mode or else you'll get
  banned"). This is a genuine, repeated community warning, not a single
  anecdote.

This maps closely onto a distinction this project already treats as
load-bearing elsewhere: WHERE and HOW something runs matters as much as
WHAT it does. A widely-distributed public injector, run against official
VAC-monitored matchmaking, by definition satisfies both "is a known
signature" (public, widely used, plausibly already fingerprinted) and "is
detectable in the exact context VAC actively watches" (official
matchmaking). This project's own actual target context for the specific
investigation that raised this question — Survival/Campaign/Spec-Ops, not
competitive MP — is a real, different context worth its own explicit
consideration (see section 4).

## 3. Not all GSC interaction is the same action — three real technique classes, three different risk profiles

**Class A — read-only inspection of existing GSC-VM state.** Reading a
variable a script already set (e.g. `self._id_18D3[...]`, the exact thing
issue #89 needed for the scoreboard feature). No new code introduced, no
new behavior, the running game is byte-for-byte unaffected by the read
itself. This is the SAME risk category this project's own standing policy
already reasons about for gameplay-entity memory reads generally (closed
for the main mod, reserved for opt-in plugins only, per the aim-assist
removal precedent and issue #89 itself) — nothing about GSC specifically
changes that existing reasoning; this assessment does not propose
reopening it.

**Class B — triggering EXISTING script functions the game itself already
shipped, through the game's own real native APIs.** Confirmed via web
research: a genuine, established technique exists for this — hook the
real native `Scr_LoadScript` (fires when the game loads a script it
already ships), use `Scr_GetFunctionHandle` (also a real native function)
to get a handle to a function ALREADY PRESENT in that already-loaded
script, then call it via `Scr_ExecThread` (also real/native). **No new
bytecode is ever written or introduced anywhere** — this only reads and
invokes content the game's own developers already authored and shipped,
through the exact same native entry points the engine's own script system
uses for its own normal operation. This is structurally the SAME
"hook/call real native functions, synthesize nothing new, never write
foreign memory content" philosophy this project's own architecture
already lives by for every other confirmed-safe technique it uses
(synthetic WM_KEYDOWN events indistinguishable from real keypresses,
calling the real `g_pauseToggle`/`g_openPauseMenuForCinematic` instead of
hand-rolling pause logic, etc.) — a real, meaningful parallel, not a
stretch.

**Class C — injecting genuinely new, custom GSC bytecode/behavior the game
never shipped.** This is what public "GSC mod menus" actually do (loading
entirely new menu systems, new gameplay behaviors) — allocates and writes
real new memory content with a recognizable shape, and is almost
certainly what the community's own "100% VAC banned" warnings are
actually about. This is the highest-risk class by a wide margin, closest
in shape to what VAC's signature scanning is specifically built to catch,
and is NOT what this investigation's own actual need (reading/triggering
existing Survival wave-transition logic) would require.

**The practical implication for "how do we stay indistinguishable"**:
the real answer per this research is not "avoid GSC" (impossible and
unnecessary — GSC execution is the baseline, ever-present, unavoidable
activity of any running session) but "stay in Class A/B, never drift into
Class C." Class B specifically — hooking real `Scr_LoadScript`/
`Scr_GetFunctionHandle`/`Scr_ExecThread` to trigger EXISTING content —
appears to be the closest available technique to this project's own
already-proven-safe "real native function calls only" standard, and would
likely be the right target if this project ever decides to actually
TRIGGER (not just read) GSC-side behavior for a future feature.

## 4. Context this project's specific use case has that generic community warnings don't cover

Every "100% VAC banned" warning found in this research is specifically
about **injectors used against live, official, VAC-secured multiplayer
matchmaking**. This project's own actual target for the investigation that
raised this question is **Survival/Campaign/Spec-Ops** — not competitive
MP. Whether VAC's live monitoring is even meaningfully active during
offline/co-op Survival sessions (as opposed to `iw5mp.exe`'s own
confirmed-active VAC) is a genuinely open, NOT yet independently checked
question this project has never specifically investigated — flagged here
as a real, distinct factor that could materially change the practical risk
picture for this exact use case, separate from (and not resolving) the
existing standing MP VAC research. Worth a dedicated check before any
implementation, not assumed either way.

## 5. Summary verdict (research-only, no implementation decision made here)

- GSC execution itself is not a detectable signal — it's the baseline
  operation of any running session. The real question is always about the
  SPECIFIC technique and content, not the general category "touches GSC."
- Class A (read-only inspection) carries the same risk profile this
  project's standing policy already reasons about for live gameplay-memory
  reads generally — already closed for the main mod, already the
  plugin-only line, unchanged by this research.
- Class B (hook real native script-loading/execution APIs to trigger
  EXISTING, already-shipped script content) is the most promising,
  lowest-apparent-risk technique for anything beyond pure reading — no new
  bytecode, real native calls only, the same philosophy this project
  already trusts elsewhere.
- Class C (inject genuinely new script behavior) is real, established,
  and carries real, repeatedly-documented community-observed ban risk,
  concentrated specifically around public-tool use against live official
  matchmaking — not the shape of anything this investigation's own actual
  need would require.
- This project's own specific context (Survival/SP, not competitive MP)
  has a real, unresolved open question (is VAC's live monitoring even
  meaningfully active there) that could matter a lot and hasn't been
  checked yet.

**This is research only — no code written, no policy change made, no
implementation decided.** If this project wants to actually act on
Class B for a specific feature, that's a fresh, explicit decision to make
deliberately, the same way the 2026-08-21 MP opt-in authorization and the
aim-assist removal were both real, explicit, direct decisions rather than
inferred from research alone.

---

## Sources (web research, 2026-09-16)

- [mcabcon/IW5-EnCoReV16](https://github.com/mcabcon/IW5-EnCoReV16) — real, maintained GSC mod menu for MW3 (IW5) multiplayer.
- [CabConModding — mw3-gsc tag](https://cabconmodding.com/tags/mw3-gsc/) and [EnCoReV16 thread](https://cabconmodding.com/threads/modern-warfare-3-2011-encorev16-gsc-mod-menu-by-cabcon.10265/) — community context, real/allowed vs. injector/risky distinction.
- [MPGH thread, "[Detected] MW3 Mod menu"](https://www.mpgh.net/forum/showthread.php?t=1013623) — direct "100% VAC banned" community warning for detected injector use.
- [Se7enSins — "CoD MW3 SP custom script loading"](https://www.se7ensins.com/forums/threads/cod-mw3-sp-custom-script-loading.1872197/) — SP-specific GSC loading discussion.
- [Lunar Journal — "A simple GSC loader for COD Black Ops 1"](https://journal.lunar.sh/2023/gsctool.html) — the `Scr_LoadScript`/`Scr_GetFunctionHandle`/`Scr_ExecThread` technique description (a different, later IW-lineage title, cited for the technique's real existence and shape, not assumed identical addresses/ABI on MW3).
- [Valve Anti-Cheat — Wikipedia](https://en.wikipedia.org/wiki/Valve_Anti-Cheat) and [GuidedHacking — "The Truth About VAC Detection"](https://www.mpgh.net/forum/showthread.php?t=910257) — VAC's real signature-scanning, non-heuristic, usermode-only architecture.
- [Call of Duty — RICOCHET Anti-Cheat](https://www.callofduty.com/ricochet) — confirms RICOCHET is a separate, modern, kernel-level system for current titles, not applicable to MW3 (2011).
