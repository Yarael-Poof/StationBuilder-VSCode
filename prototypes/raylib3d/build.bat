@echo off
rem Builds the 3D prototype (Release) into build\raylib3d at the repository's root. "build.bat run" also runs it from
rem this folder, where scene.txt is. The first build downloads raylib and Dear ImGui.
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
cd /d "%~dp0"
cmake -S . -B ..\..\build\raylib3d -G Ninja -DCMAKE_BUILD_TYPE=Release || exit /b 1
cmake --build ..\..\build\raylib3d || exit /b 1
if /i "%1"=="run" "..\..\build\raylib3d\Raylib3DDemo.exe"
