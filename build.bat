@echo off
REM Build script for Virtual USB project
REM Requires: Visual Studio with WDK, CMake

echo ============================================
echo Virtual USB Project Build Script
echo ============================================
echo.

REM Check for CMake
where cmake >nul 2>&1
if %ERRORLEVEL% NEQ 0 (
    echo ERROR: CMake not found in PATH
    exit /b 1
)

REM Build user-mode components
echo Building user-mode components...
echo.

if not exist build mkdir build
cd build

cmake .. -G "Visual Studio 17 2022" -A x64
if %ERRORLEVEL% NEQ 0 (
    echo ERROR: CMake configuration failed
    cd ..
    exit /b 1
)

cmake --build . --config Release
if %ERRORLEVEL% NEQ 0 (
    echo ERROR: Build failed
    cd ..
    exit /b 1
)

cd ..

echo.
echo ============================================
echo Build complete!
echo.
echo User-mode binaries are in build\Release\
echo.
echo To build the kernel driver:
echo 1. Open driver\vusb.sln in Visual Studio with WDK
echo 2. Build for Release x64
echo.
echo To install the driver (requires admin):
echo   pnputil /add-driver driver\vusb.inf /install
echo ============================================
