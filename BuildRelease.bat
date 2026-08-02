@echo off
setlocal enabledelayedexpansion
rem NativeWaterLightStabilizer release build and package script.

cd /d "%~dp0"

set "BUILD_DIR=build"
set "CONFIG=Release"
set "EXPECTED_GENERATOR=Visual Studio 18 2026"
set "AUTO_DEPLOY_DIR=C:\vanillaskyrim\mods\NativeWaterLightStabilizer"
set "AUTO_DEPLOY_DIR_SECONDARY=C:\ARCHON_\mods\NativeWaterLightStabilizer"

set "PACKAGE_DIR=dist\NativeWaterLightStabilizer"
set "PACKAGE_FILE="

if /I "%~1"=="clean" (
    echo [NWLS] Cleaning %BUILD_DIR% ...
    if exist "%BUILD_DIR%" rmdir /s /q "%BUILD_DIR%"
    shift
)

rem Prefer the CMake bundled with Visual Studio 18 2026.
set "CMAKE_EXE="
set "CMAKE_VERSION="
set "VS_INSTALL="
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"

if exist "!VSWHERE!" (
    for /f "usebackq delims=" %%i in (`"!VSWHERE!" -latest -products * -requires Microsoft.VisualStudio.Component.VC.CMake.Project -property installationPath 2^>nul`) do (
        if not defined VS_INSTALL set "VS_INSTALL=%%i"
    )

    rem Fall back to the latest Visual Studio installation.
    if not defined VS_INSTALL (
        for /f "usebackq delims=" %%i in (`"!VSWHERE!" -latest -products * -property installationPath 2^>nul`) do (
            if not defined VS_INSTALL set "VS_INSTALL=%%i"
        )
    )
)

if defined VS_INSTALL (
    set "VS_CMAKE=!VS_INSTALL!\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
    if exist "!VS_CMAKE!" set "CMAKE_EXE=!VS_CMAKE!"
)

rem Check standard Visual Studio 18 installation paths.
for %%e in (Community Professional Enterprise BuildTools) do (
    if not defined CMAKE_EXE (
        set "VS_CANDIDATE=%ProgramFiles%\Microsoft Visual Studio\18\%%e"
        if exist "!VS_CANDIDATE!\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" (
            set "VS_INSTALL=!VS_CANDIDATE!"
            set "CMAKE_EXE=!VS_CANDIDATE!\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
        )
    )
)

rem Last fallback: first CMake found in PATH.
if not defined CMAKE_EXE (
    for /f "delims=" %%i in ('where cmake 2^>nul') do (
        if not defined CMAKE_EXE set "CMAKE_EXE=%%i"
    )
)

if not defined CMAKE_EXE (
    echo [ERROR] CMake was not found in Visual Studio or PATH.
    exit /b 1
)

rem Read the quoted CMake path through a temporary file to avoid FOR /F parsing issues.
set "CMAKE_VERSION_FILE=%TEMP%\NWLS-cmake-version-!RANDOM!-!RANDOM!.txt"
"!CMAKE_EXE!" --version > "!CMAKE_VERSION_FILE!" 2>nul
if errorlevel 1 (
    if exist "!CMAKE_VERSION_FILE!" del /q "!CMAKE_VERSION_FILE!" >nul 2>nul
    echo [ERROR] Unable to run: !CMAKE_EXE!
    exit /b 1
)

for /f "usebackq tokens=1,2,3" %%a in ("!CMAKE_VERSION_FILE!") do (
    if /I "%%a %%b"=="cmake version" if not defined CMAKE_VERSION set "CMAKE_VERSION=%%c"
)
del /q "!CMAKE_VERSION_FILE!" >nul 2>nul

if not defined CMAKE_VERSION (
    echo [ERROR] Unable to determine the version of: !CMAKE_EXE!
    exit /b 1
)

set "CMAKE_MAJOR="
set "CMAKE_MINOR="
for /f "tokens=1,2 delims=." %%a in ("!CMAKE_VERSION!") do (
    set /a CMAKE_MAJOR=%%a
    set /a CMAKE_MINOR=%%b
)

set "CMAKE_TOO_OLD="
if !CMAKE_MAJOR! LSS 4 set "CMAKE_TOO_OLD=1"
if !CMAKE_MAJOR! EQU 4 if !CMAKE_MINOR! LSS 2 set "CMAKE_TOO_OLD=1"
if defined CMAKE_TOO_OLD (
    echo [ERROR] CMake !CMAKE_VERSION! is too old. CMake 4.2 or newer is required.
    echo          Selected executable: !CMAKE_EXE!
    echo          The Visual Studio 18 2026 generator is unavailable in older versions.
    exit /b 1
)

echo [NWLS] CMake = !CMAKE_EXE!
echo [NWLS] CMake version = !CMAKE_VERSION!

where git >nul 2>nul
if errorlevel 1 (
    echo [ERROR] Git was not found in PATH. It is required to populate extern\.
    exit /b 1
)

