@echo off
rem The portable tests natively on Windows, through MSYS2's bash and MinGW-w64's g++.
rem (tools/wine-tests.sh runs the same assertions through Wine from Linux.)
setlocal
if not defined MSYS2_ROOT set "MSYS2_ROOT=C:\msys64"
if not exist "%MSYS2_ROOT%\usr\bin\bash.exe" (
  echo native-tests: MSYS2 not found at %MSYS2_ROOT% 1>&2
  echo Set MSYS2_ROOT to its folder, for example: set MSYS2_ROOT=D:\msys64 1>&2
  exit /b 1
)
set "PATH=%MSYS2_ROOT%\mingw64\bin;%MSYS2_ROOT%\usr\bin;%PATH%"
pushd "%~dp0.."
bash tools/test.sh
set "code=%ERRORLEVEL%"
popd
exit /b %code%
