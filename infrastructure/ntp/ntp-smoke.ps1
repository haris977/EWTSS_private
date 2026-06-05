# NTP convergence smoke test — run on WS2 after the WS2 install completes.
# Verifies B1.3 Phase 1 gate: convergence to <=10 ms across the observation window.
# Per B1.3 design spec §11.

[CmdletBinding()]
param(
    [double]$ToleranceMs = 10.0,
    [int]$ObservationSeconds = 300
)

$ErrorActionPreference = "Stop"

function Get-NtpOffsetMs {
    $rv = & "C:\Program Files\NTP\bin\ntpq.exe" -c "rv 0 offset,jitter,stratum" 2>&1
    if ($LASTEXITCODE -ne 0) {
        throw "ntpq query failed: $rv"
    }
    if ($rv -match "offset=([\-\d\.]+)") {
        return [double]$matches[1]
    }
    throw "Could not parse offset from ntpq output: $rv"
}

Write-Host "Sampling NTP offset every 5 s for $ObservationSeconds s..."
$samples = @()
$start = Get-Date
while (((Get-Date) - $start).TotalSeconds -lt $ObservationSeconds) {
    $offsetMs = Get-NtpOffsetMs
    $samples += $offsetMs
    Write-Host ("[{0:HH:mm:ss}] offset = {1:F3} ms" -f (Get-Date), $offsetMs)
    Start-Sleep -Seconds 5
}

$max = ($samples | Measure-Object -Maximum).Maximum
$mean = ($samples | Measure-Object -Average).Average
Write-Host ""
Write-Host "Summary over $($samples.Count) samples:"
Write-Host "  Max offset:  $([math]::Round($max, 3)) ms"
Write-Host "  Mean offset: $([math]::Round($mean, 3)) ms"
Write-Host "  Tolerance:   $ToleranceMs ms"

if ([math]::Abs($max) -gt $ToleranceMs) {
    throw "FAIL - max offset $([math]::Round($max, 3)) ms exceeds $ToleranceMs ms tolerance."
}

Write-Host "PASS - NTP convergence within $ToleranceMs ms across $ObservationSeconds s observation window."
