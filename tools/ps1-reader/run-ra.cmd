@echo off
set RA=C:\Users\Devin Prater\Downloads\ra-mcp-test\RASETUP\retroarch.exe
set CFG=C:\Users\Devin Prater\Downloads\ra-mcp-test\ra-mcp-run.cfg
set CORE=C:\Users\Devin Prater\scoop\persist\retroarch\cores\mupen64plus_next_libretro.dll
set ROM=C:\Users\Devin Prater\Dropbox\games\N64\Pokemon Stadium (USA) (Rev 2).z64
start "" "%RA%" -c "%CFG%" -L "%CORE%" "%ROM%"
echo Waiting for RetroArch and the MCP server...
timeout /t 10 /nobreak >nul
python "%~dp0reader.py"
