$PY   = "C:\Users\Admin\drs-bridge-test\.venv\Scripts\python.exe"
$BDIR = "C:\Users\Admin\drs-bridge-test"
$SDIR = "C:\Users\Admin\drs-server-test"
$env:PYTHONIOENCODING = "utf-8"

# ── Kill old processes ───────────────────────────────────────────────────────
Write-Host "[1/4] Stopping old processes..."
Stop-Process -Name python -Force -ErrorAction SilentlyContinue
Start-Sleep -Seconds 2

# ── Open BRIDGE in a new visible window ─────────────────────────────────────
Write-Host "[2/4] Opening BRIDGE window..."
Start-Process powershell -ArgumentList @(
    "-NoExit",
    "-Command",
    "& { `$env:PYTHONIOENCODING='utf-8'; `$host.UI.RawUI.WindowTitle='BRIDGE'; cd '$BDIR'; & '$PY' run_bridge_launcher.py }"
)

# ── Open SERVER in a new visible window ─────────────────────────────────────
Write-Host "[3/4] Opening SERVER window..."
Start-Process powershell -ArgumentList @(
    "-NoExit",
    "-Command",
    "& { `$env:PYTHONIOENCODING='utf-8'; `$host.UI.RawUI.WindowTitle='DRS-SERVER'; cd '$SDIR'; & '$PY' '$BDIR\run_server_launcher.py' }"
)

# ── Wait for bridge to bind ──────────────────────────────────────────────────
Write-Host "[4/4] Waiting 15s for bridge to bind ports..."
Start-Sleep -Seconds 15

$hf = netstat -ano | findstr ":19001 "
$vu = netstat -ano | findstr ":19011 "
if (-not $hf) { Write-Host "WARNING: HF port 19001 not bound" -ForegroundColor Yellow }
if (-not $vu) { Write-Host "WARNING: VU port 19011 not bound" -ForegroundColor Yellow }
Write-Host ""
Write-Host "Bridge ports:" -ForegroundColor Green
if ($hf) { Write-Host "  HF $hf" -ForegroundColor Green }
if ($vu) { Write-Host "  VU $vu" -ForegroundColor Green }

# ── Run simulator in THIS window ─────────────────────────────────────────────
Write-Host ""
Write-Host "======================================================" -ForegroundColor Cyan
Write-Host "  SIMULATOR  --  watch BRIDGE window for decoded data" -ForegroundColor Cyan
Write-Host "======================================================" -ForegroundColor Cyan
Write-Host ""

Set-Location $BDIR
& $PY tools\dp_ecm_simulator.py --port 19001 --variant hf --repeat 5 --delay 1.5

Write-Host ""
Write-Host "--- VU ---" -ForegroundColor Cyan
& $PY tools\dp_ecm_simulator.py --port 19011 --variant vu --repeat 5 --delay 1.5
