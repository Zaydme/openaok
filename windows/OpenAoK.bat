@echo off
rem Prepares the game data from original_game_1999\ the first time, then
rem starts the game.
cd /d "%~dp0"
set PY=python
where py >nul 2>nul && set PY=py -3
%PY% tools\data\prepare_data.py
if errorlevel 1 (
    pause
    exit /b 1
)
start "" /d "%~dp0build" "%~dp0build\OpenAoK.exe" %*
