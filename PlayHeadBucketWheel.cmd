@echo off
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0Tools\PlayMachines.ps1" -Head BucketWheel -InspectHead %*
