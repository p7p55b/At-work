@echo off
setlocal

set "VS_PATH="
for /f "usebackq tokens=*" %%i in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -property installationPath`) do (
    set "VS_PATH=%%i"
)

if "%VS_PATH%"=="" (
    echo [ERREUR] Visual Studio introuvable.
    exit /b 1
)

call "%VS_PATH%\VC\Auxiliary\Build\vcvars64.bat"

if not exist "build" mkdir "build"

cl.exe /std:c++17 /EHsc /O2 /Zi /FS /W4 /DUNICODE /D_UNICODE /DNOMINMAX /DWIN32_LEAN_AND_MEAN ^
    src\main.cpp src\gui.cpp src\discord_ipc.cpp src\calendar_sync.cpp src\config.cpp ^
    /Fo:build\ /Fd:build\ /Fe:build\At-Work.exe ^
    /link /SUBSYSTEM:WINDOWS /DEBUG /MAP:build\At-Work.map winhttp.lib comctl32.lib user32.lib gdi32.lib shell32.lib advapi32.lib gdiplus.lib ole32.lib

if %ERRORLEVEL% equ 0 (
    echo [SUCCES] Compilation terminee : build\At-Work.exe
) else (
    echo [ERREUR] Echec de la compilation.
)

