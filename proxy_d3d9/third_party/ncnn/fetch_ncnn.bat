@echo off
REM fetch_ncnn.bat -- downloads and extracts the real, official, prebuilt
REM Tencent/ncnn Windows VS2022 x64 static SDK (Vulkan-enabled) into this
REM directory's lib/ and bin/ subfolders. These are gitignored (size, not
REM license -- see .gitignore's own comment) and must be fetched locally
REM before building anything that links against ncnn.
REM
REM Pinned version: 20260526 (the latest ncnn release tag as of the
REM texture-upscale-cache feature's groundwork session, re_notes/x64_migration/
REM texture_upscale_cache_research.md). Bump NCNN_TAG below and re-run this
REM script to update; do not silently track "latest" so builds stay
REM reproducible.

setlocal
set NCNN_TAG=20260526
set NCNN_ZIP=ncnn-%NCNN_TAG%-windows-vs2022.zip
set NCNN_URL=https://github.com/Tencent/ncnn/releases/download/%NCNN_TAG%/%NCNN_ZIP%
set SCRIPT_DIR=%~dp0
set TMP_ZIP=%TEMP%\%NCNN_ZIP%
set TMP_EXTRACT=%TEMP%\ncnn_fetch_%NCNN_TAG%

echo Downloading %NCNN_URL% ...
curl -sL -o "%TMP_ZIP%" "%NCNN_URL%"
if errorlevel 1 (
    echo FAILED to download ncnn release zip.
    exit /b 1
)

echo Extracting ...
powershell -NoProfile -Command "Expand-Archive -Path '%TMP_ZIP%' -DestinationPath '%TMP_EXTRACT%' -Force"
if errorlevel 1 (
    echo FAILED to extract ncnn release zip.
    exit /b 1
)

set SRC=%TMP_EXTRACT%\ncnn-%NCNN_TAG%-windows-vs2022\x64

if not exist "%SCRIPT_DIR%lib" mkdir "%SCRIPT_DIR%lib"
if not exist "%SCRIPT_DIR%bin" mkdir "%SCRIPT_DIR%bin"

copy /Y "%SRC%\lib\*.lib" "%SCRIPT_DIR%lib\" >nul
if exist "%SRC%\bin\*.dll" copy /Y "%SRC%\bin\*.dll" "%SCRIPT_DIR%bin\" >nul

echo Done. ncnn %NCNN_TAG% (x64, Vulkan-enabled, static) is ready in
echo   %SCRIPT_DIR%lib\
echo Headers are already committed under %SCRIPT_DIR%include\ncnn\ and
echo do not need fetching.

endlocal
