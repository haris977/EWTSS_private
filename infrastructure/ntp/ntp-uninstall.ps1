# Uninstalls Meinberg NTP. Run as Administrator.

[CmdletBinding()]
param()

$ErrorActionPreference = "Stop"

Write-Host "Stopping NTP service..."
Stop-Service -Name NTP -ErrorAction SilentlyContinue
Set-Service -Name NTP -StartupType Disabled -ErrorAction SilentlyContinue

Write-Host "Uninstalling Meinberg NTP..."
$uninstall = Get-WmiObject -Class Win32_Product -Filter "Name LIKE 'Network Time Protocol%'" -ErrorAction SilentlyContinue
if ($uninstall) {
    $uninstall.Uninstall() | Out-Null
    Write-Host "Uninstall complete."
} else {
    Write-Host "Meinberg NTP not installed; nothing to do."
}