rem Initialize the Visual Studio x64 developer environment.
if defined VS_INSTALL (
    set "VSDEVCMD=!VS_INSTALL!\Common7\Tools\VsDevCmd.bat"
    if exist "!VSDEVCMD!" (
        call "!VSDEVCMD!" -no_logo -arch=x64 -host_arch=x64 >nul
        if errorlevel 1 (
            echo [ERROR] Failed to initialize the Visual Studio environment:
            echo          !VSDEVCMD!
            exit /b 1
        )
    )
)

rem Prefer the shared CommonLib vcpkg, then Visual Studio, then PATH.
set "SELECTED_VCPKG_ROOT="
if exist "C:\ClibDT\vcpkg\scripts\buildsystems\vcpkg.cmake" (
    set "SELECTED_VCPKG_ROOT=C:\ClibDT\vcpkg"
)
if not defined SELECTED_VCPKG_ROOT if defined VS_INSTALL (
    if exist "!VS_INSTALL!\VC\vcpkg\scripts\buildsystems\vcpkg.cmake" (
        set "SELECTED_VCPKG_ROOT=!VS_INSTALL!\VC\vcpkg"
    )
)

if not defined SELECTED_VCPKG_ROOT (
    set "VCPKG_EXE="
    for /f "delims=" %%i in ('where vcpkg 2^>nul') do if not defined VCPKG_EXE set "VCPKG_EXE=%%i"
    if defined VCPKG_EXE (
        for %%i in ("!VCPKG_EXE!") do set "SELECTED_VCPKG_ROOT=%%~dpi"
        set "SELECTED_VCPKG_ROOT=!SELECTED_VCPKG_ROOT:~0,-1!"
    )
)
if not defined SELECTED_VCPKG_ROOT (
    echo [ERROR] vcpkg was not found in C:\ClibDT, Visual Studio, or PATH.
    exit /b 1
)

set "VCPKG_ROOT=!SELECTED_VCPKG_ROOT!"
if defined VS_INSTALL set "VCPKG_VISUAL_STUDIO_PATH=!VS_INSTALL!"

set "TOOLCHAIN=!VCPKG_ROOT!\scripts\buildsystems\vcpkg.cmake"
if not exist "!TOOLCHAIN!" (
    echo [ERROR] vcpkg toolchain not found: !TOOLCHAIN!
    exit /b 1
)

set "TOOLCHAIN_CMAKE=!TOOLCHAIN:\=/!"
echo [NWLS] Visual Studio = !VS_INSTALL!
echo [NWLS] VCPKG_ROOT = !VCPKG_ROOT!
echo [NWLS] VCPKG_VISUAL_STUDIO_PATH = !VCPKG_VISUAL_STUDIO_PATH!

rem Recreate the build tree when generator, configuration, or toolchain changes.
if exist "%BUILD_DIR%\CMakeCache.txt" (
    set "CACHED_GENERATOR="
    set "CACHED_CONFIGS="
    set "CACHED_TOOLCHAIN="
    for /f "tokens=2 delims==" %%g in ('findstr /B /C:"CMAKE_GENERATOR:INTERNAL=" "%BUILD_DIR%\CMakeCache.txt" 2^>nul') do set "CACHED_GENERATOR=%%g"
    for /f "tokens=2 delims==" %%g in ('findstr /B /C:"CMAKE_CONFIGURATION_TYPES:STRING=" "%BUILD_DIR%\CMakeCache.txt" 2^>nul') do set "CACHED_CONFIGS=%%g"
    for /f "tokens=2 delims==" %%g in ('findstr /B /C:"CMAKE_TOOLCHAIN_FILE:" "%BUILD_DIR%\CMakeCache.txt" 2^>nul') do set "CACHED_TOOLCHAIN=%%g"
    if defined CACHED_TOOLCHAIN set "CACHED_TOOLCHAIN=!CACHED_TOOLCHAIN:\=/!"

    set "RECREATE_BUILD="
    if defined CACHED_GENERATOR if /I not "!CACHED_GENERATOR!"=="%EXPECTED_GENERATOR%" set "RECREATE_BUILD=1"
    if defined CACHED_CONFIGS if /I not "!CACHED_CONFIGS!"=="Release" set "RECREATE_BUILD=1"
    if defined CACHED_TOOLCHAIN if /I not "!CACHED_TOOLCHAIN!"=="!TOOLCHAIN_CMAKE!" set "RECREATE_BUILD=1"

    if defined RECREATE_BUILD (
        echo [NWLS] The cache does not match the current Release preset.
        echo [NWLS] Cached generator: !CACHED_GENERATOR!
        echo [NWLS] Cached configurations: !CACHED_CONFIGS!
        echo [NWLS] Cached toolchain: !CACHED_TOOLCHAIN!
        echo [NWLS] Required toolchain: !TOOLCHAIN_CMAKE!
        echo [NWLS] Recreating %BUILD_DIR%...
        rmdir /s /q "%BUILD_DIR%"
    )
)

