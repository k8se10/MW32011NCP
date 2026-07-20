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
| `changelog.bbcode.txt` | "Changelog" tab | BBCode. Condensed from `PATCHNOTES.md` — link there for full detail rather than reproducing everything. |
| `page-metadata.md` | Category / tags / requirements / permissions / install-instructions fields | Plain Markdown (internal reference only, Nexus has no single free-text field for most of this — it's separate form fields/dropdowns when actually creating the page). |

**Update checklist for every release** (mirrors the `PATCHNOTES.md` version-bump habit):
1. Re-check every ✅/🟡/⬜ claim in `description.bbcode.txt` against `README.md`'s
   current "Status at a glance" table — don't let a feature stay listed as working
   if it regressed, and don't forget to move a newly-confirmed feature up.
2. Add the new version's headline entry to `changelog.bbcode.txt` (newest on top).
3. Bump the version number and date in `description.bbcode.txt`'s status line.
4. If the summary's claims change enough to need it, re-check `summary.txt` is
   still accurate and still under 350 characters.
