@echo off
rem SACoop : compile seulement dinput8.dll (Win32, /MT) avec le Visual Studio installe, sans le lanceur.
rem Cherche vcvars32.bat dans les Visual Studio 2019 a 2026 (Community / Professional / Enterprise / BuildTools).
rem Sortie : build\dinput8.dll   Journal complet : build\build.log
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
  echo vcvars32.bat introuvable : installez la charge de travail "Developpement Desktop en C++" de Visual Studio.
  exit /b 1
)
echo Visual Studio : %VCVARS%
call "%VCVARS%" >nul 2>&1
if errorlevel 1 (
  echo vcvars32.bat a echoue.
  exit /b 1
)
echo Compilation en cours (1 a 2 minutes)...
cl /nologo /O2 /MT /W3 /EHsc /utf-8 /D_CRT_SECURE_NO_WARNINGS /DWIN32_LEAN_AND_MEAN /Fobuild\ src\*.cpp launcher\model3d.cpp ^
   /LD /Febuild\dinput8.dll /link /MAP:build\dinput8.map /DEF:src\dinput8.def user32.lib kernel32.lib advapi32.lib ws2_32.lib winmm.lib shell32.lib gdi32.lib > build\build.log 2>&1
if errorlevel 1 (
  echo.
  echo *** ECHEC DE LA COMPILATION. Erreurs :
  findstr /C:" error " build\build.log
  echo.
  echo Journal complet : %~dp0build\build.log
  exit /b 1
)
echo OK  %~dp0build\dinput8.dll
for %%F in (build\dinput8.dll) do echo     %%~tF  %%~zF octets
pause
