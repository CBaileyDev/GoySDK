@echo off
setlocal enabledelayedexpansion
echo =========================================
echo GoySDK Internal Bot - Build Script
echo =========================================

REM ---- Locate MSVC Automatically ----
for /f "usebackq tokens=*" %%i in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do (
  set InstallDir=%%i
)
if exist "!InstallDir!\VC\Auxiliary\Build\vcvars64.bat" (
  call "!InstallDir!\VC\Auxiliary\Build\vcvars64.bat"
) else (
  echo [WARNING] Could not find vcvars64.bat
)

REM ---- Configuration ----
REM   LIBTORCH_DIR = extracted LibTorch root (must contain lib\torch.lib)
REM     Default: CUDA build for in-game CPU/GPU inference toggle (NVIDIA).
REM     CPU-only dev:  set LIBTORCH_DIR=C:\libtorch-cpu  before running this script.
REM   VIGEM_DIR      = ViGEmClient (repo ships under deps\)
REM
REM   SETUP (new machine):
REM     1. Download libtorch from https://pytorch.org/get-started/locally/
REM        (select Stable, Windows, LibTorch, C++/Java, CUDA 12.1)
REM     2. Extract to C:\libtorch-cu121  (or C:\libtorch-cpu for CPU-only)
REM     3. Install Visual Studio 2022 with "Desktop development with C++" workload
REM     4. Run this script from a normal cmd prompt (vcvars is auto-detected)

if not defined LIBTORCH_DIR (
    if exist "C:\libtorch-cu121\lib\torch.lib" (
        set "LIBTORCH_DIR=C:\libtorch-cu121"
    ) else if exist "C:\libtorch-cpu\lib\torch.lib" (
        set "LIBTORCH_DIR=C:\libtorch-cpu"
    ) else (
        set "LIBTORCH_DIR=C:\libtorch-cu121"
    )
)
if not defined VIGEM_DIR (
    set VIGEM_DIR=%CD%\deps\ViGEmClient
)

if not exist "%LIBTORCH_DIR%\lib\torch.lib" (
    echo [ERROR] LibTorch not found at "%LIBTORCH_DIR%"
    echo Extract official libtorch-win-shared-with-deps matching that CUDA major to this folder,
    echo or run:  set LIBTORCH_DIR=C:\libtorch-cpu  for a CPU-only LibTorch ^(GPU menu stays off at runtime^).
    exit /b 1
)

echo.
echo LibTorch: %LIBTORCH_DIR%
echo ViGEm:    %VIGEM_DIR%
echo.

REM ---- Build ----
if exist build rmdir /s /q build
mkdir build
cd build

cmake -G Ninja ^
    -DCMAKE_BUILD_TYPE=Release ^
    -DCMAKE_PREFIX_PATH="%LIBTORCH_DIR%" ^
    -DVIGEM_DIR="%VIGEM_DIR%" ^
    ..

if %ERRORLEVEL% neq 0 (
    echo [ERROR] CMake configure failed.
    exit /b %ERRORLEVEL%
)

cmake --build . --config Release
if %ERRORLEVEL% neq 0 (
    echo [ERROR] Build failed.
    exit /b %ERRORLEVEL%
)

echo.
echo =========================================
echo BUILD SUCCESSFUL
echo =========================================
echo Output: build\GoySDK.dll
echo.
echo Deployment:
echo   1. Package-Payload.ps1 zips build\*.dll (+ models) into GoyLoader\Resources\payload.zip
echo   2. Use CUDA LibTorch for in-game GPU inference toggle; ship all DLLs from build\
echo   3. Install ViGEmBus (optional) and NVIDIA driver (optional, for CUDA policy)
echo   4. Inject into RocketLeague.exe in a PRIVATE MATCH — Press HOME to toggle bot
echo =========================================
cd ..
