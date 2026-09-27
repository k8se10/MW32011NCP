# x86 → x64 binary provenance diff + why the x64 build dies under a debugger (2026-09-27)

Static comparison of the pre-update x86 executables against the 2026-09-03
x64 executables, answering two questions:

1. **Is there evidence the x64 build is a console port?**
2. **Why does the x64 game crash instantly or misbehave when a debugger is attached?**

Everything below was reproduced from the binaries with the scripts in
[`provenance_scripts/`](provenance_scripts/). Addresses are VAs at the
default image base. No executables or decrypted DRM payloads are
committed (see `.gitignore`); re-run the scripts against your own install.

## Inputs

| File | SHA-256 | Machine | PE timestamp | PDB path |
|---|---|---|---|---|
| old `iw5sp.exe` | `ad47f21f…0c64` | x86 | 2012-11-30 | `C:\trees\iw5\game\pc\iw5sp.pdb` |
| old `iw5mp.exe` | `db3e87dc…bde2` | x86 | 2018-05-02 | `c:\trees\iw5-pc-release\game\pc\iw5mp.pdb` |
| old `iw5mp_server.exe` | `f271c305…6d20` | x86 | 2018-05-02 | `c:\trees\iw5-pc-release\game\pc\iw5mp_server.pdb` |
| new `iw5sp.exe` | `a97d2bbc…1023` | x64 | **2026-08-06 07:40:08** | `C:\trees\iw5\game\pc\iw5sp_ship.pdb` |
| new `iw5mp.exe` | `e4e9531d…8b286` | x64 | **2026-08-06 07:40:14** | `C:\trees\iw5\game\pc\iw5mp_ship.pdb` |

SP and MP were linked 6 seconds apart in the same build run, about four
weeks before the Steam push on 2026-09-03.

---

## Part 1: Console-port evidence

### Verdict

