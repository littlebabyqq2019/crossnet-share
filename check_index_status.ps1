# CrossNetShare 索引状态检查脚本

Write-Host "=== CrossNetShare 索引状态检查 ===" -ForegroundColor Cyan
Write-Host ""

# 1. 检查客户端目录
$clientDir = "$env:LOCALAPPDATA\CrossNetShareClient"
Write-Host "1. 客户端目录检查" -ForegroundColor Yellow
Write-Host "   路径: $clientDir"

if (Test-Path $clientDir) {
    Write-Host "   状态: " -NoNewline
    Write-Host "✓ 存在" -ForegroundColor Green
    
    Write-Host "`n   目录内容:"
    Get-ChildItem $clientDir | ForEach-Object {
        $size = if ($_.Length) { 
            if ($_.Length -lt 1KB) { 
                "$($_.Length) B" 
            } elseif ($_.Length -lt 1MB) { 
                "{0:N2} KB" -f ($_.Length / 1KB) 
            } else { 
                "{0:N2} MB" -f ($_.Length / 1MB) 
            }
        } else { 
            "目录" 
        }
        Write-Host "     - $($_.Name) ($size, 修改于: $($_.LastWriteTime.ToString('yyyy-MM-dd HH:mm:ss')))"
    }
} else {
    Write-Host "   状态: " -NoNewline
    Write-Host "✗ 不存在" -ForegroundColor Red
    Write-Host "   说明: 客户端还未运行或未创建配置"
}

Write-Host ""

# 2. 检查索引文件
$indexFile = "$clientDir\content_index.db"
Write-Host "2. 索引文件检查" -ForegroundColor Yellow
Write-Host "   路径: $indexFile"

if (Test-Path $indexFile) {
    $file = Get-Item $indexFile
    $sizeMB = [math]::Round($file.Length / 1MB, 2)
    Write-Host "   状态: " -NoNewline
    Write-Host "✓ 存在" -ForegroundColor Green
    Write-Host "   大小: $sizeMB MB ($($file.Length) bytes)"
    Write-Host "   创建时间: $($file.CreationTime.ToString('yyyy-MM-dd HH:mm:ss'))"
    Write-Host "   最后修改: $($file.LastWriteTime.ToString('yyyy-MM-dd HH:mm:ss'))"
    
    # 检查文件是否最近更新（表示索引正在工作）
    $minutesOld = (New-TimeSpan -Start $file.LastWriteTime -End (Get-Date)).TotalMinutes
    if ($minutesOld -lt 10) {
        Write-Host "   索引状态: " -NoNewline
        Write-Host "✓ 活跃 (最近 $([math]::Round($minutesOld, 1)) 分钟内更新)" -ForegroundColor Green
    } elseif ($minutesOld -lt 60) {
        Write-Host "   索引状态: " -NoNewline
        Write-Host "⚠ 闲置 ($([math]::Round($minutesOld, 0)) 分钟未更新)" -ForegroundColor Yellow
    } else {
        Write-Host "   索引状态: " -NoNewline
        Write-Host "⚠ 过期 ($([math]::Round($minutesOld / 60, 1)) 小时未更新)" -ForegroundColor Yellow
    }
} else {
    Write-Host "   状态: " -NoNewline
    Write-Host "✗ 不存在" -ForegroundColor Red
    Write-Host ""
    Write-Host "   可能原因:" -ForegroundColor Yellow
    Write-Host "     1. 客户端还未连接到服务器"
    Write-Host "     2. 还未设置共享路径"
    Write-Host "     3. 索引功能尚未启动"
    Write-Host "     4. 共享文件夹为空或没有支持的文件类型"
}

Write-Host ""

# 3. 检查配置文件
$configFile = "$clientDir\crossnet_client_config.json"
Write-Host "3. 配置文件检查" -ForegroundColor Yellow
Write-Host "   路径: $configFile"

if (Test-Path $configFile) {
    Write-Host "   状态: " -NoNewline
    Write-Host "✓ 存在" -ForegroundColor Green
    
    try {
        $config = Get-Content $configFile -Raw | ConvertFrom-Json
        Write-Host "   内容:"
        Write-Host "     服务器地址: $($config.serverAddress)"
        Write-Host "     服务器端口: $($config.serverPort)"
        Write-Host "     客户端名称: $($config.clientName)"
        Write-Host "     共享路径: $($config.sharePath)"
        
        # 检查共享路径是否存在
        if ($config.sharePath) {
            if (Test-Path $config.sharePath) {
                $fileCount = (Get-ChildItem $config.sharePath -Recurse -File -ErrorAction SilentlyContinue | Measure-Object).Count
                Write-Host "     路径状态: " -NoNewline
                Write-Host "✓ 有效 (共 $fileCount 个文件)" -ForegroundColor Green
            } else {
                Write-Host "     路径状态: " -NoNewline
                Write-Host "✗ 无效 (路径不存在)" -ForegroundColor Red
            }
        } else {
            Write-Host "     路径状态: " -NoNewline
            Write-Host "⚠ 未设置" -ForegroundColor Yellow
        }
    } catch {
        Write-Host "   解析错误: $_" -ForegroundColor Red
    }
} else {
    Write-Host "   状态: " -NoNewline
    Write-Host "✗ 不存在" -ForegroundColor Red
}

Write-Host ""

# 4. 检查日志文件
$logFile = "$clientDir\startup.log"
Write-Host "4. 启动日志检查" -ForegroundColor Yellow
Write-Host "   路径: $logFile"

if (Test-Path $logFile) {
    Write-Host "   状态: " -NoNewline
    Write-Host "✓ 存在" -ForegroundColor Green
    
    # 读取最后 20 行
    $lastLines = Get-Content $logFile -Tail 20
    Write-Host "`n   最后 20 行日志:"
    $lastLines | ForEach-Object {
        if ($_ -match "FileIndexer|Indexed|FTS5|SQLite") {
            Write-Host "     $_" -ForegroundColor Cyan
        } elseif ($_ -match "error|failed|ERROR|FAILED") {
            Write-Host "     $_" -ForegroundColor Red
        } else {
            Write-Host "     $_"
        }
    }
} else {
    Write-Host "   状态: " -NoNewline
    Write-Host "✗ 不存在" -ForegroundColor Yellow
}

Write-Host ""
Write-Host "=== 检查完成 ===" -ForegroundColor Cyan
Write-Host ""

# 5. 建议
Write-Host "💡 建议操作:" -ForegroundColor Green
Write-Host ""

if (Test-Path $indexFile) {
    Write-Host "✅ 索引文件存在，可以进行全文搜索测试"
    Write-Host "   1. 启动客户端和服务器"
    Write-Host "   2. 在 Web 界面 (http://localhost:8080/browse) 进行全文搜索"
    Write-Host "   3. 使用 .\test_web_search.ps1 测试 API"
} else {
    Write-Host "⚠️  索引文件不存在，需要先初始化索引"
    Write-Host "   1. 确保客户端已连接到服务器"
    Write-Host "   2. 确保已设置有效的共享路径"
    Write-Host "   3. 确保共享路径中有 .doc/.docx/.txt/.pdf 文件"
    Write-Host "   4. 等待 3-10 分钟（首次索引需要时间）"
    Write-Host "   5. 再次运行此脚本检查状态"
}

Write-Host ""
