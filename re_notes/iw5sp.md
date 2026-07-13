# iw5sp.exe — RE notes (Campaign + Survival)

## Launch/testing environment
- **Steam App ID: 42680** (`Call of Duty®: Modern Warfare® 3 (2011)`) — confirmed via `appmanifest_42680.acf` in `D:\SteamLibrary\steamapps\`.
- Launching `iw5sp.exe` directly (not through Steam) fails fast: `SteamAPI_Init` has no App ID context, process exits within a few seconds. Fix for standalone test launches: drop a `steam_appid.txt` containing `42680` in the game root (Steam client must still be running). This is a standard Steamworks dev/testing convenience, not a DRM bypass — the Steam client still enforces ownership.
- On a successful launch the process **re-execs itself under a new PID** at least once (observed: `Start-Process` PID exits, a new `iw5sp` PID takes over holding the actual game session) — don't assume the PID returned by the launcher call is the one to monitor; re-check `Get-Process -Name iw5sp` after a few seconds.
- Confirmed reaching a live, responsive main window: `Get-Process | Select MainWindowTitle` shows `"Call of Duty®: Modern Warfare® 3"`, `Responding = True`.

## proxy_d3d9.dll verification (2026-07-13)
- Log file: `proxy_d3d9.log`, written next to whichever `.exe` loaded the DLL (game root). Opened in append mode across the process's lifetime — if you need to read it while the game is still running, a plain read from another process may hit a sharing violation; simplest is to wait for the process to exit, or read after `CloseMainWindow`.
- Confirmed via log: `Direct3DCreate9` gets called **multiple times per launch** (3–4 observed) — the engine appears to probe/reinit D3D9 more than once during startup (adapter enumeration and/or windowed-mode shim behavior). Don't assume a single call.
- Full pass-through proxy (forward everything, implement only `Direct3DCreate9` as a logging wrapper around the real call) loads cleanly and the game behaves identically to vanilla through Campaign/Survival's front-end — no crash, no visible regression.

## Not yet done
- No hooking of `IDirect3D9::CreateDevice` or `IDirect3DDevice9::Present` yet (task #3 follow-up / task #4 groundwork) — proxy is currently a transparent pass-through only.
- Usercmd-build / view-angle-update / menu-input function locations: not yet located (task #4).
