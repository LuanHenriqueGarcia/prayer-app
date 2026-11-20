@echo off
rem Compila o PrayerApp nativo. Precisa do Visual Studio 2022 (ou Build Tools) com "Desenvolvimento para desktop com C++".
rem   build.cmd           Release em build\PrayerApp.exe        (o que vai para as lojas)
rem   build.cmd debug     Debug em build-debug\PrayerApp.exe    (para depurar no VS Code)
rem   build.cmd clean     apaga build\ e build-debug\
setlocal
cd /d "%~dp0"

if /i "%~1"=="clean" (
  if exist build rmdir /s /q build
  if exist build-debug rmdir /s /q build-debug
  echo Limpo.
  exit /b 0
)

set CFG=Release
set OUT=build
if /i "%~1"=="debug" (
  set CFG=Debug
  set OUT=build-debug
)

rem Ja dentro de um "Developer Command Prompt"? Entao o compilador ja esta no PATH.
if defined VSCMD_VER goto compile
set "PATH=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer;%PATH%"
set VSDIR=
for /f "usebackq tokens=*" %%i in (`vswhere.exe -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSDIR=%%i"
if not defined VSDIR (
  echo ERRO: Visual Studio 2022 / Build Tools com C++ nao encontrado. Veja COMPILAR.md, passo 1.
  exit /b 1
)
call "%VSDIR%\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1

:compile
where cmake >nul 2>nul || (echo ERRO: cmake nao encontrado. Instale o componente "Ferramentas do CMake do C++ para Windows". & exit /b 1)
where ninja >nul 2>nul || (echo ERRO: ninja nao encontrado. Instale o componente "Ferramentas do CMake do C++ para Windows". & exit /b 1)
cmake -S . -B %OUT% -G Ninja -DCMAKE_BUILD_TYPE=%CFG% || exit /b 1
cmake --build %OUT% || exit /b 1
echo.
echo OK (%CFG%): %~dp0%OUT%\PrayerApp.exe
