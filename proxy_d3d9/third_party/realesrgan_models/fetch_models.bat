@echo off
REM fetch_models.bat -- downloads the real, official Real-ESRGAN ncnn model
REM files (realesrgan-x4plus.bin/.param) for the texture-upscale-cache
REM feature. Gitignored, not committed (33MB, same size-driven treatment as
REM every other large vendored binary in this project this session).
REM
REM Source: xinntao/Real-ESRGAN's own v0.2.5.0 release (2022-04-24) --
REM confirmed to bundle real, separate .param/.bin files, unlike the
REM xinntao/Real-ESRGAN-ncnn-vulkan fork's own releases, which embed models
REM directly inside the CLI .exe and don't ship them as separate files.
REM License: BSD-3-Clause (xinntao/Real-ESRGAN's own LICENSE, verified
REM 2026-09-28 -- see texture_upscale_cache_research.md).

setlocal
set MODEL_URL=https://github.com/xinntao/Real-ESRGAN/releases/download/v0.2.5.0/realesrgan-ncnn-vulkan-20220424-windows.zip
set SCRIPT_DIR=%~dp0
set TMP_ZIP=%TEMP%\realesrgan-models-fetch.zip
set TMP_EXTRACT=%TEMP%\realesrgan-models-fetch

echo Downloading %MODEL_URL% ...
curl -sL -o "%TMP_ZIP%" "%MODEL_URL%"
if errorlevel 1 (
    echo FAILED to download.
    exit /b 1
)

echo Extracting ...
powershell -NoProfile -Command "Expand-Archive -Path '%TMP_ZIP%' -DestinationPath '%TMP_EXTRACT%' -Force"
if errorlevel 1 (
    echo FAILED to extract.
    exit /b 1
)

copy /Y "%TMP_EXTRACT%\models\realesrgan-x4plus.bin" "%SCRIPT_DIR%" >nul
copy /Y "%TMP_EXTRACT%\models\realesrgan-x4plus.param" "%SCRIPT_DIR%" >nul

echo Done. realesrgan-x4plus.bin/.param are ready in %SCRIPT_DIR%

endlocal
