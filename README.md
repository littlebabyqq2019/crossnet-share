# CrossNetShare - 跨网段文件共享系统

## 项目简介

CrossNetShare 是一个专为跨网段文件共享设计的 C++ 应用程序。它允许在不同网段的电脑之间通过中间服务器进行文件传输和同步。

### 应用场景

- **A电脑**：拥有两个网卡，分别连接到两个不同的网段
  - 网卡1：连接到 192.168.1.X 网段（与 B电脑 同网段）
  - 网卡2：连接到 10.28.168.X 网段（与 C电脑 同网段）
- **B电脑** 和 **C电脑**：分别在不同网段，无法直接通信
- **目标**：通过在 A电脑 上运行服务器，使 B 和 C 可以互相共享文件

## 核心特性

### 服务器端（运行在A电脑）
- ✅ 多网卡监听（同时监听多个网络接口）
- ✅ 客户端连接管理
- ✅ 文件索引和元数据管理
- ✅ 按日期范围筛选文件
- ✅ 批量文件传输
- ✅ 可配置的上传功能（支持开启/关闭）
- ✅ 图形化管理界面

### 客户端（运行在B和C电脑）
- ✅ 连接到服务器并注册
- ✅ 共享本地目录
- ✅ 浏览其他客户端的共享文件
- ✅ 按日期筛选文件列表
- ✅ 批量下载符合条件的文件
- ✅ 上传文件到服务器
- ✅ 实时下载进度显示
- ✅ 图形化操作界面

## 技术架构

### 技术栈
- **语言**：C++17
- **GUI框架**：Qt5 (Widgets, Network)
- **JSON库**：nlohmann/json
- **构建系统**：CMake 3.15+
- **网络通信**：Qt Network (TCP Socket)

### 项目结构
```
crossnet-share/
├── CMakeLists.txt          # 主构建配置
├── common/                 # 公共模块
│   ├── message.h           # 消息类型定义
│   ├── protocol.h/cpp      # 通信协议实现
│   └── file_utils.h/cpp    # 文件工具类
├── server/                 # 服务器端
│   ├── main.cpp
│   ├── server.h/cpp        # 服务器核心
│   ├── file_indexer.h/cpp  # 文件索引器
│   ├── client_handler.h/cpp # 客户端处理器
│   └── ui/
│       └── main_window.h/cpp # 服务器GUI
└── client/                 # 客户端
    ├── main.cpp
    ├── client.h/cpp        # 客户端核心
    ├── file_manager.h/cpp  # 文件管理器
    └── ui/
        └── main_window.h/cpp # 客户端GUI
```

## 编译和构建

### 依赖项
- CMake 3.15 或更高版本
- Qt5 (Core, Widgets, Network)
- C++17 编译器（MSVC 2017+, GCC 7+, Clang 5+）
- nlohmann/json（通过 CMake FetchContent 自动下载）

### Windows 编译步骤

#### 方法1：使用 Visual Studio
```powershell
# 创建构建目录
mkdir build
cd build

# 配置项目（需要设置 Qt5 路径）
cmake .. -DCMAKE_PREFIX_PATH="C:/Qt/5.15.2/msvc2019_64"

# 编译
cmake --build . --config Release
```

#### 方法2：使用 Qt Creator
1. 打开 Qt Creator
2. 选择 "打开项目" 并选择 `CMakeLists.txt`
3. 配置构建套件（选择合适的 Qt 版本和编译器）
4. 点击 "构建" 按钮

### 生成的可执行文件
- `build/bin/CrossNetShareServer.exe` - 服务器程序
- `build/bin/CrossNetShareClient.exe` - 客户端程序

## 使用指南

### 1. 在A电脑上运行服务器

#### 启动服务器
1. 运行 `CrossNetShareServer.exe`
2. 在 "Server Configuration" 区域配置：
   - **Port**：设置监听端口（默认 8888）
   - **Enable Upload**：勾选以允许客户端上传文件