**There's no evidence that the x64 executables are a port of the Xbox 360 or
PS3 game code.** There's **strong evidence** that the rebuild came out of a
modern, multi-target pipeline built with a **Microsoft GDK** target in mind
next to Steam. In practice that means Microsoft Store / PC Game Pass /
Xbox PC app, and possibly an Xbox SKU. It's the same IW5 **PC** codebase
(9,016 identical strings in SP, D3D9 renderer kept, `game\pc\` build
folder), modernised and given GDK-aware switches.

### Strong evidence (new in x64, absent from all three old exes)

1. **`isgdk` / `issteam` menu-expression operators.** Old MP's
   menu-expression operator table ends at `doWeHaveMissingOwnedContent`
   (op `0x160`). New MP appends three operators:

   | op | name | switch case (MP, `FUN_1402dc130`) | behaviour |
   |---|---|---|---|
   | `0x161` | `isgdk` | `0x1402e5504` | returns **int 0** (shares the stub case with `doWeHaveMissingOwnedContent`) |
   | `0x162` | `issteam` | `0x1402e57e5` | returns **int 1** |
   | `0x163` | `ternary` | `0x1402e57f2` | real evaluator |

   Operator name table: MP `0x140552980` (357 entries), SP `0x1404d2b20`.
   Both values are compile-time constants. The same source clearly
   builds a GDK variant where these flip, and `.menu` assets can branch
   on it. This is the most direct "there is a GDK build of this" marker
   in the binary.
2. **Audio backend swapped from Miles to XAudio2 + X3DAudio.** Every
   `mss32.dll` `AIL_*` import is gone (plus `MILES sound sample allocation
   failed…`, `WIN_MSS_INIT_FAILED`). The new builds import
   `x3daudio1_7.dll` (`X3DAudioInitialize/Calculate`), create XAudio2 2.7
   through COM (`ole32!CoCreateInstance`/`CoInitializeEx`, RTTI
   `IXAudio2EngineCallback`, `IXAudio2VoiceCallback`,
   `XAudio2StreamingVoiceContext`, string `XAudio2_7.DLL`), and Bink moved
   to `BinkOpenXAudio2`. New dvars: `snd_usingXAudio2RestartFix`
   ("Used to enable restarting XAudio2 when the audio device changes"),
   `snd_omnidirectionalPercentage`, MP `snd_outputConfiguration`,
   `snd_restart_withdisconnect`. XAudio2 is the audio API shared by
   Xbox and the GDK. On its own this is *consistent with* a GDK-portable
   backend, not proof.
3. **DemonWare SDK replaced with a much newer multi-platform drop**,
   built from `d:\workspace\outrun\iw5-depot\iw5\code_source\DemonWare\…`.
   The old build had relative `.\bd*.cpp` paths, curl, and statically
   linked OpenSSL (all the `ssl_`/`x509_`/`d2i_`/`ec_` strings are gone).
   The new one has WinHTTP (`bdHTTPWorkerWinHTTP.cpp`), BCrypt RNG,
   `bdJSON`, `bdHTTPAuthService-steam.cpp`
   (`auth3.prod.demonware.net`, encrypted app tickets), Umbrella/UNO
   error tables. It compiles
   **`bdPlatform\bdPlatformSocket\bdPlatformStreamSocket-xboxone.cpp`**
   into the PC exe. That path is referenced by live code at
   `FUN_1403d6660` (SP), next to `bdPlatformSocket-win32.cpp`.
   *Caveat:* the `BD_AUTH_PS3_*`, `WIIU`, `3DS`, `TENCENT` error codes are
   generic DW SDK tables and prove nothing about this game.
4. **Toolchain / build config.** MSVC 14.29 (VS 2019 16.11, link build
   30154, Rich header also shows 29395/30034 objects), POGO + VC_FEATURE
   debug entries (PGO/LTCG), `_ship` PDB suffix, `.pdata` unwind info,
   CRT pulled up to the UCRT/FLS era. The old SP/MP were VS 2008 (linker
   9.0). **The `outrun` workspace name** shows up nowhere in the old binaries.
   It's presumably the internal name of the team or project behind the
   rebuild. The name alone doesn't tell us who that is.

### Checked and ruled out (do not cite these)

- `XBOXLIVE_*` localisation keys, `XUID` helpers, `ps3_dw_addrHandleTimeout`,
  `menu_xboxlive_*`, `PLATFORM_*` keys, splitscreen/safe-area dvars:
  **all already present in the old x86 PC builds**. They're shared IW5
  code, not new. (A naive `strings | comm` diff flags them as new because
  of string-pooling prefix bytes. Always check against raw bytes; `cstr.py`
  plus a raw `in` check does this.)
- `social_TU22.cfg` / `playlists_tu22.aggr` / `iotd_tu22-%s.txt`:
  "Title Update" naming, but the old builds already used `social_TU20/21`
  and `playlists_tu21`. It's a routine bump, not a console marker.
- Console gamepad layer: **none came across.** Zero `gpad_*`, `BUTTON_*`,
  `APAD_*`, `DPAD`, `xinput`, rumble identifiers in any of the five exes.
  The one `joystick` string was actually *removed*. This project's
  controller work is still the only controller path on PC.
- `GetCurrentPackageId`, `AppPolicyGetProcessTerminationMethod`,
  `CreateSymbolicLinkW`, `operator co_await` and similar: the standard
  VS 2019 static CRT/STL thunk and undname tables, present in any 16.11 build.

### Dev console / debug surface: unchanged

The presence matrix (`monkeytoy`, `developer`, `sv_cheats`, `noclip`, `devmap`,
`kill`, `viewpos`, `cg_drawFPS`, `timescale`, `devgui`, `ai_debug*`,
`r_ssaoDebug`, `con_*`, `rcon`, `cl_enableRCon`, …) is **identical old vs
new** for SP and MP. Nothing was added, nothing unlocked.

- `monkeytoy` ("Restrict console access") defaults are unchanged:
  - **SP: default 1** (console locked): new `FUN_14007acb0` → `Dvar_RegisterBool("monkeytoy", 1, ARCHIVE, …)`; old `0x538c80` pushes `1, 1`.
  - **MP: default 0**: new `FUN_1400b2cb0`; old `0x486710` pushes `1, 0`.
- Removed: `iwnet_debug` ("turn on iwnet debugging", SP), `dw-dev-ca.crt`,
  and the whole DXERR/DirectShow HRESULT text table. The old
  `"Debugger is present."` string is one of those HRESULT descriptions
  (next to `IPinFlowControl::Block()…`), **not** an anti-debug check.
- Added: `OutputDebugStringA` import (9 call sites, DW logging), DemonWare
  diagnostics (`BD_ASSERT`, `DW-Trace` header, `BITDEMON MEMORY LEAKS
  DETECTED`, `%u Bytes leaked in %u allocation(s)`), and DW's
  "defaulting to DEV environment" warnings. It also carries a
  `%s\devraw\%s\video\%s.bik` Bink search path next to the `raw\` and
  `main\video\` ones. That's a leftover dev-content lookup path; it
  doesn't expose anything by itself.

---

## Part 2: Why the x64 build breaks under a debugger

### Root cause: SteamStub v3.1 (x64) DRM with its anti-debug check enabled

Both new exes have a **`.bind`** section (entropy 7.96) holding the entry
point. The old x86 exes have no `.bind`, so no stub. **That's why
debugging worked before 2026-09-03 and doesn't now.**

Decrypted stub header (`steamstub.py`, header = 0xF0 bytes before EP,
rolling-XOR, signature `0xC0DEC0DF`):

| Field | SP | MP |
|---|---|---|
| Entry (stub) | `0x144484310` | `0x148981310` |
| **Original entry (OEP)** | **`0x140365c3c`** | **`0x1403cafdc`** |
| SteamAppId | 42680 (`0xa6b8`) | 42690 (`0xa6c2`) |
| Flags | `0x6` | `0x6` |
| `NoModuleVerification` (0x02) | SET | SET |
| `NoEncryption` (0x04) | SET: `.text` is plaintext | SET |
| `NoOwnershipCheck` (0x10) | clear: ownership **is** checked | clear |
| **`NoDebuggerCheck` (0x20)** | **clear: anti-debug active** | **clear** |
| `NoErrorDialog` (0x40) | clear | clear |

The stub decrypts and maps an embedded `steamdrmp.dll` (identical in SP
and MP, SHA-256 `c2dd765f…9c4a`, PDB
`C:\buildworker\steam_rel_rack_win64\…\drmpayload\win64\Release\steamdrmp.pdb`)
and calls its `steam` export. The anti-debug logic sits in its main
routine, `FUN_1800052c0`, gated on `test byte [hdr+0x3C], 0x20`:

```
18000530e  jne   skip                       ; NoDebuggerCheck set -> skip everything
180005314  call  [IsDebuggerPresent]
18000531c  je    +                          ; debugger seen ->
18000531e  mov   al, 0x54                   ;   fail with DRM error 0x54 ('T')
...
180005338  "ntdl" "l.dl" "l"                ; "ntdll.dll" built on the stack
180005325  "NtSetInformation" + "Thread"    ; name split to dodge string scans
180005363  call  [GetModuleHandleA]
180005371  call  [GetProcAddress]           ; -> ntdll!NtSetInformationThread
18000537a  call  [GetCurrentThread]
180005389  mov   edx, 0x11                  ; ThreadHideFromDebugger
18000538e  call  rbx                        ; NtSetInformationThread(cur, 0x11, NULL, 0)
...
1800053a2  t0 = GetTickCount()
1800057cd  if (GetTickCount() - t0 > 10000) fail 0x53   ; timing check, same 0x20 gate
```

That gives three separate failure modes, which together match "the game
crashes instantly and the debugger acts broken":

1. **Launched under a debugger** → `IsDebuggerPresent()` is true → the stub
   aborts with DRM error `0x54` before the game's own code (OEP) runs.
   The error-dialog flag is clear, so a Steam DRM error box may show
   instead of a silent exit. Either way the game never starts.
2. **Stepping or breaking inside the stub for more than 10 s** → the timing
   check fails with `0x53`.
3. **Attached after startup, or started with the `IsDebuggerPresent`
   check hidden** → the stub has already called
   `NtSetInformationThread(ThreadHideFromDebugger)` on the **thread that
   goes on to run OEP**, which is the game's **main thread**: WinMain, the
   frame loop, Pmove/usercmd, the renderer calls this project's hooks
   run on. From then on the kernel sends the debugger **no** debug
   events for that thread. Any `int3` or hardware breakpoint that thread
   hits becomes an exception nobody handles. The debugger never sees it,
   so the process is torn down on the spot with no break and no
   first-chance notification. That's the "crashes instantly when I put a
   breakpoint on a hooked function" symptom. Worker threads aren't
   hidden, so breakpoints hit by those work. That's why failures look
   inconsistent.

### Not the cause (checked)

- The game's own `IsDebuggerPresent` / `UnhandledExceptionFilter` /
  `TerminateProcess` uses are the standard MSVC CRT: `__report_gsfailure`
  (`FUN_140366170`: `IsProcessorFeaturePresent(0x17)` → `int 0x29`
  fastfail) and the CRT abort path (`FUN_1403ae3c0`). The only TLS
  callback (`0x140365660`) is the CRT's. The game adds no anti-debug of
  its own.
- CRT fastfail (`int 0x29`, reported as `0xC0000409
  STATUS_STACK_BUFFER_OVERRUN`) still applies to real `/GS` failures,
  such as a hook with the wrong prototype smashing the stack. That's the
  bug class in `known_issues_x64.md` #1. It's a separate crash
  signature from the DRM one above.

### Useful side facts for hooking and debugging

- `DllCharacteristics = 0x8120`: **no `DYNAMIC_BASE`** and no `.reloc`, so
  the image always loads at `0x140000000`. Ghidra addresses map 1:1 to
  live addresses on every run.
- `NoModuleVerification` is set and `.text` isn't encrypted, so the stub
  does **not** checksum game code. In-memory MinHook detours (what
  `proxy_d3d9` does) don't trip the DRM. Only debugging does.
- CFG is not enforced (`GuardFlags 0x100` instrumented only, no
  `GUARD_CF` DllCharacteristic).
- `d3d9.dll` is a static import, so the loader runs the proxy's
  `DllMain` **before** the stub's entry point, and therefore before
  `IsDebuggerPresent` and the thread hide happen.

### Workarounds for local RE and debugging

Pick one. These are for **local debugging only**; never redistribute a
modified or unwrapped executable.

1. **Strip the stub locally (Steamless).** It unpacks SteamStub 3.1 x64
   and restores OEP `0x140365c3c` / `0x1403cafdc`. With no stub there's
   no anti-debug. Ownership is still enforced by `SteamAPI_Init`, so run
   with Steam open, and drop a `steam_appid.txt` containing `42680` (SP)
   or `42690` (MP) next to the unpacked exe if launching outside Steam.
   Code addresses are unchanged, since `.text` was never encrypted.
2. **x64dbg + ScyllaHide from process start.** The plugin needs at least
   the *NtSetInformationThread* (ThreadHideFromDebugger) and
   *PEB BeingDebugged / IsDebuggerPresent* hooks enabled before the
   stub runs. Attaching later is too late: the main thread is already
   hidden.
3. **Neutralise it from the proxy (dev builds only).** Since the proxy
   `DllMain` runs before the stub, a developer-only switch could hook
   `ntdll!NtSetInformationThread` to ignore class `0x11` and clear
   `PEB->BeingDebugged` for the stub's check. Not implemented. It would
   need to be off by default and stay out of release builds, so it's a
   user decision.

## Reproduce

```
python3 provenance_scripts/pehdr.py old/iw5sp.exe new/iw5sp.exe       # headers, PDB, Rich
python3 provenance_scripts/imports.py old/iw5sp.exe new/iw5sp.exe     # import diff
python3 provenance_scripts/cstr.py new/iw5mp.exe new_mp.json          # strings -> VA
python3 provenance_scripts/steamstub.py new/iw5sp.exe new/iw5mp.exe   # DRM header + flags
python3 provenance_scripts/drmp.py new/iw5sp.exe drmp.dll             # local-only payload decrypt
python3 provenance_scripts/disall.py drmp.dll drmp.asm                # then grep FUN_1800052c0
python3 provenance_scripts/xref.py new/iw5sp.exe 0x1403f4000 40       # monkeytoy registration
```

Requires `pip install pefile capstone`.
