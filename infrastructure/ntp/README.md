# infrastructure/ntp

Meinberg NTP install / uninstall / smoke-test scripts for the EWTSS v2
time-sync subsystem (B1.3).

See [B1.3 design spec](../../docs/ewtss/specs/time-sync-design.md)
and [B1.3 implementation plan](../../docs/ewtss/plans/time-sync-plan.md).

Sibling local-dev stack: [`infrastructure/kafka/`](../kafka/README.md) — single-node Kafka KRaft broker for `drs-server` / `drs-bridge` development.

| Script | Run on | Run as | Purpose |
|---|---|---|---|
| `sg-ntp-install.ps1` | WS1 (SG) | Administrator | Install Meinberg NTP as Stratum-1 server using `sg-ntp.conf`. |
| `ws2-ntp-install.ps1` | WS2 | Administrator | Install Meinberg NTP as client pointing at WS1, using `ws2-ntp.conf`. |
| `ntp-uninstall.ps1` | WS1 or WS2 | Administrator | Stop, disable, and uninstall Meinberg NTP. |
| `ntp-smoke.ps1` | WS2 (or any client) | local user | Poll `ntpq -p` and verify peer status. Phase 1 acceptance gate. |
