@echo off
title RoBoHoN Control App

echo Starting local server on http://localhost:8000
echo Press CTRL+C to stop the server.
echo.

REM Start a simple HTTP server using PowerShell
start "" powershell -NoLogo -NoProfile -Command ^
  "cd '%~dp0'; python -m http.server 8000"

REM Give the server a moment to start
timeout /t 2 >nul

REM Launch Chrome pointed at the app
start chrome "http://localhost:8000/index.html"

exit
