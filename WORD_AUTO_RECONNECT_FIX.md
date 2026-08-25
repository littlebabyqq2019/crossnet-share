# Word文档预览自动重连修复方案

## 问题描述

应用程序运行一段时间后，预览Word文档会失败并报错：
- 客户端错误：`Failed to access Microsoft Word Documents collection`
- 服务端日志：`WARNING: QAxBase: Error calling IDispatch member Documents: Unknown error`

## 根本原因

Microsoft Word COM对象在长时间运行后，Documents集合接口可能会失效，导致IDispatch调用失败。这是Word COM自动化的已知问题，可能由以下原因触发：
- Word应用程序长时间保持打开状态
- 处理大量文档后的内存累积
- COM对象状态不一致
- 系统资源压力

## 解决方案：自动重连机制

### 实现内容

#### 1. 新增辅助函数（document_converter.h）

```cpp
#ifdef Q_OS_WIN
    static bool reinitializeWord();  // 重新初始化Word实例
    static bool isWordHealthy();     // 检查Word健康状态
#endif
```

#### 2. reinitializeWord() - 重新初始化Word

- 安全关闭旧的Word实例
- 创建新的Word.Application COM对象
- 设置Visible=false和DisplayAlerts=0
- 返回初始化是否成功

#### 3. isWordHealthy() - 健康检查

- 检查wordApp是否存在且有效
- 尝试访问Documents集合
- 返回Word是否处于健康状态

#### 4. convertWordWithMicrosoftWord() - 添加重试机制

**重试逻辑：**
- 最多尝试2次转换
- 第1次失败后，第2次尝试前检查Word健康状态
- 如果Word不健康，自动重新初始化
- 在每个可能失败的步骤都支持重试：
  - Documents集合访问失败
  - 文档打开失败
  - PDF导出失败
  - PDF文件生成失败

**关键改进：**
- 添加try-catch捕获ExportAsFixedFormat异常
- 每次循环使用新的临时目录
- 正确管理QAxObject的内存（delete documents）
- 详细的日志输出用于调试

#### 5. convertWordToJpg() - 添加健康检查

- 在访问Documents前检查Word健康状态
- 如果不健康，先重新初始化
- Documents访问失败时尝试重新初始化一次

## 技术特点

### 1. 透明恢复
- 对调用者完全透明
- 不需要修改上层代码
- 用户体验无感知

### 2. 性能优化
- 只在出现问题时才重启Word
- 第一次尝试不做额外检查
- 避免不必要的性能开销

### 3. 线程安全
- 使用现有的wordMutex保护
- 所有Word操作都在锁保护下进行

### 4. 详细日志
- 记录每次重试尝试
- 输出Word健康检查结果
- 便于问题诊断

## 测试建议

### 1. 正常场景测试
- 启动服务器
- 预览多个Word文档
- 验证正常工作

### 2. 长时间运行测试
- 让服务器运行数小时
- 定期预览Word文档
- 观察是否仍会出现错误

### 3. 压力测试
- 连续预览大量Word文档
- 预览大尺寸Word文档
- 模拟高并发场景

### 4. 恢复测试
- 手动终止Word进程（模拟COM失效）
- 尝试预览Word文档
- 验证是否自动恢复

## 预期效果

1. **自动恢复**：当Documents集合失效时，自动重新初始化Word
2. **提高可靠性**：从偶发性失败恢复，无需重启服务器
3. **更好的日志**：详细记录问题和恢复过程
4. **向后兼容**：不影响现有功能，纯改进

## 修改文件

- `server/document_converter.h` - 添加辅助函数声明
- `server/document_converter.cpp` - 实现自动重连逻辑

## 备注

如果问题仍然频繁出现，可以考虑以下进一步优化：

1. **定期重启策略**：在处理N个文档后主动重启Word
2. **超时机制**：为Word操作添加超时检测
3. **降级策略**：Word多次失败后直接使用LibreOffice
4. **监控统计**：记录Word失败和恢复的次数
