@echo off
title WC2r Audio Tool
set WC2R_AUDIO_TOOL_DIR=%~dp0
cd /d "%~dp0"
for /f "tokens=5" %%a in ('netstat -ano ^| findstr ":8799.*LISTENING"') do taskkill /F /PID %%a >nul 2>&1
py -3 "%~dp0audio_tool.py"
if errorlevel 1 pause
