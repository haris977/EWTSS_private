#!/usr/bin/env python3
"""Send all HF ICD commands to drs-bridge.

One-time commands (all groups from Excel ICD) are sent once at startup.
Periodic commands (FF / FH / BURST detection) are re-sent every 3 seconds
until Ctrl+C.

Usage:
  python tools/test_dispatch.py --port 19001
  python tools/test_dispatch.py --port 19001 --interval 3
"""
from __future__ import annotations

import argparse
import asyncio
import struct
import sys
import time

CMD_HEADER  = bytes([0xAA, 0xAB, 0xBA, 0xBB])
CMD_FOOTER  = bytes([0xCC, 0xCD, 0xDC, 0xDD])
RESP_HEADER = bytes([0xEE, 0xEF, 0xFE, 0xFF])
RESP_FOOTER = bytes([0xFF, 0xFE, 0xEF, 0xEE])

# Default HF frequency range used in test payloads (MHz)
START_MHZ = 20
STOP_MHZ  = 6000


def build_cmd(group: int, unit: int, payload: bytes = b"") -> bytes:
    return (CMD_HEADER
            + struct.pack("<I", len(payload))
            + struct.pack("<H", group)
            + struct.pack("<H", unit)
            + payload
            + CMD_FOOTER)


# ── Payload builders used in PERIODIC ────────────────────────────────────────

def _payload_fh() -> bytes:
    # 101/39: start_freq_mhz (f32) + stop_freq_mhz (f32)  — 8 bytes
    return struct.pack("<ff", float(START_MHZ), float(STOP_MHZ))


def _payload_ff_burst() -> bytes:
    # 101/69 and 101/83: start_mhz (u16) + stop_mhz (u16) + rf_atten (u8) + if_atten (i8) + 2 rsv
    return struct.pack("<HHBbxx", START_MHZ, STOP_MHZ, 0, 0)


def _payload_wideband_fft() -> bytes:
    # 101/43: start_mhz (u16) + stop_mhz (u16) + rf_atten (u8) + if_atten (u8) + 2 rsv
    return struct.pack("<HHBBxx", START_MHZ, STOP_MHZ, 0, 0)


# ── Generic helpers ───────────────────────────────────────────────────────────

def _p_flag(flag: int = 0) -> bytes:
    """Single flag u8 + 3 reserved bytes."""
    return struct.pack("<BBBB", flag, 0, 0, 0)


def _p_u16x(val: int = 0) -> bytes:
    """u16 value + u16 reserved."""
    return struct.pack("<HH", val, 0)


def _p_u32(val: int = 0) -> bytes:
    """Single u32 value."""
    return struct.pack("<I", val)


def _p_ch(ch: int = 0) -> bytes:
    """MRx channel u8 + 3 reserved."""
    return struct.pack("<BBBB", ch, 0, 0, 0)


def _p_ch_param(ch: int = 0, param: int = 0) -> bytes:
    """Channel u8, param u8, 2 reserved bytes."""
    return struct.pack("<BBBB", ch, param, 0, 0)


def _p_ch_f32(ch: int = 0, val: float = 0.0) -> bytes:
    """Channel u8, reserved u8, reserved u16, f32 value — 8 bytes."""
    return struct.pack("<BBHf", ch, 0, 0, val)


# ── Group-specific payload builders ──────────────────────────────────────────

def _payload_center_freq() -> bytes:
    # 5/1: channel u8, antenna u8, reserved u16, center_freq double (MHz)
    return struct.pack("<BBxxd", 0, 0, 50.0)


def _payload_attenuation() -> bytes:
    # 5/3: channel u8, reserved u8, reserved u16, rf_atten f32, if_atten f32, cal f32
    return struct.pack("<BBxxfff", 0, 0, 0.0, 0.0, 0.0)


def _payload_go2mon_connect() -> bytes:
    # 6/9: IP char[128] + Port char[128] + channel u16 + reserved u16
    ip   = b"127.0.0.1\x00" + bytes(118)  # char[128]
    port = b"5000\x00"       + bytes(123)  # char[128]
    return ip + port + struct.pack("<HH", 0, 0)


def _payload_go2mon_transmit() -> bytes:
    # 6/13: channel u16, bw u16, reserved u16*2, center_freq double, date char[128]
    return struct.pack("<HHHHd", 0, 0, 0, 0, 50.0) + bytes(128)


