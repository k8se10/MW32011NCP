# Patch Notes

All notable changes to this project, per release. See `re_notes/` for the full
vulnerability-research and reverse-engineering trail behind each entry, and
`CLAUDE.md` (game install root) for the complete technical plan.

---

## Unreleased

### What's New
- **Three distribution modes for the netcode security fixes, all built from
  the same fix-module source.** Standalone (`proxy_d3d9/`, this project's
  own injected DLL), a plugin for the sibling
  [MW32011NCP](https://github.com/k8se10/MW32011NCP) project
  (`tools/ncp_plugin_netcode_fixes/`, builds to `mw32011nsp_security.dll`),
  and — via that same plugin build — a "greenlit" default-on capability in
  NCP itself: NCP's own plugin loader now auto-loads this exact filename
  without requiring its normal third-party-plugin opt-in
  (`[Plugins] Enabled=1`), a real change made on NCP's own side to its
  `plugin_loader.cpp`. See NCP's `PLUGIN_API.md` for the full design and its
  real caveat (filename matching isn't cryptographic). Build-verified: x64
  `/t:Rebuild` (0 errors), `dumpbin` confirms `8664 machine (x64)` and both
  required exports (`MW3NCP_PluginInit`/`MW3NCP_PluginShutdown`) present.
  Not yet live-tested through NCP itself.

### Fixed
- **First real fixes shipped: three of the four confirmed, unpatched netcode
  vulnerabilities are now patched via a standalone proxy DLL (`proxy_d3d9/`,
  NSP's first-ever implementation code).** This project's Phase 1 stated
  purpose -- ship real fixes, not just report bugs -- starts here.
  - **Finding 1 (iw5sp.exe Steamworks P2P receive-path overflow)**: hooks the
    real Steamworks SDK `ReadP2PPacket` interface method, scoped by return
    address to only the one confirmed vulnerable call site, clamping the
    attacker-reported packet size to the real 1200-byte destination buffer
    before forwarding to the real implementation.
  - **Findings 2+3 (iw5mp.exe `matchdatadone` dispatcher overflow and
    `pa_memberjoin` party-join handler overflow)**: both share the same root
    cause and the same underlying copy primitive, so both are fixed via one
    shared hook on that primitive, scoped by exact return address to each
    finding's own specific vulnerable call site (not the whole enclosing
    function -- `pa_memberjoin` calls the same primitive a second time for
    something unrelated, which this fix deliberately leaves untouched).
    Length is clamped to each destination's real buffer size (1020 bytes,
    1024-byte buffer; 512 bytes, matching buffer) before the copy runs.
  - **Finding 4 (fragment-reassembly OOB write) is NOT fixed this pass** --
    see `re_notes/vulnerability_research.md`'s own new entry for why: the
    destination address is computed at runtime as a per-connection buffer
    base plus an attacker-controlled offset, and the shared-primitive-hook
    technique used for findings 2/3 can't recover that buffer's real bounds
    from the address alone. A full-function replacement was considered and
    rejected this pass -- it risks silently dropping undocumented behavior in
    the function's own "magic-byte-range/version-compat checks" section,
    which this project doesn't yet fully understand. Real next step, not
    guessed at.
  - **Build-verified: YES, as of this pass.** The implementation was
    originally produced in an isolated environment that could not run
    MSBuild at all; a follow-up pass fixed one real bug found in that
    process (an invalid `--` inside an XML comment in `proxy_d3d9.vcxproj`,
    which MSBuild's XML parser rejects outright), then completed a clean
    `/t:Rebuild` (0 errors) and confirmed via `dumpbin /headers` that the
    output is a real x64 DLL exporting all 16 required D3D9 functions.
    Independently re-verified the P2P fix's own 52-byte signature against a
    fresh raw byte dump of the live binary -- exact match, not just internally
    self-consistent. **Still NOT live-tested** -- build success confirms the
    code compiles and the signatures/offsets are real, not that the fixes
    behave correctly against actual malicious network traffic. That
    remains open.

- **Finding 1's fix was silently never installing on its actual first live
  execution (2026-09-05) — root-caused and fixed the same day.** The first
  time this whole pipeline ever ran against a real, live game process (via
  the sibling MW32011NCP project's new "greenlit" plugin path), the live
  log showed `SteamNetworking() returned null` — `steam_api64.dll` was
  loaded and exported the symbol, but Steamworks itself hadn't finished
  initializing inside the game process yet at `DLL_PROCESS_ATTACH` time.
  The fix silently gave up on that one attempt: it built, loaded, and
  logged cleanly, but provided zero real protection. Fixed by splitting
  Steamworks resolution out of the one-shot install path into its own
  retry loop (a background thread, once/sec for up to 30sec, matching
  MW32011NCP's own established event-driven/periodic-retry pattern for
  "wait for something to become ready" cases) — a genuinely permanent
  failure (missing export, hook-install failure) still gives up
  immediately, only the transient "not ready yet" case retries.
  Build-verified both distribution targets, redeployed. **Not yet
  live-retested against a session where Steamworks actually becomes ready
  during the retry window.**

### Investigated-not-resolved
- **Plutonium `iw5sp.exe` P2P vulnerability cross-check: CONFIRMED same bug
  present, byte-for-byte identical machine code (2026-07-17, later
  session).** Followed up the earlier Plutonium binary comparison by
  independently importing and fully analyzing Plutonium's own `iw5sp.exe`
  from scratch (not assuming any retail address carries over, since it's a
  differently-compiled binary). Result: every instruction in the vulnerable
  P2P-receive function is unmodified between retail and Plutonium's build —
  not just the same bug class, the exact same machine code, despite the two
  binaries differing elsewhere. Strongest available confirmation that this
  is a shared-binary issue, not something either distribution introduced
  independently.
  - **Bonus finding, significant enough to flag on its own**: this pass
    surfaced a real, substantial, actively-used raw-socket (`WS2_32.dll`)
    networking subsystem in `iw5sp.exe` — dozens of real call sites across
    distinct functions, not vestigial — entirely unexplored by any research
    so far, and structurally separate from the Steamworks P2P path this
    project has focused on. Strong candidate for the next research pass.
  - Exact addresses/disassembly stay in the gitignored internal notes per
    this project's redaction policy (still unpatched). Full detail
    (redacted appropriately) in `re_notes/vulnerability_research.md`.
- **Direct cross-check against Plutonium's own installed binaries
  (2026-07-17, later session), plus disclosure-language and fact
  corrections.** Prompted by the user's own realization that a client-side
  bug in a byte-identical binary implies Plutonium isn't fully insulated
  either — refined the doc's framing from a blanket "Plutonium isn't safe"
  claim to the precise, defensible one: a vulnerable client-side code path
  exists in the shared binary, and whether Plutonium's own deployment makes
  it unreachable in practice is a separate, unconfirmed question this
  project hasn't investigated. Then did the actual direct verification:
  - Re-confirmed `iw5mp.exe` byte-identical via direct SHA256 (not just
    citing the earlier claim) — this alone proves the confirmed MP
    client-side finding is present in Plutonium's client too, no further
    work needed for that specific question.
  - Found and corrected two factual errors surfaced by this check: an
    earlier claim that `iw5sp.exe` has no `WS2_32.dll` import was wrong
    (verified via `dumpbin /imports`, both retail and Plutonium import it
    identically — flags a new, unexplored raw-socket attack surface,
    doesn't overturn the Steamworks-P2P conclusion which was reached via
    call-site tracing); and a previously-recorded "~175KB smaller" size
    difference for Plutonium's `iw5sp.exe` was wrong — actual delta is
    2,320 bytes, the ~175KB figure was very likely a byte-difference COUNT
    mistaken for a file-size difference. Also corrected in the sibling
    `MW32011NCP` project's own notes (cross-reference policy).
  - Launched a focused pass to determine whether Plutonium's own,
    differently-compiled `iw5sp.exe` still has the confirmed P2P
    receive-path vulnerability — in progress, not yet resolved as of this
    entry; full detail (redacted while unresolved) in
    `re_notes/vulnerability_research.md` and the internal notes.
- **Four-fork netcode sweep across both binaries (2026-07-17, later
  session).** Prompted by reconciling "Activision patched Steam-Auth" against
  Plutonium's continued claims that vanilla Steam MW3 is unsafe — found the
  two aren't about the same bug (see `re_notes/vulnerability_research.md`'s
  "Reconciling..." section for the community-documented "MemberJoin RCE"
  citation). Independently swept both binaries' netcode for the same bug
  class and found:
  - **A second, currently-unpatched candidate vulnerability in `iw5mp.exe`**,
    in what looks like a compressed/fragmented message reassembly routine —
    an out-of-bounds write at an attacker-controlled offset, same
    "checked but not enforced" shape as the first finding, plausibly the
    real Huffman/CVE-2018-10718 bug class. A third, structurally similar,
    not-yet-fully-confirmed candidate also found.
  - **A separate, genuinely server-side per-client dispatcher subsystem
    located in `iw5mp.exe`** (the one call site audited there is safe) —
    the most plausible home for the community-reported "MemberJoin RCE,"
    flagged as the strongest lead for the "malicious client attacks the
    hosting player" direction, which no finding so far actually covers.
  - **The original `iw5mp.exe` finding's reachability was corrected**: both
    client-side (malicious server → connecting client) and via a malicious
    demo file — NOT reachable from a malicious client against a hosting
    player, as originally guessed from structure alone.
  - **A new, independently confirmed, currently-unpatched vulnerability
    found in `iw5sp.exe`'s Spec-Ops/Survival co-op networking** (Steamworks
    P2P receive path) — notably has no bounds check at all, not even a
    logged-but-unenforced one, and is confirmed reachable continuously
    (once per frame) during any active P2P session with a malicious peer.
  - **A real DoS/reflection-amplification gap found in `iw5mp.exe`'s
    pre-auth OOB dispatcher**: zero rate-limiting anywhere in that table,
    with `getinfo` showing a ~60-70x request/reply size factor — the same
    shape CoD4x_Server's `SVC_RateLimit` was built to close. Concrete Phase
    2 target.
  - **Corrected a factual error in this project's own prior research**: a
    previous claim that `iw5sp.exe` had its own "connection/challenge-
    adjacent code" at two specific addresses was wrong — both were
    unrelated unlock-notification code, a stale carry-forward of an already-
    documented false lead. Retracted.
  - Exact addresses/disassembly for every still-unpatched item above stay in
    the gitignored `re_notes/INTERNAL_vulnerability_research.md` per this
    project's redaction policy — no fixes written yet, this was a research
    pass. Full detail (redacted appropriately) in
    `re_notes/vulnerability_research.md`.
- **Steam-Auth (CVE-2018-20817) does NOT reproduce against this project's own
  retail `iw5mp.exe` (2026-07-17, later session).** Full decompile +
  disassembly of the real `"steamauth"` OOB packet handler (`FUN_005704b0`,
  reached via the real dispatcher `FUN_005763f0`) found a correct, sound
  length check (unsigned comparison, exactly matching the destination
  buffer's 2048-byte size, and correctly rejecting a sign-extended negative
  length rather than being bypassed by one) guarding the copy, with actual
  ticket validation delegated entirely to Valve's own Steamworks SDK
  (`SteamGameServer()`) rather than parsed by MW3's own code. This retail
  Steam build is very likely a post-fix build (the CVE itself notes a fix
  existed for versions before 2015-08-11). **There is no fix to write at this
  location** — this candidate is closed as a non-issue, not implemented as a
  patch. Full trail in `re_notes/vulnerability_research.md` and
  `re_notes/INTERNAL_vulnerability_research.md`. Next candidates to verify:
  Huffman (CVE-2018-10718), the MW3-specific 2019 protocol exploit, and
  PartyHost/joinParty (CVE-2019-20893).
- Also added a project-wide **redaction policy** (`SECURITY.md`, addendum in
  `CODE_STANDARDS.md`): exact addresses of still-unpatched vulnerable code
  stay in a gitignored `re_notes/INTERNAL_*.md` counterpart to each public
  doc, never committed, until a fix ships — everything else (CVEs,
  architecture, methodology) stays fully public. Repo itself remains public;
  this is a content-level discipline, not an access-control change.
- **`iw5mp_server.exe` (the dedicated-server binary) installed and directly
  RE'd against `iw5mp.exe` for the first time (2026-07-21).** Previously
  absent from this project's install; user installed it (App ID 42750) to
  answer a real scoping question — since MW3 uses a listen-server model
  where an ordinary player's own machine can run server code, does patching
  `iw5mp.exe` alone actually protect a hosting player, or does the real
  vulnerable logic only fully exist in the separate dedicated-server binary?
  Found: the real OOB dispatcher exists in both (`FUN_004fdf60` server vs.
  `FUN_005763f0` client), same command set and order, structurally identical
  but at different addresses (independent compiles from shared source) — one
  real asymmetry, a server-only `"queryserverinfo"` command absent from the
  client's own dispatcher. The already-confirmed-safe Steam-Auth
  (CVE-2018-20817) bounds check was independently re-verified present in the
  server binary's own `steamauth` handler (`FUN_004f6eb0`) too — same sound
  check-before-copy shape as the client's `FUN_005704b0`. **Net conclusion**:
  near-identical structure/logic but not byte-identical and no shared
  addresses — any future patch DLL that wants to protect a self-hosted
  dedicated server, not just a P2P-hosting client, needs its own independent
  signature scan and hook table against `iw5mp_server.exe` specifically.
  Full detail in `re_notes/vulnerability_research.md`'s "Extended to
  `iw5mp_server.exe`" section.

### Docs
- **Project planning complete, scaffolding created (2026-07-17).** No code
  written yet. Established: scope (client-side hardening, both `iw5sp.exe`
  co-op and `iw5mp.exe` MP), a research-backed vulnerability list (two real
  CVEs, one explicitly naming MW3; one MW3-specific still-unpatched
  client-side exploit; one 2012 crash of unconfirmed current-patch-status; GSC
  VM flagged as under-documented and requiring original research), a phased
  implementation plan (patch known bugs → general hardening → GSC VM
  research), and the planned architecture (same proxy-DLL injection technique
  as the sibling `MW32011NCP` project, targeting the vulnerable network-
  parsing functions instead of input functions). Full detail in `CLAUDE.md`
  and `re_notes/vulnerability_research.md`.
- **First research/verification batch (2026-07-17, later session).** Seven
  parallel research forks: partial verification of the original 3
  vulnerabilities against this project's own binaries (found the real OOB
  packet dispatcher and a strong candidate handler for the Steam-Auth CVE,
  `FUN_005704b0` in `iw5mp.exe`, not yet confirmed vulnerable), a fourth
  candidate vulnerability found (CVE-2019-20893, same missing-length-check bug
  class), a real netcode architecture map (the OOB dispatcher and its full
  command table), CoD4x_Server's actual hardening code extracted in full as a
  concrete Phase 2 blueprint (real challenge-response/rate-limiting/infostring-
  fix code, not just descriptions), GSC VM security research (genuinely
  inconclusive, Phase 3's low-priority framing confirmed reasonable), a
  significant threat-model finding (**MW3 uses a real listen-server/host-
  migration model, meaning an ordinary player's own machine can run the
  vulnerable server-side code whenever they're the lobby host** — scope now
  explicitly includes protecting the hosting player from a malicious peer,
  not just a connecting client from a malicious server), and an investigation
  into Plutonium's own claimed fix for these same vulnerabilities — their
  client binary is confirmed byte-identical to retail, and forum/staff
  statements suggest their real mitigation is architectural (steering play to
  Plutonium-operated dedicated servers instead of the vulnerable player-hosted
  listen-server model) and protocol-level (bypassing Steam's ticket-auth path
  server-side rather than patching the buggy parser) — a reasonable inference,
  not independently confirmed. Full detail in `CLAUDE.md` and
  `re_notes/vulnerability_research.md`.

---