rem Remove the legacy build\extern layout.
if exist "%BUILD_DIR%\extern" (
    echo [NWLS] Legacy %BUILD_DIR%\extern layout detected.
    echo [NWLS] Recreating %BUILD_DIR% with dependencies at the build root ...
    rmdir /s /q "%BUILD_DIR%"
)

rem Always configure; CMake remains incremental when nothing changed.
echo.
echo [NWLS] ===== CMAKE CONFIGURE =====
"!CMAKE_EXE!" --preset release
if errorlevel 1 (
    echo.
    echo [ERROR] CMake configuration failed.
    exit /b 1
)

rem Read the normalized project version from CMakeCache.txt.
set "PROJECT_VERSION="
for /f "tokens=2 delims==" %%v in ('findstr /B /C:"CMAKE_PROJECT_VERSION:" "%BUILD_DIR%\CMakeCache.txt" 2^>nul') do if not defined PROJECT_VERSION set "PROJECT_VERSION=%%v"
if not defined PROJECT_VERSION (
    echo [ERROR] Unable to read CMAKE_PROJECT_VERSION from %BUILD_DIR%\CMakeCache.txt.
    exit /b 1
)
set "PACKAGE_FILE=dist\NativeWaterLightStabilizer-!PROJECT_VERSION!.zip"
echo [NWLS] Project version = !PROJECT_VERSION!

echo.
echo [NWLS] ===== BUILD %CONFIG%  =====
"!CMAKE_EXE!" --build --preset release --parallel
if errorlevel 1 (
    echo.
    echo [ERROR] Build failed.
    exit /b 1
)

set "DLL=%BUILD_DIR%\Release\NativeWaterLightStabilizer.dll"
if not exist "!DLL!" set "DLL="
if not defined DLL (
    echo [ERROR] Build completed but NativeWaterLightStabilizer.dll was not found.
    exit /b 1
)

echo [NWLS] DLL: !DLL!
echo [NWLS] Mod automatically updated in: !AUTO_DEPLOY_DIR!
echo [NWLS] Mod automatically updated in: !AUTO_DEPLOY_DIR_SECONDARY!

if not exist "!PACKAGE_DIR!\SKSE\Plugins\NativeWaterLightStabilizer.dll" (
    echo [ERROR] Package staging DLL not found:
    echo          !PACKAGE_DIR!\SKSE\Plugins\NativeWaterLightStabilizer.dll
    exit /b 1
)
if not exist "!PACKAGE_DIR!\SKSE\Plugins\NativeWaterLightStabilizer.ini" (
    echo [ERROR] Package staging INI not found:
    echo          !PACKAGE_DIR!\SKSE\Plugins\NativeWaterLightStabilizer.ini
    exit /b 1
)

where powershell >nul 2>nul
if errorlevel 1 (
    echo [ERROR] PowerShell was not found; unable to create the ZIP package.
    exit /b 1
)

if exist "!PACKAGE_FILE!" del /q "!PACKAGE_FILE!"
echo.
echo [NWLS] ===== CREATE PACKAGE =====
powershell -NoProfile -ExecutionPolicy Bypass -Command ^
  "$ErrorActionPreference='Stop'; Compress-Archive -Path '!PACKAGE_DIR!\*' -DestinationPath '!PACKAGE_FILE!' -Force"
if errorlevel 1 (
    echo.
    echo [ERROR] ZIP package creation failed.
    exit /b 1
)

if not exist "!PACKAGE_FILE!" (
    echo [ERROR] PowerShell did not create the expected package: !PACKAGE_FILE!
    exit /b 1
)

echo [NWLS] Package: !PACKAGE_FILE!

if /I "%~1"=="deploy" (
    set "DEST=%~2"
    if "!DEST!"=="" (
        echo [ERROR] Deploy requires a path: BuildRelease.bat deploy "C:\mod"
        exit /b 1
    )

    echo [NWLS] Manual deployment to "!DEST!\SKSE\Plugins" ...
    if not exist "!DEST!\SKSE\Plugins" mkdir "!DEST!\SKSE\Plugins"
    copy /y "!DLL!" "!DEST!\SKSE\Plugins\" >nul
    if errorlevel 1 (
        echo [ERROR] DLL copy failed.
        exit /b 1
    )

    rem Preserve an existing INI.
    set "INI_SRC=%~dp0packed\SKSE\Plugins\NativeWaterLightStabilizer.ini"
    set "INI_DST=!DEST!\SKSE\Plugins\NativeWaterLightStabilizer.ini"
    if exist "!INI_DST!" (
        echo [NWLS] Existing INI preserved.
    ) else (
        if exist "!INI_SRC!" (
            copy /y "!INI_SRC!" "!INI_DST!" >nul
            if errorlevel 1 (
                echo [ERROR] INI copy failed.
                exit /b 1
            )
            echo [NWLS] Default INI copied.
        ) else (
            echo [WARNING] Source INI not found: !INI_SRC!
        )
    )
    echo [NWLS] Manual deployment completed.
)

echo.
echo [NWLS] DONE.
endlocal
