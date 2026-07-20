# Nexus Mods page source

This folder is the **single source of truth** for this project's Nexus Mods page —
maintained alongside `README.md`/`PATCHNOTES.md` so the Nexus listing can't quietly
drift out of sync with what the project actually does. When a release changes what's
✅ working / 🟡 partial / ⬜ not working, update `description.bbcode.txt` in the same
pass as `README.md`'s own "Status at a glance" section — don't let them diverge.

Nexus renders **BBCode**, not Markdown — every file here ending `.bbcode.txt` is
ready to paste directly into the corresponding Nexus page field as-is, no conversion
needed. Character counts are noted where Nexus enforces a limit.

| File | Nexus field | Notes |
|---|---|---|
| `summary.txt` | "Summary" (short blurb shown in search/listing) | Plain text, no BBCode. Hard 350-char Nexus limit. |
| `description.bbcode.txt` | "Description" (main page body) | BBCode. No hard length limit, but keep it scannable. |
| `changelog.bbcode.txt` | "Changelog" tab (full, per-version paragraph summaries) | BBCode. Condensed from `PATCHNOTES.md` — link there for full detail rather than reproducing everything. |
| `changelog-oneline.bbcode.txt` | Per-file-upload "changelog" box (the short one shown when uploading a new file version, not the page's own Changelog tab) | BBCode. One line per version — same content as `changelog.bbcode.txt`, just compressed for the field that doesn't fit a paragraph. |
| `credits.bbcode.txt` | "Credits" field (Permissions/Credits tab) | BBCode. Author line, bundled-library credits (MinHook/HDE), special thanks, license pointer. |
| `page-metadata.md` | Category / tags / requirements / install-instructions fields | Plain Markdown (internal reference only, Nexus has no single free-text field for most of this — it's separate form fields/dropdowns when actually creating the page). |
| `zip-description.txt` | The per-file "Description" box shown next to each uploaded archive on the Files tab | Plain text. Describes what's IN that specific archive (currently the same contents as the GitHub release zip) — update the version number whenever a new archive is uploaded. |

**The actual file upload to Nexus is automated (2026-07-20)** — see
`.github/workflows/nexus-upload.yml`, which fires on every published GitHub
Release, downloads that release's zip asset, and pushes it to Nexus as a new
version of the existing file (`file_id 7686417`) via Nexus's official
`Nexus-Mods/upload-action`, using the `NEXUS_API_KEY` repo secret. This
covers the FILE upload only — the page text fields above (`description`,
`changelog`, `credits`, etc.) still need manually pasting into the Nexus web
UI when they change; the Upload API doesn't cover page-content edits yet.

**Update checklist for every release** (mirrors the `PATCHNOTES.md` version-bump habit):
1. Re-check every ✅/🟡/⬜ claim in `description.bbcode.txt` against `README.md`'s
   current "Status at a glance" table — don't let a feature stay listed as working
   if it regressed, and don't forget to move a newly-confirmed feature up.
2. Add the new version's headline entry to `changelog.bbcode.txt` (newest on top)
   AND its one-line equivalent to `changelog-oneline.bbcode.txt`.
3. Bump the version number and date in `description.bbcode.txt`'s status line.
4. Bump the version number in `zip-description.txt` to match the newly-uploaded
   archive.
5. If the summary's claims change enough to need it, re-check `summary.txt` is
   still accurate and still under 350 characters.
