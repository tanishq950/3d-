@echo off
setlocal

set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
set "VSROOT="
set "VSINFO=%TEMP%\RoomStudio_vswhere_%RANDOM%.txt"
if not exist "%VSWHERE%" (
    echo Visual Studio 2022 Build Tools are required. Install the C++ build tools and Windows SDK.
    exit /b 1
)

"%VSWHERE%" -latest -products * -property installationPath > "%VSINFO%"
if errorlevel 1 (
    del "%VSINFO%" 2>nul
    echo Could not locate a Visual Studio installation.
    exit /b 1
)
for /f "usebackq delims=" %%I in ("%VSINFO%") do set "VSROOT=%%I"
del "%VSINFO%" 2>nul
if not defined VSROOT (
    echo Could not find a Visual Studio installation with C++ build tools.
    exit /b 1
)

set "PATH=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer;%PATH%"
call "%VSROOT%\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b 1

pushd "%~dp0"
if errorlevel 1 exit /b 1
if not exist build mkdir build
cl /nologo /W4 /O2 /utf-8 main.c /Fo:build\ /Fe:RoomStudio.exe /link /SUBSYSTEM:WINDOWS user32.lib gdi32.lib comdlg32.lib
if errorlevel 1 goto :build_failed

echo Build successful: RoomStudio.exe
popd
exit /b 0

:build_failed
popd
exit /b 1
