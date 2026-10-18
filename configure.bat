@echo off
cd /d "%~dp0"
call tools\find_python.cmd || (pause & exit /b 1)
"%PY%" -c "import socket; s=socket.socket(); s.bind(('127.0.0.1',8765)); s.close()" >nul 2>&1
if errorlevel 1 (
  echo Port 8765 is already in use. Is another configure.bat still running?
  pause
  exit /b 1
)
echo Configurator: http://127.0.0.1:8765/tools/configurator.html
echo Keep this window open while you set up the pad. Close it when done.
start "" "http://127.0.0.1:8765/tools/configurator.html"
"%PY%" -m http.server 8765 --bind 127.0.0.1 --directory .
if errorlevel 1 (
  echo.
  echo The server stopped unexpectedly. Press any key to close.
  pause >nul
)
