# CrossNetShare Index Status Check

Write-Host "=== CrossNetShare Index Status ===" -ForegroundColor Cyan
Write-Host ""

$clientDir = "$env:LOCALAPPDATA\CrossNetShareClient"
Write-Host "1. Client Directory" -ForegroundColor Yellow
Write-Host "   Path: $clientDir"

if (Test-Path $clientDir) {
    Write-Host "   Status: EXISTS" -ForegroundColor Green
    Write-Host ""
    Write-Host "   Contents:"
    Get-ChildItem $clientDir | ForEach-Object {
        $size = if ($_.Length) { 
            if ($_.Length -lt 1KB) { "$($_.Length) B" } 
            elseif ($_.Length -lt 1MB) { "{0:N2} KB" -f ($_.Length / 1KB) } 
            else { "{0:N2} MB" -f ($_.Length / 1MB) }
        } else { "DIR" }
        Write-Host "     - $($_.Name) ($size)"
    }
} else {
    Write-Host "   Status: NOT FOUND" -ForegroundColor Red
}

Write-Host ""

$indexFile = "$clientDir\content_index.db"
Write-Host "2. Index File" -ForegroundColor Yellow
Write-Host "   Path: $indexFile"

if (Test-Path $indexFile) {
    $file = Get-Item $indexFile
    $sizeMB = [math]::Round($file.Length / 1MB, 2)
    Write-Host "   Status: EXISTS" -ForegroundColor Green
    Write-Host "   Size: $sizeMB MB"
    Write-Host "   Last Modified: $($file.LastWriteTime)"
} else {
    Write-Host "   Status: NOT FOUND" -ForegroundColor Red
}

Write-Host ""
Write-Host "=== Check Complete ===" -ForegroundColor Cyan