def _payload_signal_bite_7() -> bytes:
    # 7/1: bite_freq double (MHz), channel u16, reserved u16
    return struct.pack("<dHH", 50.0, 0, 0)


def _payload_audio_squelch() -> bytes:
    # 7/21: selection u8, channel u8, reserved u16, threshold f32
    return struct.pack("<BBHf", 0, 0, 0, -80.0)


def _payload_start_iq_logging() -> bytes:
    # 4/23: channel u8, IQ_BW u8, reserved u16, center_freq double, Day/Mon/Yr/Hr/Min/Sec u8*6
    return struct.pack("<BBxxd", 0, 1, 50.0) + struct.pack("<BBBBBB", 1, 6, 0, 0, 0, 0)


def _payload_mem_scan() -> bytes:
    # 4/39 and 4/41: channel u8, reserved u8, reserved u16, count u32,
    # then 1 S_CMD_MEMORY_SCAN_LIST entry: freq double, bw u16, reserved u16
    return struct.pack("<BBHId", 0, 0, 0, 1, 50.0) + struct.pack("<HH", 100, 0)


def _payload_configure_detection() -> bytes:
    # 101/37: start_mhz u16, stop_mhz u16, rf_atten u8, if_atten i8, mode u8, zoom u8, agc u8, rsv u8
    return struct.pack("<HHBbBBBB", START_MHZ, STOP_MHZ, 0, 0, 0, 0, 0, 0)


def _payload_pulse_range() -> bytes:
    # 101/47: max_pulse f32, min_pulse f32, burst_max f32, burst_min f32
    return struct.pack("<ffff", 1.0, 0.1, 2.0, 0.2)


def _payload_zoom_fft() -> bytes:
    # 101/94: center_freq f32, zoom_bw i32, noise_leveling i32, threshold f32
    return struct.pack("<fIIf", 50.0, 1, 0, -80.0)


def _payload_flatness() -> bytes:
    # 101/100: mode u32, start_mhz f32, stop_mhz f32
    return struct.pack("<Iff", 0, float(START_MHZ), float(STOP_MHZ))


def _payload_comb_noise() -> bytes:
    # 106/55: start_freq double, stop_freq double, step u8, power_level u8, reserved 6B
    return struct.pack("<ddBB", 20.0, 80.0, 1, 10) + bytes(6)


def _payload_protected_band_list() -> bytes:
    # 111/9: count u16, reserved u16, then 1 entry (start f32, stop f32)
    return struct.pack("<HHff", 1, 0, float(START_MHZ), float(STOP_MHZ))


def _payload_hopper_channelization() -> bytes:
    # 111/31: start f32, stop f32, hop_period f32, enable u8, reserved 3B
    return struct.pack("<fffBxxx", float(START_MHZ), float(STOP_MHZ), 5.0, 1)


