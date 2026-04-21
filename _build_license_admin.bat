@echo off
cd /d "%~dp0"
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvarsall.bat" x86
rc /nologo /fo license_admin.res app.rc
cl /nologo /EHsc /O2 /MT /utf-8 /DWIN32 /D_WIN32_WINNT=0x0601 /DWINVER=0x0601 ^
  license_admin.cpp offline_license.cpp license_admin.res ^
  /link bcrypt.lib user32.lib /OUT:..\dist\license_admin.exe
if exist license_admin.res del /q license_admin.res
