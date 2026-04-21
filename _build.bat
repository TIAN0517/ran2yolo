@echo off
cd /d "%~dp0"
"C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe" JyTrainer.vcxproj /p:Configuration=Release /p:Platform=Win32 /v:m
