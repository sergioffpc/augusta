@echo off
rem Runs the given command inside the Visual Studio Build Tools x64 developer
rem environment (cl.exe's INCLUDE/LIB/PATH), so the Ninja generator works from a
rem plain terminal. Usage: scripts\vcenv.cmd cmake --build --preset windows
rem The Makefile, the pre-push hook and the pack bootstrap use this on Windows;
rem it's also handy on its own.
setlocal

set "vswhere=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
set "vsroot="
if exist "%vswhere%" for /f "usebackq delims=" %%i in (`"%vswhere%" -latest -products * -property installationPath`) do set "vsroot=%%i"
if not defined vsroot (
  echo Visual Studio Build Tools not found - run scripts/bootstrap.sh first. 1>&2
  exit /b 1
)

rem vcvars64.bat changes this cmd.exe's environment, which the command inherits.
call "%vsroot%\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1
%*
exit /b %ERRORLEVEL%
