# GitHub wiki source

This folder is the **single source of truth** for this project's GitHub
wiki — every `.md` file here (except this one) maps 1:1 to a real wiki page
of the same name. Edit here, commit, push to `main`; `.github/workflows/
wiki-sync.yml` republishes the real wiki automatically. **Never edit the
wiki directly through GitHub's web UI** — a direct edit there is not
tracked in this repo and will be silently overwritten by the next sync.

This exists because this project got burned by exactly the failure mode
it prevents: the wiki sat stuck at `v0.2.1` for weeks while the rest of the
project moved on through several releases, purely because syncing it was a
manual, easy-to-forget step with nothing tracking it (`CLAUDE.md`'s own
"Keeping this file current" section, 2026-08-19). Version-controlling the
wiki's own content here, same as every other doc in this repo, closes that
gap the same way `PATCHNOTES.md`'s own "update in the same pass" rule
already does for release notes.

## Pages

| File | Wiki page | What it covers |
|---|---|---|
| `Home.md` | Home | Landing page — current status, what this project is |
| `Known-Issues.md` | Known Issues | Player-facing summary; links to `re_notes/known_issues_x64.md` for full RE trail |
| `Changelogs.md` | Changelogs | Condensed release history; links to `PATCHNOTES.md` for full detail |
| `Configuration.md` | Configuration | Every `mw3ncp_config.ini` key |
| `Compatibility.md` | Compatibility | Per-client/per-mission compatibility breakdown |
| `Controller-Setup.md` | Controller Setup | Supported controllers, pairing/setup steps |
| `Installation-Guide.md` | Installation Guide | Step-by-step install/uninstall |
| `Troubleshooting.md` | Troubleshooting | Common problems and fixes |
| `FAQs.md` | FAQs | Frequently asked questions |
| `Technical-Documentation.md` | Technical Documentation | High-level architecture overview for contributors |
| `Development-Notes.md` | Development Notes | Contributor-facing dev notes |
| `_Footer.md` | (footer, every page) | Shown at the bottom of every wiki page |

## Keeping pages current

When a release changes overall project status (a new component, a release
shipping, a major scope change), re-check `Home.md` and `Known-Issues.md`
first — they're the highest-drift pages, since they describe current state
directly rather than fairly stable reference material (Configuration,
Controller-Setup, Troubleshooting, FAQs change far less often). Cross-check
against `README.md`'s own status section and `PATCHNOTES.md`'s current
in-progress version heading before writing a wiki update, the same way
`nexus/README.md`'s own "Keeping the page in sync" section already does for
the Nexus mod-page copy.
