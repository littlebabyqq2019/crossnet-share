# CrossNetShare Index Finder (v2.0.1+)
# Finds index file in client executable directory

Write-Host "=== CrossNetShare Index Location Finder ===" -ForegroundColor Cyan
Write-Host ""

# Try to find running client process
$proc = Get-Process CrossNetShareClient -ErrorAction SilentlyContinue

if ($proc) {
    $exePath = $proc.Path
    $appDir = Split-Path $exePath
    
    Write-Host "1. Running Client" -ForegroundColor Yellow
    Write-Host "   Executable: $exePath" -ForegroundColor Green
    Write-Host "   Directory: $appDir"
    Write-Host ""
    
    $indexFile = Join-Path $appDir "content_index.db"
    
    Write-Host "2. Index File Location" -ForegroundColor Yellow
    Write-Host "   Path: $indexFile"
    Write-Host ""
    
    if (Test-Path $indexFile) {
        $file = Get-Item $indexFile
        $sizeMB = [math]::Round($file.Length / 1MB, 2)
        $sizeKB = [math]::Round($file.Length / 1KB, 2)
        
        Write-Host "   Status: " -NoNewline
        Write-Host "EXISTS" -ForegroundColor Green
        Write-Host "   Size: $sizeMB MB ($sizeKB KB)"
        Write-Host "   Created: $($file.CreationTime)"
        Write-Host "   Last Modified: $($file.LastWriteTime)"
        
        # Check activity
        $minutesOld = (New-TimeSpan -Start $file.LastWriteTime -End (Get-Date)).TotalMinutes
        Write-Host "   Activity: " -NoNewline
        if ($minutesOld -lt 10) {
            Write-Host "ACTIVE (updated $([math]::Round($minutesOld, 1)) min ago)" -ForegroundColor Green
        } elseif ($minutesOld -lt 60) {
            Write-Host "IDLE (updated $([math]::Round($minutesOld, 0)) min ago)" -ForegroundColor Yellow
        } else {
            Write-Host "OLD (updated $([math]::Round($minutesOld / 60, 1)) hours ago)" -ForegroundColor Yellow
        }
        
        Write-Host ""
        Write-Host "   You can open this location in Explorer:" -ForegroundColor Cyan
        Write-Host "   explorer.exe `"$appDir`""
    } else {
        Write-Host "   Status: " -NoNewline
        Write-Host "NOT CREATED YET" -ForegroundColor Yellow
        Write-Host ""
        Write-Host "   Possible reasons:"
        Write-Host "   - Client just started (wait 1-5 minutes)"
        Write-Host "   - No shared path configured"
        Write-Host "   - Indexing not enabled"
        Write-Host "   - No files to index"
    }
    
    Write-Host ""
    Write-Host "3. Related Files in Same Directory" -ForegroundColor Yellow
    $relatedFiles = Get-ChildItem $appDir | Where-Object { 
        $_.Extension -in @('.db', '.dll', '.exe', '.json', '.log') -or 
        $_.Name -like '*sqlite*' -or 
        $_.Name -like '*simple*'
    } | Sort-Object Extension, Name
    
    if ($relatedFiles) {
        foreach ($file in $relatedFiles) {
            $size = if ($file.Length -lt 1KB) { "$($file.Length) B" } 
                    elseif ($file.Length -lt 1MB) { "{0:N2} KB" -f ($file.Length / 1KB) } 
                    else { "{0:N2} MB" -f ($file.Length / 1MB) }
            
            $icon = switch ($file.Extension) {
                '.db' { '📊' }
                '.dll' { '🔧' }
                '.exe' { '⚙️' }
                '.json' { '📄' }
                '.log' { '📝' }
                default { '📁' }
            }
            
            Write-Host "   $icon $($file.Name) ($size)"
        }
    } else {
        Write-Host "   (No related files found)"
    }
    
} else {
    Write-Host "❌ CrossNetShareClient is NOT RUNNING" -ForegroundColor Red
    Write-Host ""
    Write-Host "Please start the client first, then run this script again."
    Write-Host ""
    Write-Host "Note: Starting from v2.0.1, the index file is saved in the"
    Write-Host "same directory as the client executable, not in AppData."
}

Write-Host ""
Write-Host "=== Done ===" -ForegroundColor Cyan