# Sent once at startup  — (group, unit, handler_name, payload_bytes)
ONE_TIME: list[tuple[int, int, str, bytes]] = [
    # ── Group 1: MRx System ──────────────────────────────────────────────────
    (1,  1, "_handle_system_version",           b""),
    (1,  3, "_handle_mrx_checksum",             b""),
    (1,  5, "_handle_health_pbit",              b""),
    (1,  7, "_handle_health_ibit",              b""),
    (1,  9, "_handle_temperature",              b""),
    (1, 25, "_handle_health_cbit",              b""),
    (1, 33, "_handle_close_all_channels",       b""),
    # ── Group 3: MRx Board / Tuner ───────────────────────────────────────────
    (3,  1, "_handle_board_count",              b""),
    (3, 17, "_handle_channel_info",             b""),
    (3, 19, "_handle_write_channel",            struct.pack("<HH", 0, 1)),
    (3, 21, "_handle_channel_status",           b""),
    (3, 25, "_handle_mrx_cbit",                 b""),
    # ── Group 4: MRx Audio / IQ / Scan ──────────────────────────────────────
    (4,  5, "_handle_mrx_set_threshold",        _p_ch_f32(val=-80.0)),
    (4,  7, "_handle_audio_acquisition",        _p_ch()),
    (4,  9, "_handle_audio_start_play",         _p_ch()),
    (4, 11, "_handle_audio_stop_play",          _p_ch()),
    (4, 15, "_handle_audio_fifo_reset",         _p_ch()),
    (4, 17, "_handle_demod_bw_selection",       _p_ch_param(param=1)),
    (4, 23, "_handle_start_iq_logging",         _payload_start_iq_logging()),
    (4, 25, "_handle_stop_iq_logging",          _p_ch()),
    (4, 33, "_handle_start_iq_streaming",       _p_ch_param(param=1)),
    (4, 35, "_handle_stop_iq_streaming",        _p_ch()),
    (4, 39, "_handle_config_mem_scan",          _payload_mem_scan()),
    (4, 41, "_handle_read_mem_scan",            _payload_mem_scan()),
    (4, 43, "_handle_ddc_fft",                  _p_ch_param(param=1)),
    (4, 57, "_handle_stop_mem_scan",            _p_ch()),
    (4, 65, "_handle_start_iq_optical",         _p_ch_param(param=1)),
    (4, 67, "_handle_stop_iq_optical",          _p_ch()),
    (4, 69, "_handle_optical_port_status",      _p_ch_param(param=1)),
    (4, 71, "_handle_optical_ip_address",       _p_ch_param(param=1)),
    # ── Group 5: MRx Tuning ──────────────────────────────────────────────────
    (5,  1, "_handle_set_center_freq",          _payload_center_freq()),
    (5,  3, "_handle_attenuation_selection",    _payload_attenuation()),
    # ── Group 6: GO2Monitor ──────────────────────────────────────────────────
    (6,  9, "_handle_go2mon_connect",           _payload_go2mon_connect()),
    (6, 11, "_handle_go2mon_disconnect",        b""),
    (6, 13, "_handle_go2mon_start_tx",          _payload_go2mon_transmit()),
    (6, 15, "_handle_go2mon_stop_tx",           b""),
    # ── Group 7: MRx BITE / RF / Audio Control ───────────────────────────────
    (7,  1, "_handle_signal_bite",              _payload_signal_bite_7()),
    (7,  3, "_handle_bite_antenna_switch",      _p_ch_param()),
    (7,  5, "_handle_ref_source_selection",     _p_ch_param()),
    (7,  9, "_handle_afc_enable",               _p_ch_param()),
    (7, 11, "_handle_rf_squelch",               _p_ch_param()),
    (7, 13, "_handle_iq_socket_connect",        _p_ch()),
    (7, 15, "_handle_iq_socket_close",          _p_ch()),
    (7, 17, "_handle_spectrum_averaging",       struct.pack("<BBBB", 1, 4, 0, 0)),
    (7, 19, "_handle_smart_agc",                _p_ch_param()),
    (7, 21, "_handle_audio_squelch",            _payload_audio_squelch()),
    # ── Group 100: SRx System / Diagnostics ─────────────────────────────────
    (100,  1, "_handle_system_version",         b""),
    (100,  3, "_handle_srx_checksum",           b""),
    (100,  5, "_handle_health_pbit",            b""),
    (100,  7, "_handle_health_ibit",            b""),
    (100,  9, "_handle_temperature",            b""),
    (100, 11, "_handle_set_fan_speed",          _p_u32(3000)),
    (100, 13, "_handle_fan_speed_status",       b""),
    (100, 15, "_handle_ethernet_test",          b""),
    (100, 17, "_handle_uart_test",              _p_ch()),
    (100, 21, "_handle_fan_voltage",            b""),
    (100, 23, "_handle_pps_test",               b""),
    (100, 25, "_handle_rs422_test",             b""),
    (100, 27, "_handle_fpga_temp",              b""),
    (100, 29, "_handle_health_cbit",            b""),
    # ── Group 101: SRx Detection / Jamming ──────────────────────────────────
    (101, 25, "_handle_srx_set_threshold",      _p_ch_f32(val=-80.0)),
    (101, 27, "_handle_set_resolution",         _p_ch_param(param=25)),
    (101, 31, "_handle_start_follow_on_jam",    bytes(80)),
    (101, 33, "_handle_stop_follow_on_jam",     b""),
    (101, 37, "_handle_configure_detection",    _payload_configure_detection()),
    (101, 43, "_handle_wideband_fft",           _payload_wideband_fft()),
    (101, 47, "_handle_set_pulse_range",        _payload_pulse_range()),
    (101, 55, "_handle_set_min_hops",           _p_u32(3)),
    (101, 63, "_handle_tracking_config",        _p_flag(1)),
    (101, 73, "_handle_start_list_jam",         bytes(80)),
    (101, 75, "_handle_stop_list_jam",          b""),
    (101, 79, "_handle_ecm_reports",            _p_flag(1)),
    (101, 85, "_handle_start_scan_speed",       _payload_ff_burst()),
    (101, 87, "_handle_stop_scan_speed",        b""),
    (101, 92, "_handle_responsive_sweep_jam",   bytes(100)),
    (101, 94, "_handle_zoom_fft",               _payload_zoom_fft()),
    (101, 100, "_handle_set_flatness_mode",     _payload_flatness()),
    (101, 102, "_handle_set_integration_time",  _p_u32(1000)),
    (101, 104, "_handle_set_multiband_fh",      _p_flag()),
    (101, 106, "_handle_set_narrowband_fh",     _p_flag()),
    (101, 158, "_handle_terminate_fft",         _p_flag()),
    # ── Group 106: ECM Exciter / Jammer ─────────────────────────────────────
    (106,  1, "_handle_immediate_jam",          bytes(20) + struct.pack("<d", 50.0)),
    (106,  3, "_handle_tdm_jam",                bytes(80)),
    (106,  5, "_handle_fdm_jam",                bytes(68)),
    (106,  9, "_handle_stop_immediate_jam",     b""),
    (106, 21, "_handle_enable_pa",              _p_flag(1)),
    (106, 39, "_handle_ext_mod_data",           _p_u32(0)),
    (106, 41, "_handle_sweep_freq_jam",         bytes(100)),
    (106, 45, "_handle_enable_pa_sdu",          _p_flag(1)),
    (106, 49, "_handle_prog_exciter",           struct.pack("<HH", 0, 0)),
    (106, 55, "_handle_comb_noise",             _payload_comb_noise()),
    # ── Group 109: Hopper Channelization ─────────────────────────────────────
    (109, 17, "_handle_acquire_hopper_data",    b""),
    # ── Group 111: SRx Module Control ────────────────────────────────────────
    (111,  5, "_handle_ref_input_selection",    _p_u16x()),
    (111,  7, "_handle_module_health",          b""),
    (111,  9, "_handle_protected_scan_list",    _payload_protected_band_list()),
    (111, 13, "_handle_protected_scan_enable",  _p_u16x(1)),
    (111, 15, "_handle_fh_channelization",      b""),
    (111, 17, "_handle_fh_split_enable",        _p_flag()),
    (111, 19, "_handle_fh_split_freq",          struct.pack("<f", 50.0)),
    (111, 21, "_handle_signal_bite_srx",        _p_flag()),
    (111, 23, "_handle_spectrum_protect",       _p_u16x(1)),
    (111, 25, "_handle_get_storage_details",    b""),
    (111, 27, "_handle_read_protected_bands",   b""),
    (111, 29, "_handle_auto_threshold",         _p_flag()),
    (111, 31, "_handle_hopper_chan_enable",      _payload_hopper_channelization()),
    # ── Group 112: ASU / SDU ─────────────────────────────────────────────────
    (112,  1, "_handle_asu_sdu_config",         struct.pack("<II", 0, 0)),
    (112,  3, "_handle_trsdu_line_status",      b""),
    (112,  5, "_handle_pa_line_status",         b""),
]

