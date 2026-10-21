@echo off
>>"%FAKE_EDITOR_LOG%" echo code %*
if /I "%~1"=="--uninstall-extension" (
  for /d %%D in ("%USERPROFILE%\.vscode\extensions\cp3.cp3-vscode-*") do rmdir /s /q "%%D"
)
if defined FAKE_EDITOR_EXIT exit /b %FAKE_EDITOR_EXIT%
exit /b 0
