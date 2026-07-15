# CrossNetShare 项目总览

## 项目结构

```
crossnet-share/
│
├── CMakeLists.txt              # 主构建配置文件
├── README.md                   # 项目说明文档（完整版）
├── QUICKSTART.md               # 快速开始指南
├── BUILD.md                    # 详细构建说明
│
├── common/                     # 公共模块（服务器和客户端共享）
│   ├── message.h               # 消息类型和数据结构定义
│   ├── protocol.h              # 通信协议接口
│   ├── protocol.cpp            # 通信协议实现（JSON序列化/反序列化）
│   ├── file_utils.h            # 文件工具类接口
│   └── file_utils.cpp          # 文件工具实现（扫描、读写、日期过滤）
│
├── server/                     # 服务器端（运行在A电脑）
│   ├── main.cpp                # 服务器入口
│   ├── server.h                # 服务器核心接口
│   ├── server.cpp              # 服务器实现（监听、连接管理）
│   ├── file_indexer.h          # 文件索引器接口
│   ├── file_indexer.cpp        # 文件索引实现（扫描、缓存、查询）
│   ├── client_handler.h        # 客户端连接处理器接口
│   ├── client_handler.cpp      # 客户端处理实现（消息处理、文件传输）
│   └── ui/
│       ├── main_window.h       # 服务器GUI接口
│       └── main_window.cpp     # 服务器GUI实现（配置、日志、客户端列表）
│
└── client/                     # 客户端（运行在B和C电脑）
    ├── main.cpp                # 客户端入口
    ├── client.h                # 客户端核心接口
    ├── client.cpp              # 客户端实现（连接、消息收发）
    ├── file_manager.h          # 文件管理器接口
    ├── file_manager.cpp        # 文件管理实现（下载、上传逻辑）
    └── ui/
        ├── main_window.h       # 客户端GUI接口
        └── main_window.cpp     # 客户端GUI实现（浏览、下载、上传界面）
```

## 代码统计

| 模块 | 文件数 | 行数（估算） |
|------|--------|-------------|
| 公共模块 | 4 | ~400 |
| 服务器端 | 8 | ~800 |
| 客户端 | 6 | ~700 |
| 文档 | 3 | ~1500 |
| **总计** | **21** | **~3400** |

## 模块说明

### 1. 公共模块 (common/)

**职责**：提供服务器和客户端共享的基础功能

#### message.h
- 定义所有消息类型枚举
- 定义数据结构：FileMetadata, DateFilter, TransferProgress
- 消息类型包括：注册、文件列表、下载、上传、批量传输等

#### protocol.h/cpp
- 消息序列化和反序列化（JSON ↔ C++ 对象）
- 使用 nlohmann/json 库
- 支持文件元数据、日期过滤器、进度信息的转换
- 时间格式转换工具函数

#### file_utils.h/cpp
- 目录扫描（递归遍历）
- 日期过滤（按创建时间筛选文件）
- 文件读写（支持大文件）
- 路径处理（规范化、拼接）
- 获取文件属性（大小、创建时间、修改时间）

### 2. 服务器端 (server/)

**职责**：作为中间代理，管理客户端连接和文件索引

#### server.h/cpp
- 多网卡监听（QTcpServer）
- 管理多个客户端连接
- 配置管理（端口、上传开关）
- 信号发射（连接、断开、日志）

#### file_indexer.h/cpp
- 维护所有客户端的文件索引
- 定期或按需刷新索引
- 支持按客户端、日期范围查询文件
- 线程安全（使用 QMutex）
- 解析文件完整路径

#### client_handler.h/cpp
- 每个客户端连接对应一个处理器
- 处理所有类型的客户端消息
- 文件传输（分块发送，64KB/块）
- 批量下载协调
- 错误处理和日志记录

#### ui/main_window.h/cpp
- 服务器配置界面（端口、上传开关）
- 显示已连接的客户端列表
- 实时日志输出
- 显示本机网卡IP地址
- 一键启动/停止服务器

### 3. 客户端 (client/)

**职责**：连接服务器，共享本地文件，下载其他客户端文件

