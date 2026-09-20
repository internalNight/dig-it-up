@echo off
setlocal
set "UE_EDITOR=D:\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe"

if not exist "%UE_EDITOR%" (
    echo Unreal Engine 5.8 was not found at:
    echo %UE_EDITOR%
    pause
    exit /b 1
)

start "Dig It Up - Lunar Field" "%UE_EDITOR%" "%~dp0SandExcavator.uproject" -game -windowed -ResX=1920 -ResY=1080 -d3d12 -sm6 -graphicsadapter=0 -SandExcavator
endlocal
