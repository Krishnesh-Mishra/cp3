@echo off
rem ============================================================================
rem cp3.cmd - the user-facing `cp3` command, shipped as <install>\bin\cp3.cmd.
rem
rem A thin shim around cp3c.exe: the compiler keeps its name, the terminal
rem command is `cp3` (PKG-CONTRACTS section 1). Commands owned here and never
rem forwarded:
rem   cp3 init [target] [--force]      scaffold a project (package.json,
rem                                    src/main.cp3, README.md)
rem   cp3 install vs-code-plugin       install the bundled .vsix via `code`
rem   cp3 doctor                       print environment status
rem Everything else - including `cp3 lsp`, `cp3 run`, `cp3 build`, `cp3 init`
rem arguments we do not own - is forwarded verbatim to cp3c.exe with the exit
rem code passed through.
rem
rem CP3_ROOT is exported for the child process so cp3c.exe finds the shipped
rem include/, src/ and third_party/ trees even when this shim is reached
rem through PATH while the compiler lives in a different directory (the same
rem variable the compiler consults first; see src/cli/src/toolchain/util.cpp).
rem ============================================================================
setlocal EnableExtensions DisableDelayedExpansion
set "CP3_BIN=%~dp0"
for %%I in ("%~dp0..") do set "CP3_ROOT=%%~fI"
set "CP3C=%CP3_BIN%cp3c.exe"
set "PF86=%ProgramFiles(x86)%"

if "%~1"=="" goto forward
if /I "%~1"=="doctor" goto doctor
if /I "%~1"=="init" goto init
if /I "%~1"=="install" if /I "%~2"=="vs-code-plugin" goto plugin

:forward
rem `cp3 lsp ...` lands here too: cp3c owns the LSP subcommand.
"%CP3C%" %*
exit /b %errorlevel%

rem ----------------------------------------------------------------------------
rem cp3 init [target] [--force]
rem
rem Writes the manifest the compiler actually consumes (package.json with
rem "main": "src/main.cp3"), a hello-world entry point and a README. A target
rem that exists and is not empty is refused unless --force is given, which
rem overwrites the three scaffold files (it never deletes anything else).
rem Exit codes: 0 written, 1 write failure, 2 refused or bad usage.
rem ----------------------------------------------------------------------------
:init
set "TARGET="
set "FORCE="
shift
:init_loop
if "%~1"=="" goto init_check
if /I "%~1"=="--force" goto init_force
if /I "%~1"=="-f" goto init_force
if defined TARGET goto init_usage
set "TARGET=%~1"
shift
goto init_loop
:init_force
set "FORCE=1"
shift
goto init_loop
:init_usage
echo cp3: usage: cp3 init [target] [--force]
exit /b 2
:init_check
if not defined TARGET set "TARGET=%CD%"
for %%I in ("%TARGET%") do set "TARGET=%%~fI"
set "NAME="
for %%I in ("%TARGET%") do set "NAME=%%~nxI"
if not defined NAME set "NAME=cp3-app"
set "NONEMPTY="
for /f "delims=" %%F in ('dir /b /a "%TARGET%" 2^>nul') do set "NONEMPTY=1"
if not defined NONEMPTY goto init_write
if defined FORCE goto init_write
echo cp3: target "%TARGET%" is not empty; refusing to scaffold over it.
echo       Re-run with --force to scaffold into it anyway.
exit /b 2
:init_write
if not exist "%TARGET%\src" md "%TARGET%\src"
if errorlevel 1 goto init_failed
rem Remove the three scaffold files first: --force re-runs must overwrite, and
rem `>>` appends would otherwise duplicate the entry point on the second run.
if exist "%TARGET%\package.json" del /q "%TARGET%\package.json"
if exist "%TARGET%\src\main.cp3" del /q "%TARGET%\src\main.cp3"
if exist "%TARGET%\README.md" del /q "%TARGET%\README.md"
>"%TARGET%\package.json" echo {
>>"%TARGET%\package.json" echo   "name": "%NAME%",
>>"%TARGET%\package.json" echo   "version": "0.1.0",
>>"%TARGET%\package.json" echo   "type": "application",
>>"%TARGET%\package.json" echo   "main": "src/main.cp3"
>>"%TARGET%\package.json" echo }
>"%TARGET%\src\main.cp3" echo import console from "std/console";
>>"%TARGET%\src\main.cp3" echo.
>>"%TARGET%\src\main.cp3" echo console.log("Hello from %NAME%!");
>"%TARGET%\README.md" echo # %NAME%
>>"%TARGET%\README.md" echo.
>>"%TARGET%\README.md" echo A cp3 project scaffolded by `cp3 init`.
>>"%TARGET%\README.md" echo.
>>"%TARGET%\README.md" echo Build and run it with:
>>"%TARGET%\README.md" echo.
>>"%TARGET%\README.md" echo     cp3 run src/main.cp3
>>"%TARGET%\README.md" echo.
>>"%TARGET%\README.md" echo `cp3 build` (bare) uses package.json main and writes .cp3/out/main.exe.
>>"%TARGET%\README.md" echo `cp3 doctor` checks the toolchain and the installation.
if not exist "%TARGET%\src\main.cp3" goto init_failed
if not exist "%TARGET%\package.json" goto init_failed
echo cp3: initialized project "%NAME%" in "%TARGET%"
echo       Run it with:  cd "%TARGET%" ^&^& cp3 run src/main.cp3
exit /b 0
:init_failed
echo cp3: could not write the scaffold under "%TARGET%".
exit /b 1

