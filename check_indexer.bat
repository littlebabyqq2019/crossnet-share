@echo off
echo ========================================
echo CrossNetShare 索引器诊断工具
echo ========================================
echo.

echo [1] 检查 Python 安装
echo ----------------------------------------
python --version 2>nul
if %errorlevel% neq 0 (
    echo [X] Python 未安装或不在 PATH 中
    python3 --version 2>nul
    if %errorlevel% neq 0 (
        echo [X] python3 也未找到
        echo [!] 请安装 Python: https://www.python.org/downloads/
        goto :check_scripts
    ) else (
        echo [OK] 找到 python3
    )
) else (
    echo [OK] 找到 python
)
echo.

echo [2] 检查 Python 包
echo ----------------------------------------
python -m pip list 2>nul | findstr -i "pypdf2 pdfplumber aspose"
if %errorlevel% neq 0 (
    echo [X] 未找到必需的 Python 包
    echo.
    echo [!] 请安装以下包:
    echo     pip install PyPDF2
    echo     pip install pdfplumber
) else (
    echo [OK] 找到相关包
)
echo.

:check_scripts
echo [3] 检查提取脚本
echo ----------------------------------------
if exist "extract_pdf_text.py" (
    echo [OK] extract_pdf_text.py 存在
) else (
    echo [X] extract_pdf_text.py 不存在
)

if exist "extract_word_text.py" (
    echo [OK] extract_word_text.py 存在
) else (
    echo [X] extract_word_text.py 不存在
)
echo.

echo [4] 检查 SQL 驱动
echo ----------------------------------------
if exist "sqldrivers\qsqlite.dll" (
    echo [OK] qsqlite.dll 存在
) else (
    echo [X] qsqlite.dll 不存在
)
echo.

echo [5] 测试 TXT 文件提取
echo ----------------------------------------
echo 测试内容 > test_sample.txt
if exist "test_sample.txt" (
    echo [OK] 创建了测试文件 test_sample.txt
    type test_sample.txt
    del test_sample.txt
)
echo.

echo ========================================
echo 诊断完成
echo ========================================
echo.
echo 如果发现问题，请按照提示安装缺失的组件
echo.
pause
