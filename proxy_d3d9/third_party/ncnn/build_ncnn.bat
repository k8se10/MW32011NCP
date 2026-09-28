@echo off
REM build_ncnn.bat -- builds ncnn (Vulkan-enabled, STATIC CRT) from source and
REM copies the resulting libs into this directory's lib\ folder.
REM
REM Replaces an earlier version of this script (fetch_ncnn.bat) that
REM downloaded the official prebuilt SDK zip -- that build uses the DYNAMIC
REM CRT (/MD), which mismatches this project's own proxy_d3d9.vcxproj
REM (RuntimeLibrary=MultiThreaded, i.e. /MT) and fails to link with a real
REM LNK2038 "RuntimeLibrary mismatch" error. Direct decision (2026-09-28,
REM via AskUserQuestion): rebuild ncnn from source with its own real
REM NCNN_BUILD_WITH_STATIC_CRT option instead of switching the mod itself to
REM /MD (which would add a new player-visible MSVC-redistributable
REM dependency d3d9.dll doesn't have today) or shipping ncnn as a separate
REM DLL. See re_notes/x64_migration/texture_upscale_cache_research.md's own
REM "Rebuilding ncnn from source with /MT" round for the full record.
REM
REM Requires: git, cmake (4.x confirmed working), a Visual Studio C++
REM toolchain (this project's own documented one), and the Vulkan SDK
REM installed (VULKAN_SDK env var set -- ncnn's own CMake finds it via that,
REM no explicit Vulkan_INCLUDE_DIR/Vulkan_LIBRARY hint needed; ncnn also
REM implements its own in-house Vulkan function loader rather than statically
REM linking vulkan-1.lib, so this build does not itself produce or need one).
REM
REM Pinned version: 20260526 (same tag this project's own texture-upscale-
REM cache research already verified). Bump NCNN_TAG below to update.
REM
REM IMPORTANT: build in a SHORT path, not deep inside a user profile/temp
REM directory -- a real Windows MAX_PATH/MSBuild FileTracker failure was hit
REM and confirmed during this project's own first attempt at a deeply nested
REM scratch path. This script builds under %NCNN_BUILD_ROOT% (default
REM C:\ncnn_build) for exactly that reason -- do not redirect it into a long
REM path.

setlocal
set NCNN_TAG=20260526
if "%NCNN_BUILD_ROOT%"=="" set NCNN_BUILD_ROOT=C:\ncnn_build
set SCRIPT_DIR=%~dp0

echo Building ncnn %NCNN_TAG% (Vulkan, static CRT) under %NCNN_BUILD_ROOT% ...

if not exist "%NCNN_BUILD_ROOT%" (
    git clone --depth 1 --branch %NCNN_TAG% https://github.com/Tencent/ncnn.git "%NCNN_BUILD_ROOT%"
    if errorlevel 1 (
        echo FAILED to clone ncnn.
        exit /b 1
    )
)

pushd "%NCNN_BUILD_ROOT%"
git submodule update --init --depth 1 glslang
if errorlevel 1 (
    echo FAILED to fetch the glslang submodule.
    popd
    exit /b 1
)

if not exist build mkdir build
cd build

cmake -G "Visual Studio 18 2026" -A x64 ^
  -DNCNN_VULKAN=ON ^
  -DNCNN_BUILD_WITH_STATIC_CRT=ON ^
  -DNCNN_BUILD_TOOLS=OFF ^
  -DNCNN_BUILD_EXAMPLES=OFF ^
  -DNCNN_BUILD_TESTS=OFF ^
  -DNCNN_SHARED_LIB=OFF ^
  ..
if errorlevel 1 (
    echo FAILED to configure ncnn.
    popd
    exit /b 1
)

cmake --build . --config Release
if errorlevel 1 (
    echo FAILED to build ncnn.
    popd
    exit /b 1
)

popd

if not exist "%SCRIPT_DIR%lib" mkdir "%SCRIPT_DIR%lib"

copy /Y "%NCNN_BUILD_ROOT%\build\src\Release\ncnn.lib" "%SCRIPT_DIR%lib\" >nul
copy /Y "%NCNN_BUILD_ROOT%\build\glslang\glslang\Release\glslang.lib" "%SCRIPT_DIR%lib\" >nul
copy /Y "%NCNN_BUILD_ROOT%\build\glslang\glslang\Release\glslang-default-resource-limits.lib" "%SCRIPT_DIR%lib\" >nul
copy /Y "%NCNN_BUILD_ROOT%\build\glslang\glslang\Release\MachineIndependent.lib" "%SCRIPT_DIR%lib\" >nul
copy /Y "%NCNN_BUILD_ROOT%\build\glslang\glslang\Release\GenericCodeGen.lib" "%SCRIPT_DIR%lib\" >nul
copy /Y "%NCNN_BUILD_ROOT%\build\glslang\glslang\OSDependent\Windows\Release\OSDependent.lib" "%SCRIPT_DIR%lib\" >nul
copy /Y "%NCNN_BUILD_ROOT%\build\glslang\SPIRV\Release\SPIRV.lib" "%SCRIPT_DIR%lib\" >nul

REM Copy the real, CMake-generated headers from THIS build to match the libs
REM exactly (these 4 headers embed version/config info that can genuinely
REM differ build-to-build -- confirmed during this project's own first build,
REM NCNN_VERSION_STRING differed from the prebuilt SDK's own copy).
copy /Y "%NCNN_BUILD_ROOT%\build\src\layer_shader_type_enum.h" "%SCRIPT_DIR%include\ncnn\" >nul
copy /Y "%NCNN_BUILD_ROOT%\build\src\layer_type_enum.h" "%SCRIPT_DIR%include\ncnn\" >nul
copy /Y "%NCNN_BUILD_ROOT%\build\src\ncnn_export.h" "%SCRIPT_DIR%include\ncnn\" >nul
copy /Y "%NCNN_BUILD_ROOT%\build\src\platform.h" "%SCRIPT_DIR%include\ncnn\" >nul

echo Done. ncnn %NCNN_TAG% (x64, Vulkan-enabled, static CRT) is ready in
echo   %SCRIPT_DIR%lib\

endlocal
