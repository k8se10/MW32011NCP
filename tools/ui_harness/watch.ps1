# watch.ps1 -- OPTIONAL fallback only, as of 2026-08-05. mw3ncp_ui_harness.exe
# (main.cpp's SourceWatcherThreadProc) now does this itself -- watches the same
# source files and rebuilds ui_hot.vcxproj automatically in a background thread the
# whole time it's running, single-process, no second script required (previously
# this script WAS the only thing that triggered a rebuild at all; the harness only
# ever watched the build OUTPUT, so without this running nothing ever changed for it
# to pick up -- live-reported 2026-08-05 as "no visual changes even after restart"
# and "hot reload shouldnt rely on any scripts it should work just like hmr").
# Kept only for watching from a terminal with no harness instance running at all.
#
# Usage: run this in its own terminal, alongside a separately-launched
# bin\Debug\mw3ncp_ui_harness.exe. Leave both running; just edit and save.
#   powershell -File tools\ui_harness\watch.ps1

$ErrorActionPreference = 'Continue'
$root = Split-Path -Parent $MyInvocation.MyCommand.Path
$msbuild = "C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe"
$proj = Join-Path $root "ui_hot\ui_hot.vcxproj"

# Real UI code (proxy_d3d9\src) plus this tool's own DLL-side glue -- anything NOT
# in this list (e.g. main.cpp, host_stubs.cpp) is host-exe code and needs a manual
# rebuild + relaunch of ui_harness.vcxproj itself, not covered by this watcher.
$watchPaths = @(
    (Resolve-Path (Join-Path $root "..\..\proxy_d3d9\src")).Path,
    (Resolve-Path (Join-Path $root "..\..\proxy_d3d9\resource.h")).Path,
    (Resolve-Path (Join-Path $root "ui_hot")).Path
)

function Build {
    Write-Host "[watch] change detected, rebuilding ui_hot.dll..." -ForegroundColor Cyan
    & $msbuild $proj /p:Configuration=Debug /p:Platform=Win32 /nologo /v:minimal
    if ($LASTEXITCODE -eq 0) {
        Write-Host "[watch] build OK -- harness will hot-swap within ~500ms" -ForegroundColor Green
    } else {
        Write-Host "[watch] BUILD FAILED -- harness keeps running the last good version" -ForegroundColor Red
    }
}

Write-Host "[watch] watching for changes under:" -ForegroundColor Yellow
$watchPaths | ForEach-Object { Write-Host "  $_" }
Write-Host "[watch] Ctrl+C to stop.`n"

$lastBuildTime = Get-Date
$debounceMs = 300

$watchers = @()
foreach ($path in $watchPaths) {
    $isDir = (Get-Item $path).PSIsContainer
    $w = New-Object System.IO.FileSystemWatcher
    if ($isDir) {
        $w.Path = $path
        $w.Filter = "*.*"
        $w.IncludeSubdirectories = $true
    } else {
        $w.Path = Split-Path -Parent $path
        $w.Filter = Split-Path -Leaf $path
    }
    $w.NotifyFilter = [System.IO.NotifyFilters]::LastWrite
    $w.EnableRaisingEvents = $true
    $watchers += $w
}

try {
    while ($true) {
        $changed = $false
        foreach ($w in $watchers) {
            $result = $w.WaitForChanged([System.IO.WatcherChangeTypes]::Changed, 200)
            if (-not $result.TimedOut) { $changed = $true }
        }
        if ($changed) {
            $now = Get-Date
            if (($now - $lastBuildTime).TotalMilliseconds -gt $debounceMs) {
                Start-Sleep -Milliseconds $debounceMs # let the editor finish writing
                Build
                $lastBuildTime = Get-Date
            }
        }
    }
} finally {
    $watchers | ForEach-Object { $_.Dispose() }
}
