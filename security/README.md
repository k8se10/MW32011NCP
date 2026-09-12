# MW3 Netcode Security Patch (MW32011NSP)

A defensive security patch for Call of Duty: Modern Warfare 3 (2011, IW5
engine) — finding and fixing real, exploitable vulnerabilities in this
game's netcode. This engine generation has a real, documented history of
network-reachable buffer-overflow bugs, and Steam's VAC (confirmed active
on `iw5mp.exe`) does not protect against any of them — VAC is signature-based
cheat detection, a completely different threat model from a malicious
server or peer sending crafted packets. That gap is why this project exists.

This is a sibling project to [`MW32011NCP`](https://github.com/k8se10/MW32011NCP)
(the native controller and enhancement mod for the same game) — see
**Relationship to MW32011NCP** below for how the two connect.

## Status

**Four vulnerabilities confirmed via original reverse engineering; three are
fixed.** All four were independently re-verified present, byte-for-byte
unpatched, in MW3's own 2026-09-03 binary update (its first-ever, a 32-bit
to 64-bit recompile) — the update was a real opportunity to fix them and
didn't. Full technical trail in `re_notes/vulnerability_research.md`.

| Finding | Binary | Status |
|---|---|---|
| Steamworks P2P receive-path overflow | `iw5sp.exe` | **Fixed**, build-verified, not yet live-tested |
| `matchdatadone` dispatcher stack overflow | `iw5mp.exe` | **Fixed**, build-verified, not yet live-tested |
| `pa_memberjoin` party-join handler overflow | `iw5mp.exe` | **Fixed**, build-verified, not yet live-tested |
| Fragment-reassembly out-of-bounds write | `iw5mp.exe` | **Open** — real technical blocker documented, not guessed around |
| Steam-Auth (CVE-2018-20817) | `iw5mp.exe` | Checked, confirmed already safe — no fix needed |

Three of the four unpatched findings were reported to Activision through
their official disclosure channel; a public, general-risk-category notice
(no exploit-enabling detail) is also live on MW32011NCP's own `README.md`,
since MW3 is affected regardless of whether that mod is installed.

## Scope

- **Primary goal: protect players joining public/untrusted servers** —
  client-side hardening, not primarily a dedicated-server-operator tool
  (though server-side findings that help operators are a natural byproduct).
- **Covers both target binaries**: `iw5mp.exe` (Multiplayer) and `iw5sp.exe`
  (Campaign/Survival/Spec-Ops — the co-op networking path shares the same
  vulnerability surface as MP, confirmed via original research, not just a
  Campaign side-effect).
- `iw5sp.exe` and `iw5mp.exe` are separately-built binaries, reverse-engineered
  independently — a fix verified in one is never assumed to carry over to
  the other without its own confirmation.

## How the fixes work

Same proxy-DLL injection technique already proven working in the sibling
`MW32011NCP` project (a proxy `d3d9.dll` gets code execution inside the game
process), but hooking the real, vulnerable network-message-parsing functions
directly instead of input functions. Each fix is a pre-hook that validates/
clamps incoming attacker-controlled data to a real, known-safe size *before*
the vulnerable copy runs — the original vulnerable code path is otherwise
left completely intact, and every unrelated caller of a shared primitive
passes through unmodified (hooks are scoped by return address, not
whole-function or whole-primitive). See `proxy_d3d9/src/` for the actual
implementation and each fix module's own detailed header comment.

## Relationship to MW32011NCP

The fix code ships **three ways**, all built from the same shared source
(`proxy_d3d9/src/netcode_fixes.h` and its per-finding modules):

1. **Standalone** (`proxy_d3d9/`) — this project's own injected `d3d9.dll`,
   for anyone who wants the protection independent of MW32011NCP, or who
   doesn't use it at all.
2. **Built into MW32011NCP directly, on by default** (`tools/ncp_plugin_netcode_fixes/`,
   builds to `mw32011nsp_security.dll`) — NCP's own plugin loader has a
   small, explicit "greenlit" allowlist that loads this one specific,
   first-party, defensive-only plugin automatically, without requiring a
   player to have opted into arbitrary third-party plugins. See NCP's own
   `PLUGIN_API.md` for the full design.
3. **The same DLL as #2, placed manually** — for anyone who wants it as an
   ordinary opt-in plugin on their own NCP install without the default-on
   behavior.

If you want controller support for this game too, see
[MW32011NCP](https://github.com/k8se10/MW32011NCP) — the same proxy-DLL
technique, same reverse-engineering methodology, a different problem.

## Phased plan

1. **Patch the confirmed, documented bugs** (the table above) — three of
   four done.
2. **General hardening** against undiscovered bugs of the same class — rate
   limiting, stronger connection-challenge validation, broader bounds-checking
   audits — modeled on real precedent from `CoD4x_Server` (one engine
   generation earlier, same lineage, already ships exactly this class of
   hardening). Not started.
3. **GSC VM security research** — lower priority, genuinely under-documented
   publicly, needs original RE work rather than a literature search. Not
   started.

## A note on responsible disclosure

This project's own `re_notes/` is, by necessity, a running record of real
vulnerabilities in the base game — documented precisely enough to verify a
fix works, not as a how-to-exploit resource. Exact addresses/offsets of a
confirmed vulnerability that isn't fixed yet are never committed to this
public repo (see `SECURITY.md`'s "Redaction policy") — a gitignored internal
file holds the real detail until a fix ships and is verified, at which point
the public doc is updated to match.

## Credits

This project vendors and links
[MinHook](https://github.com/TsudaKageyu/minhook) (Copyright © 2009–2017
Tsuda Kageyu, BSD 2-Clause-style license) for all API hooking, and the
Hacker Disassembler Engine (HDE) 32/64 C it bundles — same library, same
terms, as the sibling `MW32011NCP` project.

## License

See [`LICENSE`](LICENSE). Free to use, modify, and fork; neither this
project nor any derivative may be sold or placed behind a paywall. Does not
grant any rights to Call of Duty: Modern Warfare 3 itself — this is an
unofficial, fan-made security patch, not affiliated with or endorsed by
Activision or Infinity Ward.

## Contributing

See [`CONTRIBUTING.md`](CONTRIBUTING.md) and [`CODE_STANDARDS.md`](CODE_STANDARDS.md)
before opening a PR, plus this project's own responsible-disclosure norms
for anything touching a real vulnerability (`SECURITY.md`).
