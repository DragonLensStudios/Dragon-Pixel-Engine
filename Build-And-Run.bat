@echo off
setlocal

"%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe" -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%~dp0scripts\dev\Build-Production-Editor.ps1" -Launch
exit /b %ERRORLEVEL%
