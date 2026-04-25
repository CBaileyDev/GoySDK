@echo off
rmdir /s /q build
mkdir build
cd build
cmake -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="C:\libtorch-cpu" -DVIGEM_DIR="%CD%\..\deps\ViGEmClient" ..
if %ERRORLEVEL% neq 0 exit /b %ERRORLEVEL%
ninja
