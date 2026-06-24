#!/usr/bin/env python3
"""Standalone DP-ECM device simulator for drs-bridge manual testing.

Connects to the bridge's TCP command server and sends realistic SDFC frames,
simulating a DP-ECM-1071 HF or DP-ECM-1074 VU device being polled by
an entity controller.

Usage:
  python tools/dp_ecm_simulator.py --port 19001 --variant hf
  python tools/dp_ecm_simulator.py --port 19001 --variant vu --host 192.168.1.10
  python tools/dp_ecm_simulator.py --port 19001 --variant hf --repeat 5 --delay 0.5

What the simulator sends (HF scenario):
  1. CMD  100/2   GET_SYSVER            (query device version — 0-byte cmd)
  2. RESP 100/10  TEMPERATURE_STATUS    (9 float sensors, 36B payload)
  3. CMD  101/25  SET_THRESHOLD         (ch=0, -80 dBm, 8B payload)
  4. RESP 101/40  FH_DETECTION          (2 hoppers, HF 60B-per-entry layout)
  5. RESP 200/2   IMMEDIATE_JAM_ACK     (8B: jam_id + active flag)

What the simulator sends (VU scenario):
  1. CMD  100/2   GET_SYSVER
  2. RESP 100/10  TEMPERATURE_STATUS    (6 float sensors, 24B payload)
  3. RESP 200/2   IMMEDIATE_JAM_ACK     (0B payload, status-only)
  4. RESP 200/6   JAM_LIST_ACK          (1 freq entry, 8B payload)

The bridge parses each frame via its DLL and publishes decoded JSON to Kafka.
Run the simulator against a live bridge instance to verify end-to-end flow.

Prerequisites:
  - drs-bridge is running with a roster entry for the target variant/port
  - For dev, start the bridge with the dp_ecm_hf / dp_ecm_vu profiles active
"""
from __future__ import annotations

import argparse
import asyncio
import struct
import sys
import time
from typing import Sequence

# ---------------------------------------------------------------------------
# SDFC frame constants  (mirrors sdfc_frame.h)
# ---------------------------------------------------------------------------
CMD_HEADER  = bytes([0xAA, 0xAB, 0xBA, 0xBB])
CMD_FOOTER  = bytes([0xCC, 0xCD, 0xDC, 0xDD])
RESP_HEADER = bytes([0xEE, 0xEF, 0xFE, 0xFF])
RESP_FOOTER = bytes([0xFF, 0xFE, 0xEF, 0xEE])


def build_cmd(group: int, unit: int, payload: bytes = b"") -> bytes:
    """Build a complete SDFC command frame (CMD_HEADER … CMD_FOOTER)."""
    return (
        CMD_HEADER
        + struct.pack("<I", len(payload))   # payload size (u32 LE)
        + struct.pack("<H", group)          # group_id (u16 LE)
        + struct.pack("<H", unit)           # unit_id  (u16 LE)
        + payload
        + CMD_FOOTER
    )


def build_resp(status: int, group: int, unit: int, payload: bytes = b"") -> bytes:
    """Build a complete SDFC response frame (RESP_HEADER … RESP_FOOTER)."""
    return (
        RESP_HEADER
        + struct.pack("<h", status)         # status (i16 LE)
        + struct.pack("<I", len(payload))   # payload size (u32 LE)
        + struct.pack("<H", group)          # group_id (u16 LE)
        + struct.pack("<H", unit)           # unit_id  (u16 LE)
        + payload
        + RESP_FOOTER
    )


# ---------------------------------------------------------------------------
# Payload builders — produce byte-accurate payloads per ICD layouts
# ---------------------------------------------------------------------------

def hf_temperature_payload() -> bytes:
    """100/10 Temperature Status — 9 floats × 4B = 36B (HF layout, ICD Table 10).

    Sensors: internal, external, cpu, fpga, psu, rf_psu, fan, pa, exciter (°C).
    """
    return struct.pack("<9f", 25.3, 23.1, 47.8, 51.2, 22.9, 60.4, 28.7, 72.1, 41.0)


def vu_temperature_payload() -> bytes:
    """100/10 Temperature Status — 6 floats × 4B = 24B (VU layout, ICD Table 16).

    Sensors: processor, psu, fan, rf_psu, digital, fpga (°C).
    """
    return struct.pack("<6f", 25.3, 22.8, 48.5, 60.1, 23.0, 42.7)


def hf_set_threshold_payload(channel: int = 0, threshold_dbm: float = -80.0) -> bytes:
    """101/25 Set Threshold — 8B: channel(u8) + 3 reserved + threshold(f32 dBm)."""
    return struct.pack("<BBBBf", channel, 0, 0, 0, threshold_dbm)


