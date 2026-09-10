@echo off
setlocal
set "UE_EDITOR=D:\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe"
set "PACKAGED_GAME=%~dp0Builds\DigItUpVisual_20260909\SandExcavator.exe"
if not exist "%PACKAGED_GAME%" set "PACKAGED_GAME=%~dp0Builds\DigItUpEasy_20260909\SandExcavator.exe"
if not exist "%PACKAGED_GAME%" set "PACKAGED_GAME=%~dp0Builds\WindowsRegolith_20260909\SandExcavator.exe"
if not exist "%PACKAGED_GAME%" set "PACKAGED_GAME=%~dp0Builds\WindowsFirstPlayable\SandExcavator.exe"

if exist "%PACKAGED_GAME%" (
    start "Dig It Up - Easy" "%PACKAGED_GAME%" -windowed -ResX=1920 -ResY=1080 -d3d12 -sm6 -graphicsadapter=0
    endlocal
    exit /b 0
)

if not exist "%UE_EDITOR%" (
    echo Unreal Engine 5.8 was not found at:
    echo %UE_EDITOR%
    pause
    exit /b 1
)

start "Sand Excavator Preview" "%UE_EDITOR%" "%~dp0SandExcavator.uproject" -game -windowed -ResX=1920 -ResY=1080 -d3d12 -sm6 -graphicsadapter=0
endlocal
