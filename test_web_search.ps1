# CrossNetShare Web 搜索测试脚本
# 使用方法：.\test_web_search.ps1

Write-Host "=== CrossNetShare Web 搜索测试 ===" -ForegroundColor Cyan
Write-Host ""

$baseUrl = "http://localhost:8080"
$username = "admin"
$password = "admin123"

# 步骤 1：登录
Write-Host "步骤 1: 登录..." -ForegroundColor Yellow
try {
    $loginBody = @{
        username = $username
        password = $password
    } | ConvertTo-Json

    $loginResponse = Invoke-WebRequest -Uri "$baseUrl/api/login" `
        -Method POST `
        -ContentType "application/json" `
        -Body $loginBody `
        -SessionVariable session `
        -ErrorAction Stop

    Write-Host "✓ 登录成功" -ForegroundColor Green
    Write-Host "响应: $($loginResponse.Content)" -ForegroundColor Gray
    Write-Host ""
} catch {
    Write-Host "✗ 登录失败: $_" -ForegroundColor Red
    Write-Host "请确保服务器正在运行 (http://localhost:8080)" -ForegroundColor Yellow
    exit 1
}

# 步骤 2：测试简单搜索
Write-Host "步骤 2: 测试搜索 '雁塔'..." -ForegroundColor Yellow
try {
    $searchBody = @{
        query = "雁塔"
    } | ConvertTo-Json

    $searchResponse = Invoke-WebRequest -Uri "$baseUrl/api/content-search" `
        -Method POST `
        -ContentType "application/json" `
        -Body $searchBody `
        -WebSession $session `
        -ErrorAction Stop

    $result = $searchResponse.Content | ConvertFrom-Json
    
    Write-Host "✓ 搜索成功" -ForegroundColor Green
    Write-Host "查询: $($result.query)" -ForegroundColor Gray
    Write-Host "结果数量: $($result.totalResults)" -ForegroundColor Gray
    Write-Host ""
    Write-Host "找到的文件:" -ForegroundColor Cyan
    foreach ($file in $result.results) {
        Write-Host "  - $($file.filename) (来自: $($file.ownerClient))" -ForegroundColor White
    }
    Write-Host ""
} catch {
    Write-Host "✗ 搜索失败: $_" -ForegroundColor Red
    Write-Host "响应: $($_.Exception.Response)" -ForegroundColor Gray
    Write-Host ""
}

# 步骤 3：测试 OR 搜索
Write-Host "步骤 3: 测试 OR 搜索 '雁塔 or 中国'..." -ForegroundColor Yellow
try {
    $searchBody = @{
        query = "雁塔 or 中国"
    } | ConvertTo-Json

    $searchResponse = Invoke-WebRequest -Uri "$baseUrl/api/content-search" `
        -Method POST `
        -ContentType "application/json" `
        -Body $searchBody `
        -WebSession $session `
        -ErrorAction Stop

    $result = $searchResponse.Content | ConvertFrom-Json
    
    Write-Host "✓ 搜索成功" -ForegroundColor Green
    Write-Host "查询: $($result.query)" -ForegroundColor Gray
    Write-Host "结果数量: $($result.totalResults)" -ForegroundColor Gray
    Write-Host ""
    Write-Host "找到的文件:" -ForegroundColor Cyan
    foreach ($file in $result.results) {
        Write-Host "  - $($file.filename) (来自: $($file.ownerClient))" -ForegroundColor White
    }
    Write-Host ""
} catch {
    Write-Host "✗ 搜索失败: $_" -ForegroundColor Red
    Write-Host ""
}

# 步骤 4：测试英文搜索
Write-Host "步骤 4: 测试英文搜索 'cos'..." -ForegroundColor Yellow
try {
    $searchBody = @{
        query = "cos"
    } | ConvertTo-Json

    $searchResponse = Invoke-WebRequest -Uri "$baseUrl/api/content-search" `
        -Method POST `
        -ContentType "application/json" `
        -Body $searchBody `
        -WebSession $session `
        -ErrorAction Stop

    $result = $searchResponse.Content | ConvertFrom-Json
    
    Write-Host "✓ 搜索成功" -ForegroundColor Green
    Write-Host "查询: $($result.query)" -ForegroundColor Gray
    Write-Host "结果数量: $($result.totalResults)" -ForegroundColor Gray
    Write-Host ""
    Write-Host "找到的文件:" -ForegroundColor Cyan
    foreach ($file in $result.results) {
        Write-Host "  - $($file.filename) (来自: $($file.ownerClient))" -ForegroundColor White
    }
    Write-Host ""
} catch {
    Write-Host "✗ 搜索失败: $_" -ForegroundColor Red
    Write-Host ""
}

Write-Host "=== 测试完成 ===" -ForegroundColor Cyan
Write-Host ""
Write-Host "提示：" -ForegroundColor Yellow
Write-Host "- 如果搜索失败，请检查客户端是否正在运行" -ForegroundColor Gray
Write-Host "- 如果没有结果，请确保客户端已索引文件" -ForegroundColor Gray
Write-Host "- 可以在浏览器访问 http://localhost:8080/browse 查看 Web 界面" -ForegroundColor Gray
