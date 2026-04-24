@echo off
cd /d "%~dp0"
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvarsall.bat" x64
msbuild JyTrainer.vcxproj /p:Configuration=Release /p:Platform=x64 /v:m
if errorlevel 1 exit /b %errorlevel%
if exist "..\dist\license_token.dat" copy /Y "..\dist\license_token.dat" "..\dist\win64\license_token.dat" >nul
if exist "..\dist\license_public.blob" copy /Y "..\dist\license_public.blob" "..\dist\win64\license_public.blob" >nul
