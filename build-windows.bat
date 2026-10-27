@echo off
setlocal EnableExtensions DisableDelayedExpansion
pushd "%~dp0"
if errorlevel 1 (
    echo ERROR: Cannot open the project directory.
    pause
    exit /b 1
)

set "TRIGGERLAB_LOG=%CD%\build-windows.log"
echo Building TriggerLab. This may take several minutes.
echo Build output: "%TRIGGERLAB_LOG%"
call :build >"%TRIGGERLAB_LOG%" 2>&1
set "TRIGGERLAB_RESULT=%ERRORLEVEL%"
type "%TRIGGERLAB_LOG%"
if not "%TRIGGERLAB_RESULT%"=="0" goto failed

echo.
echo SUCCESS: build\Release\TriggerLab.exe
start "" /D "%CD%\build\Release" "%CD%\build\Release\TriggerLab.exe"
if errorlevel 1 goto launch_failed
popd
exit /b 0

:build
where cmake >nul 2>&1
if errorlevel 1 (
    echo ERROR: CMake was not found in PATH.
    echo Install CMake 3.20 or newer and enable "Add CMake to the system PATH".
    echo Then close this window and run build-windows.bat again.
    exit /b 1
)
where ctest >nul 2>&1
if errorlevel 1 (
    echo ERROR: CTest was not found in PATH. Repair your CMake installation.
    exit /b 1
)
echo [1/3] Configuring Visual Studio 2022 x64...
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
if errorlevel 1 (
    echo ERROR: Configuration failed.
    echo Install Visual Studio 2022 or Build Tools 2022 with:
    echo Desktop development with C++, MSVC v143 and a Windows 10/11 SDK.
    echo If build was made with another generator, rename the build directory.
    exit /b 1
)
echo [2/3] Building Release...
cmake --build build --config Release --parallel
if errorlevel 1 exit /b 1
echo [3/3] Running tests...
ctest --test-dir build -C Release --output-on-failure
if errorlevel 1 exit /b 1
if not exist "build\Release\TriggerLab.exe" (
    echo ERROR: The build did not produce TriggerLab.exe.
    exit /b 1
)
exit /b 0

:launch_failed
set "TRIGGERLAB_RESULT=1"
echo ERROR: Windows could not start TriggerLab.exe.
:failed
echo.
echo ERROR: Build, tests or launch failed. See the messages above.
echo Log: "%TRIGGERLAB_LOG%"
echo Test log: build\Testing\Temporary\LastTest.log
echo.
pause
popd
exit /b %TRIGGERLAB_RESULT%
