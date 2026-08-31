@echo off
REM Script to build Simple FTS5 extension for Windows

echo ====================================
echo Building Simple FTS5 Extension
echo ====================================

REM 检查 Git 是否安装
where git >nul 2>nul
if %ERRORLEVEL% NEQ 0 (
    echo ERROR: Git is not installed or not in PATH
    exit /b 1
)

REM 检查 CMake 是否安装
where cmake >nul 2>nul
if %ERRORLEVEL% NEQ 0 (
    echo ERROR: CMake is not installed or not in PATH
    exit /b 1
)

REM 创建构建目录
if not exist "third_party" mkdir third_party
cd third_party

REM 克隆 Simple 仓库（如果还没有）
if not exist "simple" (
    echo Cloning Simple repository...
    git clone --depth 1 https://github.com/wangfenjin/simple.git
    if %ERRORLEVEL% NEQ 0 (
        echo ERROR: Failed to clone Simple repository
        cd ..
        exit /b 1
    )
) else (
    echo Simple repository already exists, updating...
    cd simple
    git pull
    cd ..
)

cd simple

REM 创建构建目录
if not exist "build" mkdir build
cd build

REM 配置 CMake
echo Configuring Simple build...
cmake .. -G "Visual Studio 17 2022" -A x64 -DCMAKE_BUILD_TYPE=Release
if %ERRORLEVEL% NEQ 0 (
    echo ERROR: CMake configuration failed
    cd ..\..\..
    exit /b 1
)

REM 编译
echo Building Simple extension...
cmake --build . --config Release
if %ERRORLEVEL% NEQ 0 (
    echo ERROR: Build failed
    cd ..\..\..
    exit /b 1
)

cd ..\..\..

REM 查找生成的 DLL
echo.
echo Looking for simple.dll...
if exist "third_party\simple\build\Release\simple.dll" (
    echo Found: third_party\simple\build\Release\simple.dll
) else if exist "third_party\simple\build\libsimple\Release\simple.dll" (
    echo Found: third_party\simple\build\libsimple\Release\simple.dll
) else (
    echo WARNING: simple.dll not found in expected locations
    echo Please check third_party\simple\build directory manually
)

echo.
echo ====================================
echo Simple build completed!
echo ====================================
echo.
echo Next steps:
echo 1. Locate simple.dll in third_party\simple\build
echo 2. Copy it to your client build output directory
echo 3. Also copy the jieba dictionary files from third_party\simple\dict
echo.

pause
