@echo off
setlocal enabledelayedexpansion
cd /d "%~dp0"

call .\build_harness.cmd >nul
if errorlevel 1 ( echo Harness build failed & exit /b 1 )

set H=.\build\harness\test_harness.exe
set FAIL=0

:: mode | fixture | golden suffix
for %%C in (
    "extract extract_python.txt extract_python"
    "extract extract_mixed.txt  extract_mixed"
    "extract extract_bare.txt   extract_bare"
    "explain explain_in.txt     explain"
    "match   match_in.txt       match"
    "encode  encode_in.bin      encode"
) do (
    for /f "tokens=1,2,3" %%a in (%%C) do (
        %H% %%a tests\%%b > tests\actual_%%c.txt
        fc /b tests\golden_%%c.txt tests\actual_%%c.txt >nul
        if errorlevel 1 ( echo MISMATCH: %%c & set FAIL=1 ) else ( echo OK: %%c )
    )
)

if !FAIL! neq 0 (
    echo.
    echo Output changed. If the new output is correct, review it and copy
    echo tests\actual_*.txt over tests\golden_*.txt.
    exit /b 1
)
echo.
echo All tests passed.
exit /b 0