#### client.h/cpp
- 连接管理（QTcpSocket）
- 发送各种请求（注册、文件列表、下载、上传）
- 接收和解析服务器响应
- 处理文件数据流
- 下载进度跟踪
- 批量下载状态管理

#### file_manager.h/cpp
- 高层文件操作封装
- 管理下载保存目录
- 协调批量下载流程
- 文件上传准备
- 简化客户端UI的调用接口

#### ui/main_window.h/cpp
- 服务器连接配置
- 客户端注册（ID、共享路径）
- 文件浏览（树形显示）
- 日期筛选器（起止日期）
- 下载区域（保存路径、进度条）
- 上传功能（文件选择）
- 实时日志显示

## 核心流程

### 1. 启动流程

```
[A电脑] 启动服务器
  ↓
  监听 0.0.0.0:8888（所有网卡）
  ↓
  等待客户端连接

[B电脑] 启动客户端
  ↓
  连接 192.168.1.100:8888
  ↓
  注册为 "ClientB"，共享 D:\SharedFiles
  ↓
  服务器扫描 B 的共享目录，建立索引

[C电脑] 启动客户端
  ↓
  连接 10.28.168.100:8888
  ↓
  注册为 "ClientC"，共享 E:\MyFiles
  ↓
  服务器扫描 C 的共享目录，建立索引
```

### 2. 文件浏览流程

```
[B电脑] 选择日期范围：2024-01-01 ~ 2024-01-31
  ↓
[B电脑] 发送 REQUEST_FILTERED_FILES
  {
    "filter": {
      "startDate": "2024-01-01 00:00:00",
      "endDate": "2024-01-31 23:59:59"
    },
    "targetClient": "ClientC"
  }
  ↓
[服务器] 从索引中筛选符合条件的文件
  ↓
[服务器] 返回 FILTERED_FILES_RESPONSE
  {
    "files": [
      {
        "filename": "report.docx",
        "size": 102400,
        "createTime": "2024-01-15 10:30:00",
        "ownerClient": "ClientC"
      },
      ...
    ]
  }
  ↓
[B电脑] 在界面显示文件列表
```

### 3. 批量下载流程

```
[B电脑] 点击 "Download Selected"
  ↓
[B电脑] 发送 BATCH_DOWNLOAD_REQUEST
  ↓
[服务器] 返回 BATCH_DOWNLOAD_RESPONSE
  {
    "totalFiles": 100
  }
  ↓
[服务器] 对每个文件：
  ├─ 发送 DOWNLOAD_RESPONSE（文件元数据）
  ├─ 分块发送 FILE_DATA（64KB/块）
  └─ 发送 FILE_COMPLETE
  ↓
[B电脑] 接收数据块，组装文件，保存到本地
  ↓
[B电脑] 更新进度条（已下载 50/100 文件）
  ↓
[B电脑] 全部完成，显示 "Download complete: 100/100 files"
```

### 4. 上传流程

```
[C电脑] 选择文件：E:\report.pdf
  ↓
[C电脑] 读取文件内容，Base64编码
  ↓
[C电脑] 发送 UPLOAD_REQUEST
  {
    "metadata": {
      "filename": "report.pdf",
      "size": 204800,
      ...
    },
    "data": "<base64 encoded data>"
  }
  ↓
[服务器] 保存到 C 的共享目录
  ↓
[服务器] 刷新 C 的文件索引
  ↓
[服务器] 返回 UPLOAD_RESPONSE
  {
    "success": true,
    "message": "Upload successful"
  }
  ↓
[C电脑] 显示 "Upload complete"
```

## 关键技术点

### 1. 网络通信

- **协议**：TCP（可靠传输）
- **框架**：Qt Network（QTcpServer, QTcpSocket）
- **消息格式**：[4字节长度] + [JSON数据]
- **线程模型**：每个客户端连接独立处理

### 2. 数据序列化

- **库**：nlohmann/json（header-only）
- **格式**：UTF-8 JSON
- **二进制数据**：Base64 编码
- **日期时间**：ISO 8601 格式字符串

### 3. 文件传输

