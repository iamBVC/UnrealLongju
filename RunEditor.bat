@echo off

if exist "..\Engine2\Binaries\Win64\UnrealEditor-Win64-DebugGame-Cmd.exe" (
	set engine_dir="..\Engine2\Binaries\Win64\"
) else (
	set engine_dir="..\Engine2\Engine\Binaries\Win64\"
)

cd %engine_dir%
start UnrealEditor.exe "%~dp0\UnrealLongju.uproject"
exit
