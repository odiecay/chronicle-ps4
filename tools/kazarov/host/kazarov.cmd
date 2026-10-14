@echo off
rem Kazarov entry point for Windows hosts: kazarov.cmd ROOT build|run [config] [args].
rem The arguments go to kazarov.ps1 as %* and are not expanded again here.
setlocal

set "PS=powershell"
where pwsh >nul 2>&1 && set "PS=pwsh"

"%PS%" -NoProfile -ExecutionPolicy Bypass -File "%~dp0kazarov.ps1" %*
exit /b %errorlevel%
