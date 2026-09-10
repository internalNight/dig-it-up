@echo off
setlocal
echo Optional Microsoft Visual C++ runtime installer.
echo Usually unnecessary: this game already includes local runtime DLLs.
echo Windows may ask for administrator approval. No automatic reboot is requested.
start "C++ Runtime" /wait "%~dp0Prerequisites\vc_redist.x64.exe" /install /norestart
endlocal
