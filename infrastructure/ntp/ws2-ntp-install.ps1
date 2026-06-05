# WS2-side Meinberg NTP install (client mode against SG)
# Run as Administrator on WS2 during initial deployment.
# Per B1.3 design spec §4.3.

[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)]
    [string]$SgHost,  # e.g. "ws1-sg.lan" or the SG's IP address
    [string]$MsiPath = "$PSScriptRoot\..\..\packages\installers\meinberg-ntp\ntp-4.2.8p18-win-x64-setup.msi",
    [string]$ConfigTemplate = "$PSScriptRoot\ws2-ntp.conf"
)

$ErrorActionPreference = "Stop"

if (-not (Test-Path $MsiPath)) {
    throw "Meinberg NTP MSI not found at $MsiPath."
}

Write-Host "Installing Meinberg NTP from $MsiPath..."
$installLog = "$env:TEMP\meinberg-ntp-install.log"
Start-Process msiexec.exe -ArgumentList "/i `"$MsiPath`" /qn /l*v `"$installLog`"" -Wait -NoNewWindow
if ($LASTEXITCODE -ne 0) {
    throw "MSI install failed; see $installLog"
}

Write-Host "Substituting SG host '$SgHost' into ntp.conf template..."
$ntpConfDest = "C:\Program Files\NTP\etc\ntp.conf"
$template = Get-Content $ConfigTemplate -Raw
$rendered = $template.Replace("SG_HOST", $SgHost)
Set-Content -Path $ntpConfDest -Value $rendered -Encoding ASCII -Force

Write-Host "Ensuring logs directory exists..."
New-Item -Path "C:\ProgramData\EWTSS\logs\ntp-stats\" -ItemType Directory -Force | Out-Null

Write-Host "Starting NTP Windows Service..."
Set-Service -Name NTP -StartupType Automatic
Start-Service -Name NTP

Write-Host "Waiting 60 s for initial convergence against $SgHost..."
Start-Sleep -Seconds 60

Write-Host "Verifying client is syncing against $SgHost..."
$peers = & "C:\Program Files\NTP\bin\ntpq.exe" -p
$peers | Write-Host
$sgPeer = $peers | Where-Object { $_ -match $SgHost -or $_ -match "^\*" }
if (-not $sgPeer) {
    throw "ntpq -p output shows no synced peer against $SgHost - sync failed. See C:\ProgramData\EWTSS\logs\ntp-ws2.log"
}

Write-Host "WS2 NTP install complete. Sync status:"
& "C:\Program Files\NTP\bin\ntpq.exe" -c "rv 0 offset,jitter,stratum"
