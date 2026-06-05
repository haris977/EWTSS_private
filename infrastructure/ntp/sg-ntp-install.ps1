# SG-side Meinberg NTP install (Stratum-1 server)
# Run as Administrator on WS1 during initial deployment.
# Per B1.3 design spec §4.2.

[CmdletBinding()]
param(
    [string]$MsiPath = "$PSScriptRoot\..\..\packages\installers\meinberg-ntp\ntp-4.2.8p18-win-x64-setup.msi",
    [string]$ConfigSource = "$PSScriptRoot\sg-ntp.conf"
)

$ErrorActionPreference = "Stop"

if (-not (Test-Path $MsiPath)) {
    throw "Meinberg NTP MSI not found at $MsiPath. Run from a checked-out repo or supply -MsiPath."
}

Write-Host "Installing Meinberg NTP from $MsiPath..."
$installLog = "$env:TEMP\meinberg-ntp-install.log"
Start-Process msiexec.exe -ArgumentList "/i `"$MsiPath`" /qn /l*v `"$installLog`"" -Wait -NoNewWindow
if ($LASTEXITCODE -ne 0) {
    throw "MSI install failed; see $installLog"
}

Write-Host "Copying SG-specific ntp.conf to install location..."
$ntpConfDest = "C:\Program Files\NTP\etc\ntp.conf"
if (-not (Test-Path "C:\Program Files\NTP\etc\")) {
    throw "Meinberg install did not create C:\Program Files\NTP\etc\ - install may have failed."
}
Copy-Item -Path $ConfigSource -Destination $ntpConfDest -Force

Write-Host "Ensuring logs directory exists..."
New-Item -Path "C:\ProgramData\EWTSS\logs\ntp-stats\" -ItemType Directory -Force | Out-Null

Write-Host "Starting NTP Windows Service..."
Set-Service -Name NTP -StartupType Automatic
Start-Service -Name NTP

Write-Host "Waiting 30 s for daemon to initialise..."
Start-Sleep -Seconds 30

Write-Host "Verifying SG is serving as Stratum-1 (LOCAL clock at stratum 10 will surface as effective stratum 11 to clients)..."
$peers = & "C:\Program Files\NTP\bin\ntpq.exe" -p
$peers | Write-Host
if ($peers -notmatch "LOCAL") {
    throw "ntpq -p output missing LOCAL reference clock - config may be wrong."
}

Write-Host "SG NTP install complete."
