# Meinberg NTP for Windows — vendored installer

**Version:** 4.2.8p18 (current stable as of 2026-05-15)
**Source:** https://www.meinbergglobal.com/english/sw/ntp.htm
**SHA-256:** _pending — fill in after manual download (see below)_
**Licence:** NTP project licence (BSD-style; on Developer Handbook §15.7 allow-list).

## Status

The MSI binary itself is **not yet vendored**. Manual download by a human
operator is required because the file lives behind a public website and is
not on the air-gap mirror yet (tracked in B1.18).

## Manual download procedure (one-time, per release)

1. From a workstation with internet access, fetch the latest stable Windows
   x64 build:
   ```powershell
   $url = "https://www.meinbergglobal.com/download/ntp/windows/ntp-4.2.8p18-win64-setup.exe"
   $dest = "$env:TEMP\ntp-meinberg.msi"
   Invoke-WebRequest -Uri $url -OutFile $dest
   Get-FileHash -Algorithm SHA256 $dest | Select-Object Hash
   ```
2. Verify the publisher signature:
   ```powershell
   signtool verify /pa $dest
   ```
3. Copy the binary into this directory as
   `ntp-4.2.8p18-win-x64-setup.msi`.
4. Update this README and `packages/THIRD-PARTY-LICENCES.md` with the
   SHA-256 hash from step 1.
5. Commit both files (binary + docs) in a single commit.

## Usage

Installed by `infrastructure/ntp/sg-ntp-install.ps1` (SG-side Stratum-1
server) and `infrastructure/ntp/ws2-ntp-install.ps1` (WS2 client). See
[B1.3 design spec §4](../../../docs/ewtss/specs/time-sync-design.md).

## Re-vendoring

When upgrading to a newer Meinberg release, repeat the manual download
procedure with the new URL/version, then run the smoke test
`infrastructure/ntp/ntp-smoke.ps1` on a dev workstation pair.
