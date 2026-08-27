@echo off
setlocal
set "ZIMA_CAD_ARCHIVE=%~dp0"
for %%I in ("%ZIMA_CAD_ARCHIVE%..\..") do set "ZIMA_CAD_ROOT=%%~fI\"
set "ZIMA_CAD_APPLICATION_ROOT=%ZIMA_CAD_ROOT%"
set "PYTHONPATH=%ZIMA_CAD_ARCHIVE%;%PYTHONPATH%"
set "ZIMA_CAD_PYTHON=%ZIMA_CAD_ROOT%runtime\windows\python\python.exe"
set "ZIMA_CAD_CONDA_UNPACK=%ZIMA_CAD_ROOT%runtime\windows\python\Scripts\conda-unpack.exe"
set "ZIMA_CAD_CONDA_MARKER=%ZIMA_CAD_ROOT%runtime\windows\python\.zima-conda-unpacked"

if not exist "%ZIMA_CAD_PYTHON%" (
    echo ZIMA-CAD Windows runtime was not found:
    echo %ZIMA_CAD_PYTHON%
    echo.
    echo Build and unpack it with:
    echo tools\pack-windows-runtime.ps1
    echo tools\unpack-windows-runtime.ps1 -Force
    exit /b 1
)

if exist "%ZIMA_CAD_CONDA_UNPACK%" if not exist "%ZIMA_CAD_CONDA_MARKER%" (
    echo Preparing the ZIMA-CAD Windows runtime for this location...
    "%ZIMA_CAD_CONDA_UNPACK%"
    if errorlevel 1 (
        echo ZIMA-CAD could not finalize its Windows runtime.
        exit /b 1
    )
    type nul > "%ZIMA_CAD_CONDA_MARKER%"
)

"%ZIMA_CAD_PYTHON%" "%ZIMA_CAD_ARCHIVE%main.py" %*
exit /b %ERRORLEVEL%