3. 点击 **"Start Server"** 按钮启动服务

#### 查看服务器状态
- **Status** 区域显示服务器运行状态和本机网卡IP地址
- **Connected Clients** 列表显示已连接的客户端
- **Server Log** 显示服务器日志和操作记录

#### 注意事项
- 确保防火墙允许该端口的入站连接
- 服务器会自动在所有网卡上监听（包括 192.168.1.X 和 10.28.168.X）

### 2. 在B电脑上运行客户端

#### 连接到服务器
1. 运行 `CrossNetShareClient.exe`
2. 在 "Server Connection" 区域：
   - **Server**：输入 A电脑 在 192.168.1.X 网段的IP（例如：192.168.1.100）
   - **Port**：输入服务器端口（默认 8888）
3. 点击 **"Connect"** 按钮

#### 注册客户端
1. 连接成功后，在 "Client Registration" 区域：
   - **Client ID**：输入客户端标识（例如：ClientB）
   - **Share Path**：选择要共享的文件夹
2. 点击 **"Register"** 按钮完成注册

#### 浏览和下载文件
1. 在 "Browse Files" 区域：
   - **Target Client**：选择要浏览的客户端（或选择 "All Clients"）
   - **From / To**：设置日期范围
   - 点击 **"Refresh"** 查看符合条件的文件列表

2. 在 "Download" 区域：
   - **Save to**：选择文件保存目录
   - 点击 **"Download Selected"** 下载符合日期范围的所有文件
   - 进度条显示下载进度

#### 上传文件
1. 点击 **"Upload File..."** 按钮
2. 选择要上传的文件
3. 文件会上传到服务器并添加到共享索引中

### 3. 在C电脑上运行客户端

步骤与B电脑类似，只需注意：
- **Server** 地址应填写 A电脑 在 10.28.168.X 网段的IP（例如：10.28.168.100）
- **Client ID** 使用不同的标识（例如：ClientC）

### 典型使用流程

```
┌─────────────────────────────────────────────────────────┐
│                     使用流程示例                         │
└─────────────────────────────────────────────────────────┘

1. A电脑：启动服务器（监听端口 8888）

2. B电脑：
   - 连接到 192.168.1.100:8888
   - 注册为 "ClientB"，共享目录 D:\SharedFiles
   
3. C电脑：
   - 连接到 10.28.168.100:8888
   - 注册为 "ClientC"，共享目录 E:\MyFiles
   
4. B电脑下载C的文件：
   - 选择 Target Client = "ClientC"
   - 设置日期范围：2024-01-01 到 2024-01-31
   - 点击 Refresh 查看文件列表
   - 点击 Download Selected 批量下载
   
5. C电脑上传文件给B：
   - 点击 Upload File 选择本地文件
   - 上传成功后，B电脑刷新即可看到新文件
```

## 协议设计

### 消息格式
所有消息采用 JSON 格式，包含消息类型和载荷：

```json
{
  "type": <MessageType>,
  "payload": {
    // 消息内容
  }
}
```

### 主要消息类型
- **REGISTER_CLIENT** / **REGISTER_RESPONSE**：客户端注册
- **REQUEST_FILE_LIST** / **FILE_LIST_RESPONSE**：请求文件列表
- **REQUEST_FILTERED_FILES** / **FILTERED_FILES_RESPONSE**：按日期筛选文件
- **DOWNLOAD_REQUEST** / **DOWNLOAD_RESPONSE**：下载文件
- **FILE_DATA**：文件数据块（64KB）
- **FILE_COMPLETE**：文件传输完成
- **UPLOAD_REQUEST** / **UPLOAD_RESPONSE**：上传文件
- **BATCH_DOWNLOAD_REQUEST** / **BATCH_DOWNLOAD_RESPONSE**：批量下载

### 文件传输流程

