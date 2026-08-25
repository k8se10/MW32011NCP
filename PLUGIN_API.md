# Plugin API

MW32011NCP can load third-party (or your own) plugin DLLs at startup, giving
them hook-installation and direct process-memory read/write access, plus a
small set of mod-specific extension points (currently: overriding the color
of every piece of text/glyph this mod renders).

**This is a general MW3 (2011) extension point, not limited to extending
this mod specifically.** `InstallHook`/`ReadMemory`/`WriteMemory` operate on
the game process itself -- a plugin can hook or touch any part of
`iw5sp.exe`/`iw5mp.exe`, not just this mod's own internal state. In practice
this makes MW32011NCP double as a lightweight loader/platform for MW3
sub-mods, with this mod's own controller layer as the first (and currently
only) real consumer of that surface. Worth keeping in mind for anyone
evaluating this project, or writing a plugin: you're not limited to
reskinning this mod's own UI.

**Read this whole file before enabling plugin loading.** This is a real
capability class, not a toy.

## Scope — what ships, what doesn't

- **The plugin-loading infrastructure itself** (`mw3ncp_plugin_api.h`,
  `plugin_loader.h`/`.cpp`, the `[Plugins] Enabled` config key) **ships as
  part of the main mod**, in every release of `d3d9.dll`.
- **Actual plugin DLLs do not ship with the main mod's release.** Nothing is
  bundled, nothing is pre-loaded. The one exception — the "RGB Text" example
  plugin (`tools/example_plugin_rgb_text/`) — is a dev/demo build you compile
  yourself from this repo; it is not included in the mod's own release zip
  either.

## Risk statement — read this before setting `Enabled=1`

A loaded plugin runs with full access to the game process: it can install
its own hooks (reusing this mod's own MinHook instance) and read/write
arbitrary process memory. **This is exactly the capability class the main
mod's own code deliberately never uses on itself** — this project's
aim-assist feature was permanently removed (not just disabled) specifically
because reading live gameplay-entity memory is mechanically identical to a
soft-aimbot regardless of intent (`re_notes/known_issues.md` issue #33).
That policy is unchanged and does not apply to plugins the same way, because:

- Plugin loading is **strictly opt-in, off by default**. A fresh install, or
  anyone who never sets `[Plugins] Enabled=1`, is completely unaffected —
  the plugin directory is never even scanned.
- A plugin is **your own choice each time you enable this**, not something
  this project ships pre-loaded or defaults to.
- This project **makes no safety, correctness, or VAC-risk claim about what
  any plugin does.** That responsibility sits entirely with whoever wrote
  and whoever enabled the plugin — the same as any other injected DLL or
  memory-editing tool.

If you don't know why a specific plugin needs memory read/write access,
don't run it.

## Enabling plugin loading

In `mw3ncp_config.ini`:

```ini
[Plugins]
Enabled=1
```

Then place plugin `.dll` files in a `plugins` folder next to the deployed
`d3d9.dll` (i.e. in the game install root, alongside `mw3ncp_config.ini`).
Every `.dll` in that folder is scanned at startup; anything exporting
`MW3NCP_PluginInit` is loaded and initialized. Check `proxy_d3d9.log` for
`[plugin-loader]` lines to confirm what was found/loaded/rejected.

## Writing a plugin

Include `proxy_d3d9/src/mw3ncp_plugin_api.h` (the full ABI reference — read
that file's own comments for the complete contract) and export:

```c
extern "C" __declspec(dllexport) int MW3NCP_PluginInit(const MW3NCP_PluginAPI* api)
{
    if (!api || api->apiVersion < MW3NCP_PLUGIN_API_VERSION) return 0; // reject
    // ... use api->InstallHook / api->ReadMemory / api->WriteMemory / api->Log /
    // api->GetGameWindow / api->GetGameModuleBase / api->SetTextGlyphColorOverride
    return 1; // accept
}
```

Optionally also export `MW3NCP_PluginShutdown(void)` — called once during the
host's own `DLL_PROCESS_DETACH`, before it tears down.

The plugin ABI is a plain C function-pointer struct, deliberately — no C++
types (STL, virtual classes) cross this boundary, since those aren't
ABI-stable across separately compiled DLLs or compiler versions.

### API surface (v1)

| Function | What it does |
|---|---|
| `InstallHook(target, detour, &original)` | Installs a MinHook detour, reusing the host's own already-initialized MinHook instance. |
| `RemoveHook(target)` | Removes a previously installed hook. |
| `ReadMemory(addr, outBuffer, size)` | SEH-guarded raw memory read — returns 0 instead of crashing on an inaccessible page. |
| `WriteMemory(addr, buffer, size)` | SEH-guarded raw memory write, same safety guarantee. |
| `Log(msg)` | Writes to the same `proxy_d3d9.log` the host itself uses. |
| `GetGameWindow()` | The game's real `HWND`. |
| `GetGameModuleBase()` | This process's own module base address. |
| `SetTextGlyphColorOverride(callback)` | Registers a callback (`unsigned long (*)(unsigned long defaultColorArgb)`) called once per draw for every piece of text and controller-glyph icon this mod renders anywhere (gameplay hints, menu corner hints, the highlighted-item glyph, the custom Options screen's own text, every button-prompt icon). Does not affect solid UI chrome or the cursor. Pass `NULL` to clear. Only one override is active at a time. |

Callbacks run on the game's own render thread — keep them fast, no
blocking calls, same rule every other hook callback in this mod is held to.

## Example: RGB Text (`tools/example_plugin_rgb_text/`)

A real, working plugin that smoothly rainbow-cycles every piece of text and
glyph this mod draws, using the exact hue-cycle math the mod's own toast
notifications already use (`OverlayAnimStyle::Rainbow`). This is both a
usable dev-test/novelty plugin and this project's own live-verification
vehicle for the plugin API itself. Build the project (`Debug|Win32`), copy
`rgb_text_plugin.dll` into your `plugins` folder, set `[Plugins] Enabled=1`.

## Roadmap

The current API surface is deliberately minimal and developer-facing — hook
install, raw memory access, one cosmetic extension point. A friendlier,
higher-level API (more extension points, less "here's a raw memory pointer,
good luck") is a real intended direction for later, once real plugins exist
to learn from — not designed speculatively ahead of that.
