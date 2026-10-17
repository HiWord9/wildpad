@echo off
cd /d "%~dp0"
call tools\find_python.cmd || (pause & exit /b 1)
echo Configurator: http://127.0.0.1:8765/tools/configurator.html
echo Keep this window open while you set up the pad. Close it when done.
start "" "http://127.0.0.1:8765/tools/configurator.html"
"%PY%" -m http.server 8765 --bind 127.0.0.1 --directory .