def hf_fh_detection_payload(hopper_count: int = 2) -> bytes:
    """101/40 FH Detection response — header(4B) + hopper_count × 60B (HF layout).

    HF S_HOPPER_DATA = 60B per entry (VU uses 40B).
    Layout per ICD:
      @0  HopperNumber (u32)  @4  MinFreqMHz (f32)  @8  MaxFreqMHz (f32)
      @12 PulseLenMs (f32)    @16 InterHopMs (f32)  @20 DetectedCount (u32)
      @24 TOA H:M:S:rsv (4B) @28 PowerDbm (f32)    @32 Active (u16)
      @34 Reserved (u16)      @36 SNR (f32)
      HF-only extension:
      @40 MinFreqDetMHz (f32) @44 MaxFreqDetMHz (f32) @48 SignalBwKhz (f32)
      @52 HopRate (f32)       @56 Confidence (f32)
    """
    out = struct.pack("<HH", hopper_count, 0)  # count(u16) + reserved(u16) = 4B header
    for i in range(hopper_count):
        base_mhz = 30.0 + i * 10.0
        entry = (
            struct.pack("<I",  i + 1)                    # @0  HopperNumber
            + struct.pack("<f", base_mhz)                # @4  MinFreqMHz
            + struct.pack("<f", base_mhz + 5.0)          # @8  MaxFreqMHz
            + struct.pack("<f", 1.5)                     # @12 PulseLenMs
            + struct.pack("<f", 100.0)                   # @16 InterHopMs
            + struct.pack("<I", 12 + i)                  # @20 DetectedCount
            + bytes([10, 30, 45, 0])                     # @24 TOA H:M:S:rsv
            + struct.pack("<f", -65.0)                   # @28 PowerDbm
            + struct.pack("<H", 1)                       # @32 Active
            + struct.pack("<H", 0)                       # @34 Reserved
            + struct.pack("<f", 15.2)                    # @36 SNR
            + struct.pack("<f", base_mhz - 0.5)          # @40 MinFreqDetMHz
            + struct.pack("<f", base_mhz + 5.5)          # @44 MaxFreqDetMHz
            + struct.pack("<f", 25.0)                    # @48 SignalBwKhz
            + struct.pack("<f", 200.0)                   # @52 HopRate
            + struct.pack("<f", 0.92)                    # @56 Confidence
        )
        assert len(entry) == 60, f"hopper entry must be 60B, got {len(entry)}"
        out += entry
    return out


def hf_immediate_jam_ack_payload() -> bytes:
    """200/2 Immediate Jam ACK — 8B: jam_id(u16) + pad(u16) + active(u16) + pad(u16)."""
    return struct.pack("<HHHH", 7, 0, 1, 0)   # jam_id=7, active=1


def vu_jam_list_ack_payload(freq_count: int = 1) -> bytes:
    """200/6 Jam List ACK — count(u32) + freq_count × (freq_hz u32 + status u16 + pad u16).

    Status values: 0=inactive, 1=active, 2=jammed, 3=within_protected_band.
    """
    out = struct.pack("<I", freq_count)
    for i in range(freq_count):
        freq_hz = 150_000_000 + i * 1_000_000   # 150 MHz + offset
        status  = 2                               # jammed
        out += struct.pack("<IHH", freq_hz, status, 0)
    return out


# ---------------------------------------------------------------------------
# DeviceSimulator — async TCP client
# ---------------------------------------------------------------------------

def _print_hex_dump(data: bytes, indent: str = "") -> None:
    """Print bytes as hex + ASCII, 16 bytes per row."""
    for i in range(0, len(data), 16):
        chunk = data[i:i+16]
        hex_part = " ".join(f"{b:02x}" for b in chunk).ljust(47)
        asc_part = "".join(chr(b) if 32 <= b < 127 else "." for b in chunk)
        print(f"{indent}{i:04x}  {hex_part}  |{asc_part}|")


class _UdpReceiver(asyncio.DatagramProtocol):
    """Collects incoming UDP datagrams from the bridge into a queue."""

    def __init__(self, queue: asyncio.Queue) -> None:
        self._queue = queue

    def datagram_received(self, data: bytes, addr) -> None:
        self._queue.put_nowait((data, addr))

    def error_received(self, exc) -> None:
        print(f"[sim] UDP error: {exc}")


