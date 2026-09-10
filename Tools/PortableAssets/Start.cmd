@echo off
setlocal
set "DIGITUP_EXE=%~dp0SandExcavator\Binaries\Win64\SandExcavator.exe"
if not exist "%DIGITUP_EXE%" (
    echo Game files are missing. Extract or copy the ENTIRE DigItUp folder first.
    pause
    exit /b 1
)
set "DIGITUP_DATA=%LOCALAPPDATA%\DigItUp"
if not defined LOCALAPPDATA set "DIGITUP_DATA=%TEMP%\DigItUp"
if not exist "%DIGITUP_DATA%" mkdir "%DIGITUP_DATA%"
set "DIGITUP_RES=-ResX=1920 -ResY=1080"
if /I "%~1"=="720p" set "DIGITUP_RES=-ResX=1280 -ResY=720"
pushd "%~dp0"
start "Dig It Up" "%DIGITUP_EXE%" -windowed %DIGITUP_RES% -d3d12 -sm6 -UserDir="%DIGITUP_DATA%"
popd
endlocal