rem ----------------------------------------------------------------------------
rem cp3 install vs-code-plugin
rem
rem Installs the bundled <install>\share\vscode\cp3-vscode-*.vsix through the
rem `code` CLI (PATH first, then the standard VS Code install locations).
rem Exit codes: 0 installed, 1 VS Code or the bundled .vsix missing, or a
rem non-zero `code --install-extension`.
rem ----------------------------------------------------------------------------
:plugin
call :find_code
if not defined CODE goto plugin_nocode
call :find_vsix
if not defined VSIX goto plugin_novsix
echo cp3: installing the VS Code extension from "%VSIX%" ...
call "%CODE%" --install-extension "%VSIX%" --force
if errorlevel 1 goto plugin_failed
echo cp3: VS Code extension installed.
exit /b 0
:plugin_nocode
echo cp3: the VS Code CLI ('code') was not found on PATH or in the standard
echo      install locations.
echo      Install VS Code, then run this command again:
echo          cp3 install vs-code-plugin
exit /b 1
:plugin_novsix
echo cp3: no bundled VS Code extension found in "%CP3_ROOT%\share\vscode".
echo      This cp3 package was built without one (see packaging/windows). The
echo      extension is optional; the compiler works without it.
exit /b 1
:plugin_failed
echo cp3: 'code --install-extension' failed with exit code %errorlevel%.
exit /b 1

