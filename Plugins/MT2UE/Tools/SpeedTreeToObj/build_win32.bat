@echo off
setlocal

set PLUGIN=%~dp0..\..
set SDK=%PLUGIN%\ThirdParty\SpeedTree
set OUT=%PLUGIN%\Binaries\ThirdParty\SpeedTree\Win32
if not exist "%OUT%" mkdir "%OUT%"

where cl >nul 2>nul
if errorlevel 1 (
    echo cl.exe was not found. Run this from a Visual Studio x86 Developer Command Prompt.
    exit /b 1
)

cl /nologo /EHsc /W4 /DWIN32 /D_WINDOWS /I"%SDK%\include" ^
    "%~dp0SpeedTreeToObj.cpp" /Fo"%OUT%\\" ^
    /link /LIBPATH:"%SDK%\lib\Win32" SpeedTreeRT.lib /OUT:"%OUT%\SpeedTreeToObj.exe"
if errorlevel 1 exit /b 1

copy /Y "%SDK%\bin\Win32\SpeedTreeRT.dll" "%OUT%\SpeedTreeRT.dll" >nul
endlocal
