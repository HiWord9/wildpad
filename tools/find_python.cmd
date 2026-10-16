@echo off
rem Sets PY to a working Python 3 interpreter, or exits with errorlevel 1.
rem Double-clicked .bat files may not see the same PATH as a terminal, so look in
rem the usual install locations too.
set "PY="
for %%P in (py.exe python.exe python3.exe) do (
  if not defined PY for /f "delims=" %%I in ('where %%P 2^>nul') do (
    if not defined PY "%%I" -c "import sys; sys.exit(sys.version_info[0] != 3)" >nul 2>&1 && set "PY=%%I"
  )
)
if not defined PY if exist "%LOCALAPPDATA%\Microsoft\WindowsApps\python.exe" set "PY=%LOCALAPPDATA%\Microsoft\WindowsApps\python.exe"
if not defined PY for /d %%D in ("%LOCALAPPDATA%\Programs\Python\Python3*" "%ProgramFiles%\Python3*") do (
  if not defined PY if exist "%%~D\python.exe" set "PY=%%~D\python.exe"
)
if defined PY exit /b 0
echo Python 3 was not found.
echo Install it with:   winget install Python.Python.3.12
echo or from https://www.python.org/downloads/ (tick "Add python.exe to PATH"), then run this again.
exit /b 1
