@echo off
cd /d "%~dp0"
echo ========================================
echo   PowerRune24 Operator Launcher
echo ========================================
echo.

where python >nul 2>nul
if errorlevel 1 (
    echo [ERROR] python not found in PATH.
    echo Please install Python or add it to PATH.
    echo.
    pause
    exit /b 1
)

echo Starting pr-26-operator.py ...
echo.
python pr-26-operator.py

echo.
echo ========================================
echo Program exited with code %errorlevel%
echo ========================================
pause