class DeviceSimulator:
    """Async TCP client that connects to a drs-bridge TCP command server,
    sends SDFC frames, and prints every binary response it receives back.

    Architecture:
      TX  ->  TCP  ->  bridge (command port, e.g. 19001)
      RX  <-  UDP  <-  bridge (response port, e.g. 19002)
    """

    def __init__(self, host: str = "127.0.0.1", port: int = 19001,
                 udp_port: int | None = None) -> None:
        self.host      = host
        self.port      = port
        # UDP response port is command_port + 1 by default (19001->19002, 19011->19012)
        self.udp_port  = udp_port if udp_port is not None else port + 1
        self._reader: asyncio.StreamReader | None = None
        self._writer: asyncio.StreamWriter | None = None
        self._udp_transport = None
        self._udp_queue: asyncio.Queue = asyncio.Queue()
        self.frames_sent     = 0
        self.frames_received = 0

    async def connect(self) -> None:
        self._reader, self._writer = await asyncio.open_connection(self.host, self.port)
        print(f"[sim] TCP connected -> {self.host}:{self.port}")

        # Bind UDP socket to receive bridge responses
        loop = asyncio.get_running_loop()
        self._udp_transport, _ = await loop.create_datagram_endpoint(
            lambda: _UdpReceiver(self._udp_queue),
            local_addr=("0.0.0.0", self.udp_port),
        )
        print(f"[sim] UDP listening  <- 0.0.0.0:{self.udp_port}")

    async def send(self, frame: bytes, label: str) -> None:
        if self._writer is None:
            raise RuntimeError("DeviceSimulator: not connected")
        self._writer.write(frame)
        await self._writer.drain()
        self.frames_sent += 1
        frame_type = "CMD " if frame[:4] == CMD_HEADER else "RESP"
        print(f"\n[SIM] → TCP {frame_type}  |  {label}  total={len(frame)}B")

    async def receive_loop(self) -> None:
        """Background task — receives UDP datagrams from the bridge and prints them.

        Bridge response flow:
          bridge -> UDP -> device (response port = command_port + 1)

        Each datagram is one complete SDFC frame:
          CMD  frame: CMD_HEADER(4) + size(u32) + group(u16) + unit(u16) + payload + CMD_FOOTER(4)
          RESP frame: RESP_HEADER(4) + status(i16) + size(u32) + group(u16) + unit(u16) + payload + RESP_FOOTER(4)
        """
        try:
            while True:
                data, addr = await self._udp_queue.get()
                self._parse_and_print_frame(data, via="UDP")
        except asyncio.CancelledError:
            pass

    def _parse_and_print_frame(self, data: bytes, via: str = "UDP") -> None:
        """Parse one complete SDFC frame and print full binary details."""
        if len(data) < 4:
            print(f"[SIM] RX {via} too short: {data.hex()}")
            return

        header = data[:4]
        self.frames_received += 1

        if header == CMD_HEADER:
            if len(data) < 12:
                print(f"[SIM] RX {via} CMD  too short: {data.hex()}")
                return
            size    = struct.unpack_from("<I", data, 4)[0]
            group   = struct.unpack_from("<H", data, 8)[0]
            unit    = struct.unpack_from("<H", data, 10)[0]
            payload = data[12:12 + size]
            print(f"\n[SIM] ← {via} CMD  |  group={group} unit={unit}  total={len(data)}B  payload={size}B")
            print(f"  header  : {data[:4].hex()}")
            print(f"  size    : {size}")
            print(f"  group   : {group}")
            print(f"  unit    : {unit}")
            if payload:
                print(f"  payload : {payload.hex()}")
                _print_hex_dump(payload, "           ")
            else:
                print(f"  payload : (empty)")
            print(f"  footer  : {data[-4:].hex()}")

        elif header == RESP_HEADER:
            if len(data) < 14:
                print(f"[SIM] RX {via} RESP too short: {data.hex()}")
                return
            status  = struct.unpack_from("<h", data, 4)[0]
            size    = struct.unpack_from("<I", data, 6)[0]
            group   = struct.unpack_from("<H", data, 10)[0]
            unit    = struct.unpack_from("<H", data, 12)[0]
            payload = data[14:14 + size]
            print(f"\n[SIM] ← {via} RESP  |  group={group} unit={unit}  status={status}  total={len(data)}B  payload={size}B")
            print(f"  header  : {data[:4].hex()}")
            print(f"  status  : {status}")
            print(f"  size    : {size}")
            print(f"  group   : {group}")
            print(f"  unit    : {unit}")
            if payload:
                print(f"  payload : {payload.hex()}")
                _print_hex_dump(payload, "           ")
            else:
                print(f"  payload : (empty)")
            print(f"  footer  : {data[-4:].hex()}")

        else:
            print(f"[SIM] RX {via} unknown header: {header.hex()}  raw={data[:32].hex()}")

    async def close(self) -> None:
        if self._udp_transport:
            self._udp_transport.close()
        if self._writer:
            self._writer.close()
            try:
                await self._writer.wait_closed()
            except Exception:
                pass
        print(
            f"[sim] disconnected  "
            f"(sent={self.frames_sent}  received={self.frames_received})"
        )


