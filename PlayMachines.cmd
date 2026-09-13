@echo off
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0Tools\PlayMachines.ps1" -SelectVehicle %*
