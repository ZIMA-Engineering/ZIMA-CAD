@echo off
setlocal
set "ZIMA_CAD_ROOT=%~dp0"

if exist "%ZIMA_CAD_ROOT%build\cpp-release\zima-cad-cpp.exe" (
    "%ZIMA_CAD_ROOT%build\cpp-release\zima-cad-cpp.exe" %*
    exit /b %ERRORLEVEL%
)

if exist "%ZIMA_CAD_ROOT%build\cpp-debug\zima-cad-cpp.exe" (
    "%ZIMA_CAD_ROOT%build\cpp-debug\zima-cad-cpp.exe" %*
    exit /b %ERRORLEVEL%
)

echo ZIMA-CAD C++ executable was not found.
echo Build the zima-cad-cpp target first.
exit /b 1