rem ----------------------------------------------------------------------------
rem cp3 doctor
rem
rem Short status list: cp3c presence/version, C++ toolchain availability in the
rem compiler's own detection order (MSVC cl, clang++, g++), ccache, VS Code,
rem the shipped trees and the install root. Exit code 0 when cp3c.exe is
rem present, 1 otherwise.
rem ----------------------------------------------------------------------------
:doctor
set "CP3C_FOUND=no"
set "CP3C_VERSION=unknown"
if not exist "%CP3C%" goto doctor_collect
set "CP3C_FOUND=yes"
"%CP3C%" --version > "%TEMP%\cp3-doctor-version.tmp" 2>nul
if not errorlevel 1 for /f "usebackq delims=" %%V in ("%TEMP%\cp3-doctor-version.tmp") do set "CP3C_VERSION=%%V"
del "%TEMP%\cp3-doctor-version.tmp" 2>nul
:doctor_collect
call :find_toolchain
call :find_ccache
call :find_code
call :find_vsix
set "CODE_STATUS=not found"
if defined CODE set "CODE_STATUS=%CODE%"
set "VSIX_STATUS=none bundled"
if defined VSIX set "VSIX_STATUS=%VSIX%"
set "TREE_INCLUDE=no"
if exist "%CP3_ROOT%\include" set "TREE_INCLUDE=yes"
set "TREE_SRC=no"
if exist "%CP3_ROOT%\src" set "TREE_SRC=yes"
set "TREE_TP=no"
if exist "%CP3_ROOT%\third_party" set "TREE_TP=yes"
echo cp3 doctor
echo   install root : %CP3_ROOT%
echo   shim         : %~f0
if "%CP3C_FOUND%"=="yes" echo   cp3c         : %CP3C%
if not "%CP3C_FOUND%"=="yes" echo   cp3c         : MISSING ^(%CP3C%^)
echo   cp3c version : %CP3C_VERSION%
echo   C++ toolchain: %TC%
echo   ccache       : %CCACHE%
echo   VS Code CLI  : %CODE_STATUS%
echo   bundled vsix : %VSIX_STATUS%
echo   shipped trees: include=%TREE_INCLUDE% src=%TREE_SRC% third_party=%TREE_TP%
echo   note         : a C++ toolchain (g++/clang++/cl) on PATH is required to
echo                  build cp3 programs; VS Code is optional.
if "%CP3C_FOUND%"=="yes" exit /b 0
exit /b 1

rem ----------------------------------------------------------------------------
rem Helpers (each sets one variable or prints nothing)
rem ----------------------------------------------------------------------------

rem CODE: path of the VS Code CLI, undefined when absent. `code.cmd` is the
rem Windows entry point; the extensionless `code` file VS Code also ships is a
rem shell script and must never be selected (cmd cannot execute it).
:find_code
set "CODE="
for /f "delims=" %%C in ('where code.cmd 2^>nul') do if not defined CODE set "CODE=%%C"
if not defined CODE for /f "delims=" %%C in ('where code.exe 2^>nul') do if not defined CODE set "CODE=%%C"
if not defined CODE for /f "delims=" %%C in ('where code 2^>nul') do if not defined CODE if /I "%%~xC"==".cmd" set "CODE=%%C"
if defined CODE exit /b 0
for %%P in ("%LOCALAPPDATA%\Programs\Microsoft VS Code\bin\code.cmd" "%ProgramFiles%\Microsoft VS Code\bin\code.cmd" "%PF86%\Microsoft VS Code\bin\code.cmd") do if not defined CODE if exist "%%~fP" set "CODE=%%~fP"
exit /b 0

rem VSIX: path of the bundled extension, undefined when none.
:find_vsix
set "VSIX="
for %%V in ("%CP3_ROOT%\share\vscode\cp3-vscode-*.vsix") do if not defined VSIX if exist "%%~fV" set "VSIX=%%~fV"
exit /b 0

rem TC: first toolchain in cp3c's own order (MSVC cl -> clang++ -> g++).
:find_toolchain
set "TC=(none found - install MSVC, Clang or GCC/g++ and put it on PATH)"
for /f "delims=" %%C in ('where cl 2^>nul') do if "%TC%"=="(none found - install MSVC, Clang or GCC/g++ and put it on PATH)" set "TC=MSVC cl (%%C)"
for /f "delims=" %%C in ('where clang++ 2^>nul') do if "%TC%"=="(none found - install MSVC, Clang or GCC/g++ and put it on PATH)" set "TC=Clang (%%C)"
for /f "delims=" %%C in ('where g++ 2^>nul') do if "%TC%"=="(none found - install MSVC, Clang or GCC/g++ and put it on PATH)" set "TC=GCC (%%C)"
exit /b 0

rem CCACHE: ccache presence (optional build accelerator).
:find_ccache
set "CCACHE=not found (optional)"
for /f "delims=" %%C in ('where ccache 2^>nul') do if "%CCACHE%"=="not found (optional)" set "CCACHE=found (%%C)"
exit /b 0
