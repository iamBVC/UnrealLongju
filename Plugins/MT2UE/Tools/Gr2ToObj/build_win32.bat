@echo off
setlocal

set ROOT=%~dp0..\..\..
set OUTDIR=%~dp0bin\Win32
if not exist "%OUTDIR%" mkdir "%OUTDIR%"

where cl >nul 2>nul
if errorlevel 1 (
    echo cl.exe was not found. Run this from a Visual Studio Developer Command Prompt, or call vcvars32.bat first.
    exit /b 1
)

cl /nologo /EHsc /W4 /DWIN32 /D_WINDOWS /Fo"%OUTDIR%\\" /I"%ROOT%\client_src\extern\include" ^
    "%~dp0Gr2ToObj.cpp" ^
    /link /LIBPATH:"%ROOT%\client_src\extern\library" granny2.lib /OUT:"%OUTDIR%\Gr2ToObj.exe"

if errorlevel 1 exit /b 1

if exist "%ROOT%\client\granny2.dll" (
    copy /Y "%ROOT%\client\granny2.dll" "%OUTDIR%\granny2.dll" >nul
)

endlocal