# ---------------------------------------------------------------------------
# Scenario functions
# ---------------------------------------------------------------------------

async def run_hf_scenario(sim: DeviceSimulator, delay: float = 0.2) -> None:
    """Send a realistic HF scenario covering diagnostics, detection, and jamming."""
    await sim.send(
        build_cmd(100, 2),
        "100/2  GET_SYSVER  (no payload)",
    )
    await asyncio.sleep(delay)

    await sim.send(
        build_resp(0, 100, 10, hf_temperature_payload()),
        "100/10 TEMPERATURE_STATUS  (9 sensors, 36B)",
    )
    await asyncio.sleep(delay)

    await sim.send(
        build_cmd(101, 25, hf_set_threshold_payload(channel=0, threshold_dbm=-80.0)),
        "101/25 SET_THRESHOLD  ch=0  thr=-80 dBm",
    )
    await asyncio.sleep(delay)

    await sim.send(
        build_resp(0, 101, 40, hf_fh_detection_payload(hopper_count=2)),
        "101/40 FH_DETECTION  (2 hoppers, 60B each)",
    )
    await asyncio.sleep(delay)

    await sim.send(
        build_resp(0, 200, 2, hf_immediate_jam_ack_payload()),
        "200/2  IMMEDIATE_JAM_ACK  jam_id=7  active=1",
    )
    await asyncio.sleep(delay)


async def run_vu_scenario(sim: DeviceSimulator, delay: float = 0.2) -> None:
    """Send a realistic VU scenario covering diagnostics and VU-specific jamming."""
    await sim.send(
        build_cmd(100, 2),
        "100/2  GET_SYSVER  (no payload)",
    )
    await asyncio.sleep(delay)

    await sim.send(
        build_resp(0, 100, 10, vu_temperature_payload()),
        "100/10 TEMPERATURE_STATUS  (6 sensors, 24B)",
    )
    await asyncio.sleep(delay)

    await sim.send(
        build_resp(0, 200, 2),
        "200/2  IMMEDIATE_JAM_ACK  (0B payload, status-only)",
    )
    await asyncio.sleep(delay)

    await sim.send(
        build_resp(0, 200, 6, vu_jam_list_ack_payload(freq_count=2)),
        "200/6  JAM_LIST_ACK  (2 freq entries)",
    )
    await asyncio.sleep(delay)


# ---------------------------------------------------------------------------
# CLI entry point
# ---------------------------------------------------------------------------

async def _run(host: str, port: int, variant: str, delay: float, repeat: int) -> None:
    sim = DeviceSimulator(host=host, port=port)
    try:
        await sim.connect()
    except OSError as exc:
        print(
            f"[sim] ERROR: cannot connect to {host}:{port} — {exc}\n"
            "[sim] Is the bridge running with a roster entry bound on this port?",
            file=sys.stderr,
        )
        sys.exit(1)

    # Start receive loop — prints every binary SDFC frame the bridge sends back
    rx_task = asyncio.create_task(sim.receive_loop(), name="sim-rx")

    t0 = time.monotonic()
    for i in range(repeat):
        if repeat > 1:
            print(f"\n[sim] -- run {i + 1}/{repeat} --")
        if variant == "hf":
            await run_hf_scenario(sim, delay=delay)
        else:
            await run_vu_scenario(sim, delay=delay)

    # Wait a moment so any in-flight responses from the bridge arrive
    await asyncio.sleep(2.0)

    rx_task.cancel()
    try:
        await rx_task
    except asyncio.CancelledError:
        pass

    elapsed = time.monotonic() - t0
    await sim.close()
    print(f"[sim] done  elapsed={elapsed:.2f}s")


def main(argv: Sequence[str] | None = None) -> None:
    p = argparse.ArgumentParser(
        description="DP-ECM device simulator — sends SDFC frames to a running drs-bridge",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=__doc__.split("Prerequisites:")[0].strip(),
    )
    p.add_argument("--host",    default="127.0.0.1",
                   help="Bridge host (default: 127.0.0.1)")
    p.add_argument("--port",    type=int, required=True,
                   help="Bridge TCP command port (from roster entry)")
    p.add_argument("--variant", choices=["hf", "vu"], default="hf",
                   help="ECM variant — hf=DP-ECM-1071, vu=DP-ECM-1074 (default: hf)")
    p.add_argument("--delay",   type=float, default=0.2,
                   help="Delay between frames in seconds (default: 0.2)")
    p.add_argument("--repeat",  type=int, default=1,
                   help="Repeat the scenario N times (default: 1)")
    args = p.parse_args(argv)
    asyncio.run(_run(args.host, args.port, args.variant, args.delay, args.repeat))


if __name__ == "__main__":
    main()