# Sent every `interval` seconds  — (group, unit, handler_name, payload_bytes)
PERIODIC: list[tuple[int, int, str, bytes]] = [
    (101, 69, "_handle_ff_detection",     _payload_ff_burst()),
    (101, 39, "_handle_fh_detection",     _payload_fh()),
    (101, 83, "_handle_burst_detection",  _payload_ff_burst()),
]


async def read_response(reader: asyncio.StreamReader, timeout: float = 0.5) -> dict | None:
    """Read one SDFC response frame from the bridge.

    Returns a dict with parsed fields, or None on timeout, or a dict with 'error' key.
    Frame layout: RESP_HEADER(4) + status(i16le,2) + size(u32le,4) + group(u16le,2) + unit(u16le,2)
                  + payload(size bytes) + RESP_FOOTER(4)
    """
    try:
        hdr = await asyncio.wait_for(reader.readexactly(14), timeout=timeout)
    except asyncio.TimeoutError:
        return None
    except (asyncio.IncompleteReadError, ConnectionResetError) as exc:
        return {"error": str(exc)}

    if hdr[:4] != RESP_HEADER:
        return {"error": f"bad header 0x{hdr[:4].hex()}"}

    status = struct.unpack_from("<h", hdr, 4)[0]
    size   = struct.unpack_from("<I", hdr, 6)[0]
    group  = struct.unpack_from("<H", hdr, 10)[0]
    unit   = struct.unpack_from("<H", hdr, 12)[0]

    try:
        payload = await asyncio.wait_for(reader.readexactly(size), timeout=timeout) if size else b""
        footer  = await asyncio.wait_for(reader.readexactly(4),    timeout=timeout)
    except asyncio.TimeoutError:
        return {"error": "timeout reading payload/footer", "group": group, "unit": unit}
    except (asyncio.IncompleteReadError, ConnectionResetError) as exc:
        return {"error": str(exc)}

    return {
        "group":     group,
        "unit":      unit,
        "status":    status,
        "size":      size,
        "payload":   payload,
        "footer_ok": footer == RESP_FOOTER,
        "ok":        status == 0 and footer == RESP_FOOTER,
    }



