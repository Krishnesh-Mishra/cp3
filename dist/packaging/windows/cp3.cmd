@echo off
rem ============================================================================
rem cp3.cmd - the user-facing `cp3` command, shipped as <install>\bin\cp3.cmd.
rem
rem A thin shim around cp3c.exe: the compiler keeps its name, the terminal
rem command is `cp3` (PKG-CONTRACTS section 1). The only command owned here
rem and never forwarded is:
rem   cp3 doctor                       print environment status
rem Everything `cp3c` owns is forwarded, including `cp3 init` and the editor
rem extension command: `cp3 editor-plugin` (alias `cp3 editor-plugins`).
rem Everything else - including `cp3 lsp`, `cp3 run`, `cp3 build`, `cp3 init`
rem arguments we do not own - is forwarded verbatim to cp3c.exe with the exit
rem code passed through.
rem
rem CP3_ROOT is exported for the child process so cp3c.exe finds the shipped
rem include/, src/ and vendor/third_party/ trees even when this shim is reached
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

:forward
rem Everything else - including `cp3 init`, `cp3 lsp`, `cp3 run`,
rem `cp3 build` - lands here: cp3c owns those commands.
"%CP3C%" %*
exit /b %errorlevel%

rem ----------------------------------------------------------------------------
rem cp3 doctor
rem
rem Short status list: cp3c presence/version, Clang availability, ccache,
rem VS Code, the shipped trees and the install root. Exit code 0 when cp3c.exe
rem is present, 1 otherwise.
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
if exist "%CP3_ROOT%\vendor\third_party" set "TREE_TP=yes"
echo cp3 doctor
echo   install root : %CP3_ROOT%
echo   shim         : %~f0
if "%CP3C_FOUND%"=="yes" echo   cp3c         : %CP3C%
if not "%CP3C_FOUND%"=="yes" echo   cp3c         : MISSING ^(%CP3C%^)
echo   cp3c version : %CP3C_VERSION%
echo   toolchain    : %TC%
echo   ccache       : %CCACHE%
echo   VS Code CLI  : %CODE_STATUS%
echo   bundled vsix : %VSIX_STATUS%
echo   shipped trees: include=%TREE_INCLUDE% src=%TREE_SRC% vendor_third_party=%TREE_TP%
echo   note         : Clang is required to build cp3 programs. If clang++ is
echo                  not on PATH, select one with: cp3 toolchain
echo                  VS Code is optional.
echo                  Install/open the bundled extension with: cp3 editor-plugin
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

rem TC: the Clang driver cp3c will use. Ask cp3c itself - it owns the
rem resolution order (--toolchain > CP3_TOOLCHAIN > project package.json >
rem global toolchain.conf > auto-detect). Fall back to PATH discovery only
rem when cp3c is missing or cannot answer (older cp3c without `toolchain
rem current`).
:find_toolchain
set "TC="
if not exist "%CP3C%" goto find_toolchain_path
for /f "delims=" %%L in ('"%CP3C%" toolchain current 2^>nul') do if not defined TC set "TC=%%L"
:find_toolchain_path
if defined TC exit /b 0
set "TC=(none found - run 'cp3 toolchain' to select one, or put clang++ on PATH)"
for /f "delims=" %%C in ('where clang++ 2^>nul') do if "%TC%"=="(none found - run 'cp3 toolchain' to select one, or put clang++ on PATH)" set "TC=Clang (%%C)"
exit /b 0

rem CCACHE: ccache presence (optional build accelerator).
:find_ccache
set "CCACHE=not found (optional)"
for /f "delims=" %%C in ('where ccache 2^>nul') do if "%CCACHE%"=="not found (optional)" set "CCACHE=found (%%C)"
exit /b 0
