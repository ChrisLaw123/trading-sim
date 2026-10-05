@echo off
REM Double-click this to start the simulator.
REM
REM The real work happens in WSL, where the compiler and PostgreSQL live. This
REM just forwards to run.sh and opens the browser once the server answers.
REM Close this window, or press Ctrl-C, to stop the server.

setlocal
title Trading Sim

REM The trailing "." matters: %~dp0 ends in a backslash, and "C:\path\" would
REM let that backslash escape the closing quote.
for /f "usebackq delims=" %%i in (`wsl.exe wslpath -a "%~dp0." 2^>nul`) do set "WSLDIR=%%i"

if not defined WSLDIR (
    echo.
    echo   Could not reach WSL. Install it with:  wsl --install
    echo.
    pause
    exit /b 1
)

REM Wait for the port to answer rather than guessing at a delay, then open the
REM page. Gives up after 90 seconds so a failed build never leaves this running.
start "" /b powershell -NoProfile -WindowStyle Hidden -Command ^
  "for($i=0;$i -lt 90;$i++){try{(New-Object Net.Sockets.TcpClient).Connect('127.0.0.1',8080);Start-Process 'http://localhost:8080';break}catch{Start-Sleep 1}}"

wsl.exe -e bash -lc "cd '%WSLDIR%' && ./run.sh"

echo.
pause
