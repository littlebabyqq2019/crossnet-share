# 构建说明

## 系统要求

### 开发环境
- **操作系统**：Windows 10/11（64位）
- **编译器**：
  - MSVC 2017 或更高版本（推荐 MSVC 2019）
  - 或 MinGW-w64 with GCC 7+
- **CMake**：3.15 或更高版本
- **Qt**：5.12 或更高版本（推荐 5.15.2）

### 依赖库
- Qt5 Core
- Qt5 Widgets
- Qt5 Network
- nlohmann/json（自动通过 CMake FetchContent 下载）

## 详细构建步骤

### 1. 安装 Qt5

#### 方法A：官方在线安装器（推荐）
1. 访问 [https://www.qt.io/download](https://www.qt.io/download)
2. 下载 Qt Online Installer
3. 安装时选择：
   - Qt 5.15.2
   - MSVC 2019 64-bit（或您的编译器对应版本）
   - Qt Creator（可选）

#### 方法B：离线安装包
1. 下载 Qt 5.15.2 离线安装包
2. 安装到指定目录（例如：`C:\Qt\5.15.2`）

### 2. 安装 CMake

#### 方法A：通过 Chocolatey
```powershell
choco install cmake
```

#### 方法B：官方安装器
1. 访问 [https://cmake.org/download/](https://cmake.org/download/)
2. 下载 Windows x64 Installer
3. 安装时勾选 "Add CMake to system PATH"

### 3. 配置环境变量

```powershell
# 设置 Qt 路径（替换为您的实际路径）
$env:CMAKE_PREFIX_PATH = "C:\Qt\5.15.2\msvc2019_64"

# 或者永久设置
[System.Environment]::SetEnvironmentVariable("CMAKE_PREFIX_PATH", "C:\Qt\5.15.2\msvc2019_64", "User")
```

### 4. 克隆或解压项目

```powershell
cd e:\dev
# 假设项目已在 e:\dev\crossnet-share
```

### 5. 构建项目

#### 使用 Visual Studio（推荐）

```powershell
# 进入项目目录
cd e:\dev\crossnet-share

# 创建构建目录
mkdir build
cd build

# 生成 Visual Studio 解决方案
cmake .. -G "Visual Studio 16 2019" -A x64 -DCMAKE_PREFIX_PATH="C:\Qt\5.15.2\msvc2019_64"

# 编译（Debug 模式）
cmake --build . --config Debug

# 编译（Release 模式，推荐）
cmake --build . --config Release
```

#### 使用 Qt Creator

1. 启动 Qt Creator
2. 打开项目：File → Open File or Project
3. 选择 `e:\dev\crossnet-share\CMakeLists.txt`
4. 配置 Kit：
   - 编译器：MSVC 2019 (amd64)
   - Qt 版本：Qt 5.15.2 MSVC2019 64bit
   - CMake：默认
5. 点击 "Configure Project"
6. 选择构建模式（Debug 或 Release）
7. 点击左下角的"锤子"图标构建

#### 使用 Ninja（更快的构建速度）

```powershell
# 安装 Ninja
choco install ninja

# 配置项目
cmake .. -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="C:\Qt\5.15.2\msvc2019_64"

# 编译
ninja
```

### 6. 构建输出

构建成功后，可执行文件位于：

```
build/
└── bin/
    ├── CrossNetShareServer.exe    # 服务器程序
    └── CrossNetShareClient.exe    # 客户端程序
```

## 打包发布

### 1. 收集依赖的 Qt DLL

```powershell
# 进入构建目录
cd build\bin

# 使用 Qt 的 windeployqt 工具
# 替换为您的 Qt 安装路径
C:\Qt\5.15.2\msvc2019_64\bin\windeployqt.exe --release CrossNetShareServer.exe
C:\Qt\5.15.2\msvc2019_64\bin\windeployqt.exe --release CrossNetShareClient.exe
```

此命令会自动复制所需的 Qt DLL 到 bin 目录。

### 2. 创建发布包

```powershell
# 创建发布目录
mkdir CrossNetShare-Release
cd CrossNetShare-Release

# 复制服务器
mkdir Server
Copy-Item ..\build\bin\CrossNetShareServer.exe Server\
Copy-Item ..\build\bin\*.dll Server\
Copy-Item ..\build\bin\platforms Server\ -Recurse
Copy-Item ..\build\bin\styles Server\ -Recurse

# 复制客户端
mkdir Client
Copy-Item ..\build\bin\CrossNetShareClient.exe Client\
Copy-Item ..\build\bin\*.dll Client\
Copy-Item ..\build\bin\platforms Client\ -Recurse
Copy-Item ..\build\bin\styles Client\ -Recurse

# 复制文档
Copy-Item ..\README.md .
Copy-Item ..\QUICKSTART.md .
```

### 3. 压缩发布

```powershell
Compress-Archive -Path CrossNetShare-Release -DestinationPath CrossNetShare-v1.0.0.zip
```

## 构建配置选项

### CMake 配置选项

```powershell
# 指定构建类型
cmake .. -DCMAKE_BUILD_TYPE=Release    # Release | Debug | RelWithDebInfo

# 指定 Qt 路径
cmake .. -DCMAKE_PREFIX_PATH="C:\Qt\5.15.2\msvc2019_64"

# 禁用控制台窗口（GUI 模式）
# 已在 CMakeLists.txt 中配置，无需额外参数
```

### 编译器标志

在 `CMakeLists.txt` 中已设置：
- C++17 标准
- 多线程编译（MSVC）
- 警告级别

## 常见构建问题

### 问题1：找不到 Qt5

**错误信息**：
```
CMake Error at CMakeLists.txt:14 (find_package):
  Could not find a package configuration file provided by "Qt5"
```

**解决方案**：
```powershell
# 确保设置了 CMAKE_PREFIX_PATH
cmake .. -DCMAKE_PREFIX_PATH="C:\Qt\5.15.2\msvc2019_64"
```

### 问题2：nlohmann/json 下载失败

**错误信息**：
```
CMake Error: failed to download json.tar.xz
```

**解决方案**：
1. 检查网络连接
2. 手动下载：[https://github.com/nlohmann/json/releases](https://github.com/nlohmann/json/releases)
3. 修改 CMakeLists.txt 使用本地文件：
```cmake
FetchContent_Declare(
    json
    URL "file:///path/to/json.tar.xz"
)
```

### 问题3：编译器版本不兼容

**错误信息**：
```
error: this version requires C++17
```

**解决方案**：
- 更新编译器到支持 C++17 的版本
- MSVC 2017+ 或 GCC 7+ 或 Clang 5+

### 问题4：链接错误

**错误信息**：
```
unresolved external symbol
```

**解决方案**：
- 确保 Qt 版本与编译器匹配
- 检查是否使用了正确的 Qt Kit
- 清理构建目录重新构建：
```powershell
Remove-Item -Recurse -Force build
mkdir build
cd build
cmake ..
```

## 开发模式构建

### Debug 构建
```powershell
cmake .. -DCMAKE_BUILD_TYPE=Debug
cmake --build . --config Debug
```

Debug 模式特点：
- 包含调试符号
- 未优化
- 可以使用调试器
- 程序运行较慢

### Release 构建
```powershell
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build . --config Release
```

Release 模式特点：
- 完全优化
- 无调试符号
- 程序运行快
- 文件体积小

## 增量构建

修改代码后，只需重新编译：

```powershell
cd build

# 使用 CMake
cmake --build . --config Release

# 或在 Visual Studio 中按 F7
# 或在 Qt Creator 中按 Ctrl+B
```

## 清理构建

```powershell
# 完全清理
cd build
cmake --build . --target clean

# 或删除整个构建目录
cd ..
Remove-Item -Recurse -Force build
```

## 持续集成（CI）

### GitHub Actions 示例

创建 `.github/workflows/build.yml`：

```yaml
name: Build

on: [push, pull_request]

jobs:
  build-windows:
    runs-on: windows-latest
    
    steps:
    - uses: actions/checkout@v2
    
    - name: Install Qt
      uses: jurplel/install-qt-action@v3
      with:
        version: '5.15.2'
        arch: 'win64_msvc2019_64'
    
    - name: Configure CMake
      run: cmake -B build -DCMAKE_BUILD_TYPE=Release
    
    - name: Build
      run: cmake --build build --config Release
    
    - name: Upload artifacts
      uses: actions/upload-artifact@v2
      with:
        name: CrossNetShare-Windows
        path: build/bin/*.exe
```

## 性能优化构建

### 启用链接时优化（LTO）

在 `CMakeLists.txt` 中添加：

```cmake
if(MSVC)
    set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} /GL")
    set(CMAKE_EXE_LINKER_FLAGS_RELEASE "${CMAKE_EXE_LINKER_FLAGS_RELEASE} /LTCG")
endif()
```

### 减小可执行文件大小

```cmake
if(MSVC)
    set(CMAKE_EXE_LINKER_FLAGS_RELEASE "${CMAKE_EXE_LINKER_FLAGS_RELEASE} /OPT:REF /OPT:ICF")
endif()
```

## 技术支持

如遇构建问题：
1. 检查本文档的常见问题部分
2. 确认环境配置正确
3. 查看 CMake 输出的详细错误信息
4. 提交 Issue 附上完整的构建日志

---

**最后更新**：2026-07-16