async def run(host: str, port: int, interval: float) -> None:
    print(f"\n[dispatch-test] bridge  : {host}:{port}")
    print(f"[dispatch-test] one-time: {len(ONE_TIME)} commands")
    print(f"[dispatch-test] periodic: {len(PERIODIC)} commands every {interval}s  (Ctrl+C to stop)\n")

    try:
        reader, writer = await asyncio.open_connection(host, port)
    except (OSError, asyncio.CancelledError) as exc:
        print(f"[dispatch-test] ERROR: cannot connect to {host}:{port} — {exc}", file=sys.stderr)
        print("[dispatch-test] Is drs-bridge running with a roster entry on this port?", file=sys.stderr)
        sys.exit(1)

    # ── Background response reader ────────────────────────────────────────────
    # Responses are independent of sends — some commands generate no response.
    # Running a separate task avoids sync issues when reads arrive late.
    stop_reader = asyncio.Event()

    async def _bg_reader() -> None:
        while not stop_reader.is_set():
            resp = await read_response(reader, timeout=0.2)
            if resp is None:
                continue
            if "error" in resp:
                print(f"  ← ERR: {resp['error']}")
                return
            ok  = "✓ OK" if resp["ok"] else f"✗ status={resp['status']} footer={'OK' if resp['footer_ok'] else 'BAD'}"
            pay = (resp["payload"].hex()[:64] + ("…" if resp["size"] > 32 else "")) if resp["size"] else ""
            print(f"  ← {resp['group']:>3}/{resp['unit']:<3}  {resp['size']}B  {ok}" +
                  (f"  [{pay}]" if pay else ""))

    reader_task = asyncio.create_task(_bg_reader())

    # ── One-time commands ────────────────────────────────────────────────────
    print("[dispatch-test] sending one-time commands...")
    for group, unit, handler, payload in ONE_TIME:
        writer.write(build_cmd(group, unit, payload))
        await writer.drain()
        print(f"  → {group:>3}/{unit:<3}  {handler}  ({len(payload)}B sent)")
        await asyncio.sleep(0.05)

    await asyncio.sleep(1.0)  # let any late responses drain before starting periodic
    print(f"\n[dispatch-test] one-time done — starting periodic loop (FF/FH/BURST every {interval}s)\n")

    # ── Periodic loop ────────────────────────────────────────────────────────
    cycle = 0
    try:
        while True:
            cycle += 1
            ts = time.strftime("%H:%M:%S")
            print(f"[{ts}] cycle {cycle} — sending FF / FH / BURST")
            for group, unit, handler, payload in PERIODIC:
                writer.write(build_cmd(group, unit, payload))
                await writer.drain()
                print(f"  → {group:>3}/{unit:<3}  {handler}  ({len(payload)}B sent)")
            await asyncio.sleep(interval)
    except (asyncio.CancelledError, KeyboardInterrupt):
        pass
    finally:
        stop_reader.set()
        reader_task.cancel()
        try:
            await reader_task
        except asyncio.CancelledError:
            pass
        print(f"\n[dispatch-test] stopped after {cycle} periodic cycle(s)")
        writer.close()
        try:
            await writer.wait_closed()
        except Exception:
            pass


def main() -> None:
    p = argparse.ArgumentParser(description="Test drs-server _DISPATCH entries via drs-bridge")
    p.add_argument("--host",     default="127.0.0.1")
    p.add_argument("--port",     type=int, required=True, help="Bridge TCP command port (e.g. 19001)")
    p.add_argument("--interval", type=float, default=3.0,
                   help="Seconds between periodic FF/FH/BURST sends (default 3)")
    args = p.parse_args()
    asyncio.run(run(args.host, args.port, args.interval))


if __name__ == "__main__":
    main()