#### 下载流程
```
Client                Server                Owner Client
  |                      |                        |
  |-- DOWNLOAD_REQUEST ->|                        |
  |                      |-- 解析请求 ------------|
  |                      |<- 读取文件 ------------|
  |<- DOWNLOAD_RESPONSE -|                        |
  |<- FILE_DATA(chunk1)--|                        |
  |<- FILE_DATA(chunk2)--|                        |
  |<- FILE_DATA(...)  ---|                        |
  |<- FILE_COMPLETE -----|                        |
```

#### 批量下载流程
```
Client                Server
  |                      |
  |-- BATCH_DOWNLOAD --->|
  |<- BATCH_RESPONSE ----|  (totalFiles: 100)
  |<- DOWNLOAD_RESPONSE -|  (file 1)
  |<- FILE_DATA ---------|
  |<- FILE_COMPLETE -----|
  |<- DOWNLOAD_RESPONSE -|  (file 2)
  |<- FILE_DATA ---------|
  |<- FILE_COMPLETE -----|
  ...                   ...
```

## 性能特点

### 优化设计
- **分块传输**：文件以 64KB 块传输，支持大文件
- **多线程**：每个客户端连接独立处理
- **文件索引缓存**：避免重复扫描目录
- **日期筛选**：服务器端筛选，减少网络传输

### 适用场景
- ✅ 小文件（< 10MB），数量多（数百到数千个）
- ✅ 按日期批量下载
- ✅ 局域网环境（千兆网络）
- ⚠️ 大文件（> 100MB）传输速度受A电脑性能影响
- ⚠️ 不适合实时双向同步（单向传输为主）

## 安全考虑

### 当前版本
- ⚠️ **无加密**：数据明文传输（仅适用于内网）
- ⚠️ **无认证**：客户端可自由注册
- ⚠️ **无权限控制**：所有客户端可见所有文件

### 建议
- 仅在受信任的内部网络中使用
- 不要通过公网暴露服务器端口
- 定期检查服务器日志

### 未来增强
- [ ] TLS/SSL 加密传输
- [ ] 客户端密码认证
- [ ] 基于角色的访问控制
- [ ] 文件操作审计日志

## 常见问题

### Q1: 客户端无法连接到服务器
**解决方案**：
1. 检查服务器是否已启动
2. 确认填写的服务器IP地址正确
3. 检查防火墙是否允许该端口
4. 使用 `ping` 测试网络连通性

### Q2: 文件列表为空
**解决方案**：
1. 确认客户端已成功注册
2. 检查共享目录是否包含文件
3. 调整日期筛选范围
4. 在服务器端点击 "Refresh Index" 刷新索引

### Q3: 下载速度慢
**原因**：
- 所有数据都经过 A电脑 中转
- A电脑的CPU和网络带宽成为瓶颈

**优化建议**：
- 使用性能更好的电脑作为服务器
- 批量下载而非逐个下载
- 避免同时下载过多文件

### Q4: 上传失败
**解决方案**：
1. 检查服务器是否启用了上传功能
2. 确认有足够的磁盘空间
3. 检查文件是否被占用
4. 查看服务器日志获取详细错误信息

## 开发和扩展

### 添加新功能
项目采用模块化设计，易于扩展：

- **添加新消息类型**：在 `common/message.h` 中定义
- **修改通信协议**：修改 `common/protocol.cpp`
- **增强文件筛选**：扩展 `server/file_indexer.cpp`
- **自定义GUI**：修改 `server/ui/main_window.cpp` 或 `client/ui/main_window.cpp`

### 调试技巧
```cpp
// 在代码中添加日志
emit logMessage("Debug info: " + debugString);

// Qt 网络调试
qDebug() << socket_->state();
qDebug() << socket_->errorString();
```

## 许可证

本项目为开源项目，供学习和研究使用。

## 联系方式

如有问题或建议，请提交 Issue 或 Pull Request。

---

**版本**：1.0.0  
**更新日期**：2026-07-16
