@echo off

REM Configuration (Debug by default)
set CONFIG=Debug
if not "%1"=="" set CONFIG=%1

echo Build configuration: %CONFIG%

REM Remove old build folder if it exists
if exist build (
    echo Removing old build folder...
    rmdir /s /q build
)

REM Generate project
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 -DCMAKE_BUILD_TYPE=%CONFIG%

REM Build project
cmake --build build --config %CONFIG%

echo.
echo Build finished!
pause



