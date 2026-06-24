$PY   = "C:\Users\Admin\drs-bridge-test\.venv\Scripts\python.exe"
$BDIR = "C:\Users\Admin\drs-bridge-test"
$SDIR = "C:\Users\Admin\drs-server-test"
$env:PYTHONIOENCODING = "utf-8"

# ── 1. Kill any old bridge / server processes ────────────────────────────────
Write-Host "[1/5] Stopping old processes..."
Stop-Process -Name python -Force -ErrorAction SilentlyContinue
Start-Sleep -Seconds 2

# ── 2. Start bridge ──────────────────────────────────────────────────────────
Write-Host "[2/5] Starting bridge..."
$bproc = Start-Process -FilePath $PY `
    -ArgumentList "$BDIR\run_bridge_launcher.py" `
    -WorkingDirectory $BDIR `
    -RedirectStandardOutput "$BDIR\bridge_out.txt" `
    -RedirectStandardError  "$BDIR\bridge_err.txt" `
    -PassThru -WindowStyle Hidden

# ── 3. Start drs-server ──────────────────────────────────────────────────────
Write-Host "[3/5] Starting drs-server..."
$sproc = Start-Process -FilePath $PY `
    -ArgumentList "$BDIR\run_server_launcher.py" `
    -WorkingDirectory $SDIR `
    -RedirectStandardOutput "$BDIR\server_out.txt" `
    -RedirectStandardError  "$BDIR\server_err.txt" `
    -PassThru -WindowStyle Hidden

# ── 4. Wait for bridge to bind ports ─────────────────────────────────────────
Write-Host "[4/5] Waiting for bridge to bind (15s)..."
Start-Sleep -Seconds 15

$bound = Get-Content "$BDIR\bridge_err.txt" | Select-String "command consumer started"
if (-not $bound) {
    Write-Host "ERROR: Bridge did not start correctly. Check bridge_err.txt" -ForegroundColor Red
    Get-Content "$BDIR\bridge_err.txt" | Select-String "ERROR|bound"
    exit 1
}
Write-Host "      Bridge ready: $(netstat -ano | findstr ':19001 ' | Select-Object -First 1)"

# ── 5. Run full coverage tests ───────────────────────────────────────────────
Write-Host "[5/5] Running full coverage tests..."
Write-Host ""

Write-Host "===== HF - DP-ECM-1071 (218 entries) =====" -ForegroundColor Cyan
Set-Location $BDIR
& $PY tools\dp_ecm_full_coverage.py --variant hf --port 19001 --delay 0.05

Write-Host ""
Write-Host "===== VU - DP-ECM-1074 (215 entries) =====" -ForegroundColor Cyan
& $PY tools\dp_ecm_full_coverage.py --variant vu --port 19011 --delay 0.05

Write-Host ""
Write-Host "Done." -ForegroundColor Green
