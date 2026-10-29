@echo off
rem Build with MSVC. Run from "x64 Native Tools Command Prompt for VS".
setlocal
if not exist build\obj mkdir build\obj
if not exist build\Cue\fonts mkdir build\Cue\fonts
rc /nologo /I res /fo build\obj\cue.res res\cue.rc || exit /b 1
cl /nologo /std:c++20 /O2 /GL /Gy /Gw /EHsc /utf-8 /W4 /wd4100 /DUNICODE /D_UNICODE /DWIN32_LEAN_AND_MEAN /D_WIN32_WINNT=0x0A00 /DNOMINMAX ^
  /Fobuild\obj\ /Febuild\Cue\Cue.exe ^
  src\core\*.cpp src\ui\*.cpp src\win\*.cpp build\obj\cue.res ^
  /link /LTCG /OPT:REF /OPT:ICF /SUBSYSTEM:WINDOWS /MANIFEST:NO dwmapi.lib ole32.lib shell32.lib uuid.lib shcore.lib user32.lib gdi32.lib advapi32.lib winmm.lib windowscodecs.lib winhttp.lib bcrypt.lib || exit /b 1
copy /y assets\fonts\*.ttf build\Cue\fonts\ >nul
if not exist build\Cue\licenses mkdir build\Cue\licenses
copy /y assets\fonts\*.txt build\Cue\licenses\ >nul
copy /y docs\LICENSES.md build\Cue\ >nul
copy /y LICENSE build\Cue\ >nul
copy /y COPYRIGHT.md build\Cue\ >nul
xcopy /y /e /i third_party build\Cue\third_party >nul
if not exist build\Cue\libmpv-2.dll echo Falta libmpv-2.dll em build\Cue. Rode tools\fetch-libmpv.ps1
echo ok: build\Cue\Cue.exe
