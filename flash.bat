@echo off
cd /d "%~dp0"
call tools\find_python.cmd || goto end
"%PY%" tools\flash.py %*
:end
echo.
pause