- **分块大小**：64KB
- **传输方式**：同步（阻塞式）
- **断点续传**：不支持（未来可扩展）
- **校验**：无（未来可添加 MD5/SHA256）

### 4. 并发控制

- **服务器**：多线程（Qt 事件循环）
- **文件索引**：QMutex 保护共享数据
- **客户端**：单线程（GUI 线程）

### 5. GUI 框架

- **工具包**：Qt Widgets
- **布局**：VBoxLayout, HBoxLayout, QGroupBox
- **控件**：QLineEdit, QSpinBox, QPushButton, QTreeWidget, QProgressBar
- **信号槽**：异步事件驱动

## 扩展性设计

### 可扩展点

1. **新消息类型**：在 `message.h` 添加枚举，在处理器中实现逻辑
2. **新筛选条件**：扩展 `FileMetadata`，修改 `file_indexer`
3. **加密传输**：替换 `QTcpSocket` 为 `QSslSocket`
4. **认证机制**：在注册消息中添加密码字段
5. **权限控制**：在 `FileIndexer` 中添加权限检查
6. **文件校验**：在 `FILE_COMPLETE` 中添加哈希值
7. **断点续传**：在 `DownloadState` 中添加偏移量

### 未来功能

- [ ] 文件删除/重命名操作
- [ ] 实时文件监控（文件系统变化通知）
- [ ] 文件版本历史
- [ ] 文件搜索（全文搜索）
- [ ] 用户群组和权限管理
- [ ] WebDAV 协议支持
- [ ] 跨平台支持（Linux, macOS）
- [ ] 分布式文件存储
- [ ] P2P 直连优化

## 性能考虑

### 瓶颈分析

1. **A电脑带宽**：所有数据经过 A，带宽 × 2
2. **文件索引刷新**：大量文件时扫描慢
3. **JSON 序列化**：大文件列表时较慢
4. **Base64 编码**：增加 33% 数据量

### 优化方向

1. **增量索引**：只扫描变化的文件
2. **索引缓存**：持久化到磁盘
3. **二进制协议**：替换 JSON（Protocol Buffers）
4. **压缩传输**：gzip/zstd 压缩
5. **多线程下载**：并行下载多个文件
6. **内存池**：减少频繁分配

## 安全建议

### 当前版本风险

- ⚠️ 无加密，数据可被嗅探
- ⚠️ 无认证，任何人可连接
- ⚠️ 无权限，可访问所有文件
- ⚠️ 路径遍历风险（未验证相对路径）

### 安全增强

1. **传输加密**：TLS 1.3
2. **身份认证**：客户端证书或密码
3. **访问控制**：ACL（访问控制列表）
4. **输入验证**：严格验证文件路径
5. **审计日志**：记录所有操作
6. **速率限制**：防止 DoS 攻击

## 测试建议

### 单元测试

- `Protocol` 类：序列化/反序列化正确性
- `FileUtils` 类：文件操作边界条件
- `FileIndexer` 类：查询逻辑准确性

### 集成测试

- 客户端注册流程
- 文件列表请求
- 单文件下载
- 批量下载
- 文件上传

### 性能测试

- 1000 个小文件批量下载
- 100MB 大文件传输
- 10 个客户端同时连接
- 索引刷新时间（10000 个文件）

### 压力测试

- 持续运行 24 小时
- 频繁连接断开
- 并发下载
- 内存泄漏检测

## 开发工具推荐

- **IDE**：Qt Creator（推荐）, Visual Studio, CLion
- **调试**：Qt Creator 内置调试器, WinDbg
- **分析**：Visual Studio Profiler, Qt Creator 性能分析器
- **网络抓包**：Wireshark
- **版本控制**：Git
- **代码格式化**：clang-format

## 贡献指南

1. Fork 项目
2. 创建功能分支
3. 编写代码和测试
4. 提交 Pull Request
5. 代码审查
6. 合并到主分支

## 许可证

开源项目，供学习和研究使用。

---

**项目版本**：1.0.0  
**最后更新**：2026-07-16  
**维护者**：CrossNetShare 开发团队
