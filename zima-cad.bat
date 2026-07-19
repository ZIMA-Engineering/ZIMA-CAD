@echo off
setlocal
set "ZIMA_CAD_ROOT=%~dp0"
set "ZIMA_CAD_PYTHON=%ZIMA_CAD_ROOT%runtime\windows\python\python.exe"

if not exist "%ZIMA_CAD_PYTHON%" (
    echo ZIMA-CAD Windows runtime was not found:
    echo %ZIMA_CAD_PYTHON%
    echo.
    echo Build and unpack it with:
    echo tools\pack-windows-runtime.ps1
    echo tools\unpack-windows-runtime.ps1 -Force
    exit /b 1
)

"%ZIMA_CAD_PYTHON%" "%ZIMA_CAD_ROOT%main.py" %*
