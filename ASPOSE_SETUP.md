# Aspose.Words 安装说明

CrossNetShare 使用 Aspose.Words for Python 作为主要的 Word 文档转 PDF 引擎。

## 优势

相比 Microsoft Word COM 方式：
- ✅ **更稳定** - 不依赖 Word 进程，无需处理 COM 对象失效
- ✅ **更快速** - 转换速度更快，启动时间更短
- ✅ **更可靠** - 无需担心 Word 长时间运行后的问题
- ✅ **跨平台** - 支持 Windows/Linux/macOS（虽然目前只用 Windows）
- ✅ **不需要安装 Word** - 无需购买和安装 Microsoft Office

## 安装步骤

### 1. 安装 Python

如果尚未安装 Python，请下载并安装 Python 3.8 或更高版本：

**Windows:**
- 访问：https://www.python.org/downloads/
- 下载 Python 3.11 或 3.12（推荐）
- 安装时勾选 "Add Python to PATH"

**验证安装:**
```cmd
python --version
```

### 2. 安装 Aspose.Words

使用提供的 whl 包安装：

```cmd
cd Aspose.Words\python专用whl包
pip install aspose_words-25.9.0-py3-none-win_amd64.whl --force-reinstall
```

或者使用 pip 直接安装（如果你有网络访问）：
```cmd
pip install aspose-words
```

**验证安装:**
```cmd
python -c "import aspose.words as aw; print('Aspose.Words installed successfully')"
```

### 3. 许可证配置

程序会自动在以下位置查找许可证文件 `1.lic`：
1. 程序目录（与 CrossNetShareServer.exe 同级）
2. `Aspose.Words/python专用whl包/1.lic`（开发时）

**注意：** 如果没有许可证文件，Aspose.Words 会以评估模式运行，生成的 PDF 会有水印。

## 备选方案

如果 Aspose.Words 不可用，程序会自动降级到备选方案：

1. **Microsoft Word COM** - 如果安装了 Microsoft Office Word
2. **LibreOffice** - 如果安装了 LibreOffice

## 故障排除

### Python 未找到

**错误:** `Python not found. Please install Python 3.8 or later.`

**解决:** 
1. 确保已安装 Python
2. 确保 Python 在系统 PATH 中
3. 重启应用程序

### Aspose.Words 模块未找到

**错误:** `ModuleNotFoundError: No module named 'aspose'`

**解决:**
```cmd
pip install aspose_words-25.9.0-py3-none-win_amd64.whl --force-reinstall
```

### 许可证问题

**现象:** PDF 生成成功但有水印

**原因:** 运行在评估模式

**解决:** 将 `1.lic` 文件放在程序目录中

## 性能对比

| 方案 | 启动时间 | 转换速度 | 稳定性 | 依赖 |
|------|---------|---------|--------|------|
| **Aspose.Words** | 快 (< 1s) | 快 | 优秀 | Python + whl包 |
| Microsoft Word | 慢 (3-5s) | 中等 | 中等 | Microsoft Office |
| LibreOffice | 慢 (2-4s) | 慢 | 良好 | LibreOffice |

## 技术细节

- 脚本位置：`server/word_to_pdf.py`
- 转换方法：调用 Python 子进程执行转换
- 超时设置：60 秒
- 缓存机制：转换结果会被缓存，避免重复转换
