@echo off
setlocal
rem OEngine "Engine Shipping" build, published to Package\Gd\Shipping by Godot.ps1.
rem Usage: EngineShipping.bat [Build^|Rebuild] [jobs]   e.g. EngineShipping.bat Rebuild 16
set "ACTION=%~1"
if "%ACTION%"=="" set "ACTION=Build"
set "JOBS=%~2"
if "%JOBS%"=="" set "JOBS=8"

rem A running editor locks oengine.editor.exe and the publish to Package fails.
:check_editor
tasklist /FI "IMAGENAME eq oengine.editor.exe" | find /I "oengine.editor.exe" >nul
if not errorlevel 1 (
    echo OEngine editor is running. Save your scenes, close it, then press any key.
    pause >nul
    goto check_editor
)

rem Repository root: two levels above this script.
pushd "%~dp0..\.." || (echo Repository root not found & pause & exit /b 1)
git log -1 --format="Commit: %%h %%s"
powershell -NoProfile -ExecutionPolicy Bypass -File "tools\oengine\Godot.ps1" -Action %ACTION% -Configuration "Engine Shipping" -Jobs %JOBS%
set "CODE=%ERRORLEVEL%"
popd

if not "%CODE%"=="0" (
    echo Build FAILED, exit code %CODE%.
) else (
    echo Done.
)
pause
exit /b %CODE%
