@echo off
setlocal
pushd "%~dp0"
if errorlevel 1 exit /b 1

cmake -S . -B build -G "Visual Studio 17 2022" -A x64
if errorlevel 1 goto failed
cmake --build build --config Release
if errorlevel 1 goto failed
ctest --test-dir build -C Release --output-on-failure
if errorlevel 1 goto failed

echo.
echo SUCCESS: build\Release\TriggerLab.exe
start "" "build\Release\TriggerLab.exe"
popd
exit /b 0

:failed
echo.
echo ERROR: Build or tests failed.
popd
exit /b 1
