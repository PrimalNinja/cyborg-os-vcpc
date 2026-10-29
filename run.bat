@echo off
rem start.bat - a vCPC node in this console: CyborgOS's set from the cyborg-os folder beside this one
cd /d "%~dp0"
set COS=%~dp0..\cyborg-os\build\next\CYBORGOS
if not "%1"=="" set COS=%~1
if not exist "%COS%\KERNEL.CYB" echo No KERNEL.CYB in %COS% - start.bat [CYBORGOS folder] & exit /b 1
if not exist build\vcpc.exe call src\build.bat || exit /b 1
build\vcpc.exe --sys "%COS%" --sys "%COS%\C" --data mynode --tty
