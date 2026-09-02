# 索引数据库诊断脚本
# 用于检查 content_index.db 的状态和内容

param(
    [string]$DbPath = ".\content_index.db"
)

Write-Host "======================================" -ForegroundColor Cyan
Write-Host "索引数据库诊断工具" -ForegroundColor Cyan
Write-Host "======================================" -ForegroundColor Cyan
Write-Host ""

# 检查 sqlite3.exe 是否存在
$sqlite3 = $null
$possiblePaths = @(
    "sqlite3.exe",
    "C:\Program Files\SQLite\sqlite3.exe",
    "C:\sqlite\sqlite3.exe"
)

foreach ($path in $possiblePaths) {
    if (Get-Command $path -ErrorAction SilentlyContinue) {
        $sqlite3 = $path
        break
    }
}

if (-not $sqlite3) {
    Write-Host "❌ 未找到 sqlite3.exe" -ForegroundColor Red
    Write-Host ""
    Write-Host "请下载 SQLite 命令行工具：" -ForegroundColor Yellow
    Write-Host "https://www.sqlite.org/download.html" -ForegroundColor Yellow
    Write-Host ""
    Write-Host "下载 'sqlite-tools-win32-x86-*.zip' 并解压到系统 PATH 中" -ForegroundColor Yellow
    exit 1
}

Write-Host "✓ 找到 SQLite: $sqlite3" -ForegroundColor Green
Write-Host ""

# 检查数据库文件
if (-not (Test-Path $DbPath)) {
    Write-Host "❌ 数据库文件不存在: $DbPath" -ForegroundColor Red
    exit 1
}

Write-Host "✓ 数据库文件存在: $DbPath" -ForegroundColor Green
$fileInfo = Get-Item $DbPath
Write-Host "  大小: $([math]::Round($fileInfo.Length / 1MB, 2)) MB" -ForegroundColor Gray
Write-Host "  修改时间: $($fileInfo.LastWriteTime)" -ForegroundColor Gray
Write-Host ""

# 执行 SQL 查询
function Run-SQLQuery {
    param([string]$Query)
    
    & $sqlite3 $DbPath $Query 2>&1
}

# 1. 检查表结构
Write-Host "1. 检查表结构..." -ForegroundColor Cyan
$tables = Run-SQLQuery ".tables"
Write-Host "  表: $tables" -ForegroundColor Gray
Write-Host ""

# 2. 统计文件数量
Write-Host "2. 文件统计..." -ForegroundColor Cyan
$fileCount = Run-SQLQuery "SELECT COUNT(*) FROM files;"
Write-Host "  已索引文件数: $fileCount" -ForegroundColor Gray

# 3. 统计 FTS 索引数量
$ftsCount = Run-SQLQuery "SELECT COUNT(*) FROM files_fts;"
Write-Host "  FTS 索引数: $ftsCount" -ForegroundColor Gray
Write-Host ""

# 4. 显示前 10 个文件
Write-Host "3. 前 10 个已索引文件..." -ForegroundColor Cyan
$sampleFiles = Run-SQLQuery "SELECT file_id, file_path, file_name FROM files LIMIT 10;"
$sampleFiles | ForEach-Object {
    Write-Host "  $_" -ForegroundColor Gray
}
Write-Host ""

# 5. 检查文件路径格式
Write-Host "4. 文件路径示例..." -ForegroundColor Cyan
$pathSamples = Run-SQLQuery "SELECT DISTINCT substr(file_path, 1, 20) || '...' FROM files LIMIT 5;"
$pathSamples | ForEach-Object {
    Write-Host "  $_" -ForegroundColor Gray
}
Write-Host ""

# 6. 检查哈希值
Write-Host "5. 哈希值示例（前3个文件）..." -ForegroundColor Cyan
$hashSamples = Run-SQLQuery "SELECT file_name, content_hash FROM files LIMIT 3;"
$hashSamples | ForEach-Object {
    Write-Host "  $_" -ForegroundColor Gray
}
Write-Host ""

# 7. 检查索引时间
Write-Host "6. 索引时间信息..." -ForegroundColor Cyan
$firstIndexed = Run-SQLQuery "SELECT datetime(MIN(indexed_time), 'unixepoch', 'localtime') FROM files;"
$lastIndexed = Run-SQLQuery "SELECT datetime(MAX(indexed_time), 'unixepoch', 'localtime') FROM files;"
Write-Host "  首次索引: $firstIndexed" -ForegroundColor Gray
Write-Host "  最后索引: $lastIndexed" -ForegroundColor Gray
Write-Host ""

# 8. 检查文件类型分布
Write-Host "7. 文件类型分布..." -ForegroundColor Cyan
$fileTypes = Run-SQLQuery "SELECT file_type, COUNT(*) as count FROM files GROUP BY file_type;"
$fileTypes | ForEach-Object {
    Write-Host "  $_" -ForegroundColor Gray
}
Write-Host ""

# 9. 文件大小统计
Write-Host "8. 文件大小统计..." -ForegroundColor Cyan
$avgSize = Run-SQLQuery "SELECT AVG(file_size) / 1024 FROM files;"
$totalSize = Run-SQLQuery "SELECT SUM(file_size) / 1024 / 1024 FROM files;"
Write-Host "  平均文件大小: $([math]::Round([double]$avgSize, 2)) KB" -ForegroundColor Gray
Write-Host "  总文件大小: $([math]::Round([double]$totalSize, 2)) MB" -ForegroundColor Gray
Write-Host ""

# 10. 检查特定文件是否已索引
Write-Host "9. 测试查询..." -ForegroundColor Cyan
Write-Host "  请输入一个文件名来检查是否已索引（直接回车跳过）：" -ForegroundColor Yellow
$testFile = Read-Host "  文件名"
if ($testFile) {
    $result = Run-SQLQuery "SELECT file_id, file_path, content_hash FROM files WHERE file_name LIKE '%$testFile%' LIMIT 5;"
    if ($result) {
        Write-Host "  找到匹配的文件：" -ForegroundColor Green
        $result | ForEach-Object {
            Write-Host "    $_" -ForegroundColor Gray
        }
    } else {
        Write-Host "  未找到匹配的文件" -ForegroundColor Yellow
    }
}
Write-Host ""

Write-Host "======================================" -ForegroundColor Cyan
Write-Host "诊断完成" -ForegroundColor Cyan
Write-Host "======================================" -ForegroundColor Cyan
