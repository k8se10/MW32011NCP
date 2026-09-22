@echo off
REM Regenerates src\git_version.h with the current short commit hash (2026-09-22, dev-build watermark).
REM Called from a PreBuildEvent (proxy_d3d9.vcxproj). Falls back to "unknown" if git isn't on PATH or this
REM isn't a git checkout, rather than failing the build. git_version.h itself is gitignored -- generated, not
REM committed source.
setlocal
set "OUT=%~dp0src\git_version.h"
set "TMP=%~dp0src\git_version.h.tmp"
set "GITHASH=unknown"
git rev-parse --short=8 HEAD > "%TMP%" 2>nul
if exist "%TMP%" (
    set /p GITHASH=<"%TMP%"
    del "%TMP%" >nul 2>nul
)
if "%GITHASH%"=="" set "GITHASH=unknown"
> "%OUT%" echo // Generated at build time by gen_git_version.bat -- do not edit by hand, and do not rely on this
>> "%OUT%" echo // file's own committed contents (it is regenerated, and gitignored, on every build^).
>> "%OUT%" echo #pragma once
>> "%OUT%" echo #define MW3NCP_GIT_COMMIT_HASH "%GITHASH%"
endlocal
exit /b 0
