# Nexus Mods page source

This folder is the **single source of truth** for this project's Nexus Mods
page — maintained alongside `README.md`/`PATCHNOTES.md` so the Nexus listing
can't quietly drift out of sync with what the project actually does.

## Markdown is the source of truth; BBCode is a mirror

Nexus renders BBCode, not Markdown, so every rich-formatted field has two
files: a `.md` source you actually edit, and a `.bbcode.txt` mirror you paste
into Nexus. **Edit the `.md` file first, then hand-update its `.bbcode.txt`
mirror to match** — there's no build step, this is small and infrequent
enough that a disciplined manual mirror is simpler than tooling. The mapping
is direct and mechanical:

| Markdown | BBCode |
|---|---|
| `# Title` | `[center][size=6][b]Title[/b][/size][/center]` |
| `## Section` | `[size=4][b]Section[/b][/size]` |
| `**bold**` | `[b]bold[/b]` |
| `*italic*` | `[i]italic[/i]` |
| `` `code` `` | `[font=Courier New]code[/font]` |
| `[text](url)` | `[url=url]text[/url]` |
| `- list item` | `[list][*]item[/list]` |
| A horizontal section break | `[hr]` |

| File | Nexus field | Source |
|---|---|---|
| `summary.txt` | "Summary" (short blurb shown in search/listing) | Plain text, no Markdown/BBCode needed — edit directly. Hard 350-char Nexus limit. |
| `description.md` → `description.bbcode.txt` | "Description" (main page body) | Edit the `.md`, mirror to the `.bbcode.txt`. No hard length limit, but keep it scannable. |
| `changelog.md` → `changelog.bbcode.txt` | "Changelog" tab | Edit the `.md`, mirror to the `.bbcode.txt`. Condensed from `PATCHNOTES.md` — link there for full detail rather than reproducing everything. |
| `changelog-oneline.bbcode.txt` | Per-file-upload "changelog" box (the short one shown when uploading a new file version) | Simple enough (one bolded version tag per line) to author directly in BBCode — same content as `changelog.md`, compressed to one line per version. |
| `credits.md` → `credits.bbcode.txt` | "Credits" field (Permissions/Credits tab) | Edit the `.md`, mirror to the `.bbcode.txt`. |
| `page-metadata.md` | Category / tags / requirements / install-instructions fields | Plain Markdown, internal reference only — Nexus has no single free-text field for most of this, it's separate form fields/dropdowns when actually creating the page. |
| `zip-description.txt` | The per-file "Description" box on the Files tab | Plain text, edit directly. Describes what's IN that specific archive — update whenever a new archive is uploaded. |

## Keeping the page in sync

When a release changes what's confirmed-live / build-verified-only / not
yet implemented, update `description.md` (then mirror to
`description.bbcode.txt`) in the same pass as `README.md`'s own status
section — don't let them diverge.

**The actual file upload to Nexus is automated** — see
`.github/workflows/nexus-upload.yml`, which fires on every published GitHub
Release, downloads that release's zip asset, and pushes it to Nexus as a new
version of the existing file via Nexus's official `Nexus-Mods/upload-action`.
This covers the FILE upload only — the page text fields above still need
manually pasting into the Nexus web UI when they change.

**Gotcha**: this workflow's trigger fires specifically on the transition
into "published" — it does not fire when converting a release TO Draft, but
it WILL fire again if an archived release is ever un-drafted later,
automatically re-uploading that build to Nexus as a new file version. Before
un-drafting any archived release, either disable this workflow first or plan
to immediately delete the resulting Nexus file version it creates.

## Update checklist for every release

1. Re-check every status claim in `description.md` against `README.md`'s
   current feature-status table — don't let a feature stay listed as working
   if it regressed, and don't forget to move a newly-confirmed feature up.
   Mirror the change to `description.bbcode.txt`.
2. Add the new version's headline entry to `changelog.md` (mirror to
   `changelog.bbcode.txt`) AND its one-line equivalent to
   `changelog-oneline.bbcode.txt`.
3. Bump the version number in `zip-description.txt` to match the
   newly-uploaded archive.
4. If the summary's claims change enough to need it, re-check `summary.txt`
   is still accurate and still under 350 characters.
