@echo off
>>"%FAKE_EDITOR_LOG%" echo code-insiders %*
if defined FAKE_EDITOR_EXIT exit /b %FAKE_EDITOR_EXIT%
exit /b 0
