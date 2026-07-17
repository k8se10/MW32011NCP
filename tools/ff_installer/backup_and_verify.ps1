<#
.SYNOPSIS
    Backup + hash-verification utility for replacing native IW5 fastfiles (ui.ff /
    ui_mp.ff), safety infrastructure for the "worst case" fallback architecture
    (task #23 -- see re_notes/known_issues.md issue #23 for why live in-memory zone
    injection can't carry real menu content).

.DESCRIPTION
    Never overwrites anything blindly. Every run either backs up an as-yet-unbacked-up
    file, or reports the current file's relationship to the recorded original hash
    (unchanged / diverged) -- diverging could mean a Steam update reverted a previous
    install, OR that our own mod's replacement is already in place; this script does
    NOT try to guess which, it just reports plainly and leaves the decision to the
    caller. Actually installing a replacement .ff is a separate, not-yet-written
    script (install_ui_ff.ps1) that will require this backup to exist first.

    Backups live in <GameRoot>\mod_backups\, a new top-level folder sibling to zone\
    and main\ -- deliberately NOT inside zone\ so the game's own zone-scanning code
    never sees it. This is a new class of write to the game install root beyond the
    proxy d3d9.dll itself; CLAUDE.md requires explicit user confirmation before this
    kind of on-disk change, given 2026-07-17 ("worst case we ship the mod as an
    installer which replaces native ffs but always makes a backup in a separate
    folder of the og checking hash matches too").

.PARAMETER GameRoot
    Path to the MW3 install root. Defaults to this project's known install path.

.PARAMETER Files
    Relative paths (from GameRoot) of the fastfiles to back up/verify. Defaults to
    zone\english\ui.ff. Add zone\english\ui_mp.ff once MP work starts (task #23 is
    SP/Survival-scoped for now, per CLAUDE.md's locked "SP+Survival first" ordering).

.PARAMETER Restore
    Copies the backed-up original back over the current file, then re-verifies the
    restored file's hash matches what was recorded at backup time. Refuses to run
    if no backup exists for a requested file.

.EXAMPLE
    .\backup_and_verify.ps1
    First run: backs up zone\english\ui.ff, records its SHA256 in the manifest.
    Later runs: reports whether the current file still matches that hash.

.EXAMPLE
    .\backup_and_verify.ps1 -Restore
    Restores zone\english\ui.ff from mod_backups\, verifies the restore succeeded.
#>

[CmdletBinding()]
param(
    [string]$GameRoot = "D:\SteamLibrary\steamapps\common\Call of Duty Modern Warfare 3",
    [string[]]$Files = @("zone\english\ui.ff"),
    [switch]$Restore
)

$ErrorActionPreference = "Stop"

$backupDir = Join-Path $GameRoot "mod_backups"
$manifestPath = Join-Path $backupDir "manifest.json"

function Get-Manifest {
    if (Test-Path $manifestPath) {
        return Get-Content $manifestPath -Raw | ConvertFrom-Json -AsHashtable
    }
    return @{}
}

function Save-Manifest([hashtable]$manifest) {
    if (-not (Test-Path $backupDir)) {
        New-Item -ItemType Directory -Path $backupDir | Out-Null
    }
    $manifest | ConvertTo-Json -Depth 5 | Set-Content -Path $manifestPath -Encoding UTF8
}

function Get-FileHashHex([string]$path) {
    return (Get-FileHash -Path $path -Algorithm SHA256).Hash
}

function Backup-OneFile([string]$relPath, [hashtable]$manifest) {
    $srcPath = Join-Path $GameRoot $relPath
    if (-not (Test-Path $srcPath)) {
        Write-Warning "Not found, skipping: $relPath"
        return
    }

    $backupName = ($relPath -replace '[\\/]', '__') + ".orig"
    $backupPath = Join-Path $backupDir $backupName
    $currentHash = Get-FileHashHex $srcPath

    if ($manifest.ContainsKey($relPath)) {
        $recorded = $manifest[$relPath]
        if ($currentHash -eq $recorded.OriginalHash) {
            Write-Host "[unchanged] $relPath matches the recorded original hash ($currentHash)." -ForegroundColor Green
        }
        else {
            Write-Host "[DIVERGED] $relPath no longer matches the recorded original hash." -ForegroundColor Yellow
            Write-Host "  Recorded original : $($recorded.OriginalHash)"
            Write-Host "  Current file       : $currentHash"
            Write-Host "  This means either a Steam file-integrity check reverted a previous" -ForegroundColor Yellow
            Write-Host "  install, a game update shipped a new ui.ff, or our own mod's" -ForegroundColor Yellow
            Write-Host "  replacement is already installed. Not taking any action automatically" -ForegroundColor Yellow
            Write-Host "  -- compare against a known 'mod-installed' hash by hand, or re-run with" -ForegroundColor Yellow
            Write-Host "  -Restore if you want the ORIGINAL back." -ForegroundColor Yellow
        }
        return
    }

    if (-not (Test-Path $backupDir)) {
        New-Item -ItemType Directory -Path $backupDir | Out-Null
    }

    Write-Host "[backing up] $relPath -> mod_backups\$backupName" -ForegroundColor Cyan
    Copy-Item -Path $srcPath -Destination $backupPath -Force
    $backupHash = Get-FileHashHex $backupPath
    if ($backupHash -ne $currentHash) {
        throw "Backup verification FAILED for $relPath -- copied file's hash ($backupHash) doesn't match the source ($currentHash). Refusing to record this backup as trustworthy."
    }

    $manifest[$relPath] = @{
        BackupFile   = $backupName
        OriginalHash = $currentHash
        BackedUpUtc  = (Get-Date).ToUniversalTime().ToString("o")
    }
    Write-Host "[ok] Backed up and verified. SHA256: $currentHash" -ForegroundColor Green
}

function Restore-OneFile([string]$relPath, [hashtable]$manifest) {
    if (-not $manifest.ContainsKey($relPath)) {
        Write-Warning "No backup recorded for $relPath -- nothing to restore. Run without -Restore first."
        return
    }
    $recorded = $manifest[$relPath]
    $backupPath = Join-Path $backupDir $recorded.BackupFile
    $destPath = Join-Path $GameRoot $relPath

    if (-not (Test-Path $backupPath)) {
        throw "Manifest references $($recorded.BackupFile) but the file is missing from mod_backups\ -- cannot restore $relPath."
    }

    Write-Host "[restoring] mod_backups\$($recorded.BackupFile) -> $relPath" -ForegroundColor Cyan
    Copy-Item -Path $backupPath -Destination $destPath -Force
    $restoredHash = Get-FileHashHex $destPath
    if ($restoredHash -ne $recorded.OriginalHash) {
        throw "Restore verification FAILED for $relPath -- restored file's hash ($restoredHash) doesn't match the recorded original ($($recorded.OriginalHash))."
    }
    Write-Host "[ok] Restored and verified. SHA256: $restoredHash" -ForegroundColor Green
}

$manifest = Get-Manifest

foreach ($relPath in $Files) {
    if ($Restore) {
        Restore-OneFile -relPath $relPath -manifest $manifest
    }
    else {
        Backup-OneFile -relPath $relPath -manifest $manifest
    }
}

if (-not $Restore) {
    Save-Manifest $manifest
}
