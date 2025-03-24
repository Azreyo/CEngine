@echo off
cls

REM Kill any running instances of CEngine.exe
taskkill /F /IM CEngine.exe 2>nul
if errorlevel 1 (
    echo No running instances of CEngine.exe found.
) else (
    echo Terminated running CEngine.exe
    REM Add a small delay to ensure the process is fully terminated
    timeout /t 1 /nobreak >nul
)

g++ -o build\CEngine.exe ^
main.cpp ^
settings.cpp ^
settings_ui.cpp ^
logging.cpp ^
log_console.cpp ^
debug_info.cpp ^
memory_protection.cpp ^
advanced_scanning.cpp ^
include/imgui.cpp ^
include/imgui_demo.cpp ^
include/imgui_draw.cpp ^
include/imgui_tables.cpp ^
include/imgui_widgets.cpp ^
include/imgui_impl_win32.cpp ^
include/imgui_impl_dx11.cpp ^
-I. ^
-municode ^
-ld3d11 ^
-ldxgi ^
-luser32 ^
-lgdi32 ^
-lshell32 ^
-ldwmapi ^
-ld3dcompiler ^
-lole32 ^
-lpsapi ^
-D_UNICODE ^
-DUNICODE ^
-DWIN32_LEAN_AND_MEAN ^
-DWINVER=0x0601 ^
-D_WIN32_WINNT=0x0601 ^
-mwindows ^
-O2 ^
-std=c++11