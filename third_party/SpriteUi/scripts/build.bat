@echo off
setlocal
cd /d "%~dp0\.."

where cmake >nul 2>nul
if errorlevel 1 (
    echo [ERRO] CMake nao encontrado no PATH.
    exit /b 1
)

where ninja >nul 2>nul
if not errorlevel 1 (
    echo [INFO] Ninja encontrado. Usando preset default.
    cmake --preset default
    if errorlevel 1 exit /b 1
    cmake --build --preset default
    if errorlevel 1 exit /b 1
    ctest --preset default
    if errorlevel 1 exit /b 1
) else (
    echo [INFO] Ninja nao encontrado. Usando o gerador padrao do CMake.
    cmake -S . -B build -DSPRITEUI_BUILD_EXAMPLES=ON -DSPRITEUI_BUILD_TESTS=ON -DCMAKE_BUILD_TYPE=Debug
    if errorlevel 1 exit /b 1
    cmake --build build --config Debug
    if errorlevel 1 exit /b 1
    ctest --test-dir build -C Debug --output-on-failure
    if errorlevel 1 exit /b 1
)

echo.
echo Build e testes concluidos.
echo Executavel de exemplo: build\spriteui_hello.exe
endlocal
