@echo off
rem build.bat - the vCPC on Windows with Visual C++ (cl): build\vcpc.exe
rem Run it from src (or anywhere: it goes to its own folder). A console program, so no
rem /SUBSYSTEM:WINDOWS - its screen and keyboard are the console it runs in
cd /d "%~dp0"
where cl >nul 2>nul || call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
if not exist ..\build mkdir ..\build
cl /nologo /O2 /MT /Iz80 vcpc.c z80\z80.c /Fo..\build\ /Fe..\build\vcpc.exe /link ws2_32.lib
if errorlevel 1 exit /b 1
del ..\build\*.obj 2>nul
echo built ..\build\vcpc.exe
