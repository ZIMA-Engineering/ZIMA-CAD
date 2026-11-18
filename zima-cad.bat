@echo off
setlocal
set "ZIMA_CAD_ROOT=%~dp0"

if exist "%ZIMA_CAD_ROOT%build\cpp-windows-release\zima-cad-cpp.exe" (
    start "" /D "%ZIMA_CAD_ROOT%." "%ZIMA_CAD_ROOT%build\cpp-windows-release\zima-cad-cpp.exe" %*
    exit /b 0
)

if exist "%ZIMA_CAD_ROOT%build\cpp-release\zima-cad-cpp.exe" (
    start "" /D "%ZIMA_CAD_ROOT%." "%ZIMA_CAD_ROOT%build\cpp-release\zima-cad-cpp.exe" %*
    exit /b 0
)

if exist "%ZIMA_CAD_ROOT%build\cpp-debug\zima-cad-cpp.exe" (
    start "" /D "%ZIMA_CAD_ROOT%." "%ZIMA_CAD_ROOT%build\cpp-debug\zima-cad-cpp.exe" %*
    exit /b 0
)

echo ZIMA-CAD C++ executable was not found.
echo Build the zima-cad-cpp target first.
exit /b 1
