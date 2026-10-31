@echo off
rem Build and run the editor on Windows.  Usage: build_editor.bat [gpu] [nodx12] [norun]
rem   gpu          = Vulkan + OpenGL + Direct3D 12 (preset editor-gpu-dx12)
rem   gpu nodx12   = Vulkan + OpenGL only (preset editor-gpu, faster build)
rem No Git, Ninja or internet needed. Uses the newest Visual Studio found by CMake.
setlocal
cd /d "%~dp0"
set PRESET= editor-gpu-dx12
set RUN=1
for %%A in (%*) do (
  if /i "%%A"=="gpu" set PRESET=editor-gpu-dx12
  if /i "%%A"=="norun" set RUN=0
)
for %%A in (%*) do (
  if /i "%%A"=="nodx12" if "%PRESET%"=="editor-gpu-dx12" set PRESET=editor-gpu
)

set CMAKE=cmake
where cmake >nul 2>nul
if errorlevel 1 call :findcmake
if "%CMAKE%"=="" (
  echo ERROR: CMake not found. Install CMake 3.25+ from https://cmake.org/download/ ^(tick "Add to PATH"^)
  echo        or install Visual Studio with "Desktop development with C++".
  pause & exit /b 1
)

if not "%PRESET%"=="editor" (
  where python >nul 2>nul
  if errorlevel 1 (
    echo ERROR: the GPU build needs Python 3 ^(DiligentCore uses it^). Install it from https://www.python.org/downloads/
    echo        and TICK "Add python.exe to PATH" in the installer, then open a NEW terminal and run again.
    pause & exit /b 1
  )
)
echo Using: %CMAKE%
"%CMAKE%" --preset %PRESET% > build_log.txt 2>&1
if errorlevel 1 goto fail
"%CMAKE%" --build --preset %PRESET% >> build_log.txt 2>&1
if errorlevel 1 goto fail
type build_log.txt | findstr /i /c:"error" /c:"warning C" >nul && echo (see build_log.txt for warnings)
echo.
echo Done: Projeto Final\ToolEditor.exe
if "%RUN%"=="1" start "" "Projeto Final\ToolEditor.exe"
exit /b 0

:fail
type build_log.txt
echo.
echo BUILD FAILED. Please send the file build_log.txt ^(in this folder^) to the developer.
pause
exit /b 1

:findcmake
set CMAKE=
for %%E in (Insiders Preview Community Professional Enterprise BuildTools) do (
  for %%Y in (18 2026 2025 2022 2019) do (
    for %%P in ("%ProgramFiles%" "%ProgramFiles(x86)%") do (
      if exist "%%~P\Microsoft Visual Studio\%%Y\%%E\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" (
        if "%CMAKE%"=="" set "CMAKE=%%~P\Microsoft Visual Studio\%%Y\%%E\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
      )
    )
  )
)
exit /b 0
