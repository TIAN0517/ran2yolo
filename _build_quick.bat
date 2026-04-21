@echo off
cd /d "%~dp0"
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvarsall.bat" x86
msbuild JyTrainer.vcxproj /p:Configuration=Release /p:Platform=Win32 /v:m
