@echo off
setlocal enabledelayedexpansion
echo Building project...

rem === Remove old build folder ===
if exist build (
    echo Removing old build folder...
    rmdir /s /q build
)

rem === Create new build folder ===
mkdir build
cd build

rem === Run CMake configuration ===
cmake ..

rem === Build the project ===
cmake --build . --config Release

rem === Run tests (if ctest is available) ===
ctest --output-on-failure

echo Build complete!
endlocal
pause
