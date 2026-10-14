@echo off
rem SACoop: builds only the launcher, SACoop.exe (Win32, /MT), with the Visual Studio installed here.
rem Looks for vcvars32.bat in Visual Studio 2019 to 2026 (Community / Professional / Enterprise / BuildTools).
rem Output: build\SACoop.exe   Full log: build\build-exe.log
rem The launcher is "Zebra's forked repo": it updates from github.com/zebra032-cyber/SACoop (see launcher\launcher.cpp).
setlocal EnableExtensions
cd /d "%~dp0"
if not exist build mkdir build
set "PF64=%ProgramFiles%"
set "PF86=%ProgramFiles(x86)%"
set "VCVARS="
for %%V in (18 17 2026 2022 2019) do (
  for %%E in (Community Professional Enterprise BuildTools Preview) do (
    if not defined VCVARS if exist "%PF64%\Microsoft Visual Studio\%%V\%%E\VC\Auxiliary\Build\vcvars32.bat" set "VCVARS=%PF64%\Microsoft Visual Studio\%%V\%%E\VC\Auxiliary\Build\vcvars32.bat"
    if not defined VCVARS if exist "%PF86%\Microsoft Visual Studio\%%V\%%E\VC\Auxiliary\Build\vcvars32.bat" set "VCVARS=%PF86%\Microsoft Visual Studio\%%V\%%E\VC\Auxiliary\Build\vcvars32.bat"
  )
)
if not defined VCVARS (
  echo vcvars32.bat not found: install the "Desktop development with C++" workload of Visual Studio.
  exit /b 1
)
echo Visual Studio: %VCVARS%
call "%VCVARS%" >nul 2>&1
if errorlevel 1 (
  echo vcvars32.bat failed.
  exit /b 1
)
echo Building the launcher...
rc /nologo /fo build\launcher.res launcher\launcher.rc > build\build-exe.log 2>&1
if errorlevel 1 goto :failed
cl /nologo /O2 /MT /W3 /EHsc /utf-8 /DUNICODE /D_UNICODE /D_CRT_SECURE_NO_WARNINGS /Fobuild\ launcher\launcher.cpp launcher\model3d.cpp build\launcher.res ^
   /Febuild\SACoop.exe /link /SUBSYSTEM:WINDOWS user32.lib gdi32.lib gdiplus.lib winhttp.lib comdlg32.lib shell32.lib ole32.lib advapi32.lib ws2_32.lib winmm.lib >> build\build-exe.log 2>&1
if errorlevel 1 goto :failed
echo OK  %~dp0build\SACoop.exe
for %%F in (build\SACoop.exe) do echo     %%~tF  %%~zF bytes
exit /b 0
:failed
echo.
echo *** BUILD FAILED. Errors:
findstr /C:" error " build\build-exe.log
echo.
echo Full log: %~dp0build\build-exe.log
exit /b 1
pause
