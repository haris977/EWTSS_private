#!/usr/bin/env python3
"""DRS Server — DP-ECM-1071 HF variant only.

Mirrors the C++ HF DLL dispatch table (218 entries).
Each handler function corresponds to one parse_GGG_UUU / format_GGG_UUU
pair in the C++ code.  CMD handlers reply with dummy structured data.
RESP handlers read decoded telemetry and react.

Two-way flow:
  HF Device --TCP--> Bridge --Kafka(hw.dp_ecm_hf.telemetry)--> [this server]
       ^                                                               |
       +<--UDP-- Bridge <--Kafka(drs.commands)------------------------+

Usage:
  python tools/drs_server_hf.py
  python tools/drs_server_hf.py --temp-warn 75 --verbose
"""
from __future__ import annotations

import argparse
import asyncio
import json
import struct
import time
from dataclasses import dataclass, field
from typing import Callable

TOPIC_IN  = "hw.dp_ecm_hf.telemetry"
TOPIC_OUT = "drs.commands"
VARIANT   = "dp_ecm_hf"

CYAN    = "\033[96m"
GREEN   = "\033[92m"
YELLOW  = "\033[93m"
MAGENTA = "\033[95m"
RED     = "\033[91m"
DIM     = "\033[2m"
RESET   = "\033[0m"

_TEMP_WARN: float = 75.0

# ---------------------------------------------------------------------------
# State
# ---------------------------------------------------------------------------

@dataclass
class HFDeviceState:
    instance_id:  str
    temps:        dict  = field(default_factory=dict)
    hopper_count: int   = 0
    jam_active:   bool  = False
    scan_active:  bool  = False
    rx_count:     int   = 0
    tx_count:     int   = 0


# ---------------------------------------------------------------------------
# Logging
# ---------------------------------------------------------------------------

def _ts() -> str:
    return time.strftime("%H:%M:%S")

def _rx(tag: str, iid: str, detail: str, color: str = GREEN) -> None:
    print(f"{DIM}{_ts()}{RESET}  {color}RX {tag:<13}{RESET}  {iid}  {detail}", flush=True)

def _tx(tag: str, iid: str, detail: str) -> None:
    print(f"{DIM}{_ts()}{RESET}  {YELLOW}TX {tag:<13}{RESET}  {iid}  {detail}", flush=True)

def _alert(msg: str) -> None:
    print(f"{DIM}{_ts()}{RESET}  {RED}!! ALERT  {RESET}  {msg}", flush=True)

def _state(msg: str) -> None:
    print(f"{DIM}{_ts()}{RESET}  {MAGENTA}** STATE  {RESET}  {msg}", flush=True)


# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

def _ack(s: HFDeviceState, g: int, u: int, px: str = "") -> dict:
    return {"instance_id": s.instance_id, "variant": VARIANT,
            "group_id": g, "unit_id": u, "status": 0, "payload_hex": px}

def _cmd_ack(frame: dict, s: HFDeviceState) -> list[dict]:
    g, u = frame["group_id"], frame["unit_id"]
    _rx(f"CMD {g}/{u}", s.instance_id, f"-> ACK {g}/{u+1}", CYAN)
    return [_ack(s, g, u + 1)]

def _resp_log(frame: dict, s: HFDeviceState, note: str = "") -> list[dict]:
    g, u = frame["group_id"], frame["unit_id"]
    sz   = frame.get("message_size_bytes", 0)
    raw  = frame.get("raw_hex", "")
    detail = note or (f"raw={raw[:16]}" if raw else f"{sz}B ACK")
    _rx(f"RESP {g}/{u}", s.instance_id, detail)
    return []


# ===========================================================================
# GROUP 100  HF System Management
# Mirrors: parse_cmd_100_17, format_resp_100_18
#          parse_resp_100_2, parse_resp_100_4, parse_resp_100_6,
#          parse_resp_100_8, parse_resp_100_10, parse_resp_100_14,
#          parse_resp_100_18, parse_resp_100_26
# ===========================================================================

def cmd_100_17_get_hw_status(frame: dict, s: HFDeviceState) -> list[dict]:
    """GET_HW_STATUS — reply with 16-bit status: 0x0001 = all OK."""
    _rx("CMD 100/17", s.instance_id, "GET_HW_STATUS -> RESP 100/18 status=OK", CYAN)
    return [_ack(s, 100, 18, struct.pack("<H", 0x0001).hex())]

def resp_100_2_sysver_ack(frame: dict, s: HFDeviceState) -> list[dict]:
    """RESP 100/2 — device echo of GET_SYSVER reply."""
    return _resp_log(frame, s, "sysver ACK")

def resp_100_4_build_info(frame: dict, s: HFDeviceState) -> list[dict]:
    """RESP 100/4 — build date and git commit hash."""
    return _resp_log(frame, s, "build info")

def resp_100_6_capabilities(frame: dict, s: HFDeviceState) -> list[dict]:
    """RESP 100/6 — hardware capability flags bitmask."""
    return _resp_log(frame, s, "capability flags")

def resp_100_8_serial(frame: dict, s: HFDeviceState) -> list[dict]:
    """RESP 100/8 — serial number and model string."""
    return _resp_log(frame, s, "serial / model")

def resp_100_10_temperature(frame: dict, s: HFDeviceState) -> list[dict]:
    """RESP 100/10 — 9 temperature sensors (HF layout).
    Sensors: internal, external, cpu, fpga, board, pa, rx, adc, psu.
    React: if any sensor > threshold, send throttle SET_THRESHOLD 101/25.
    Mirrors: parse_resp_100_10_hf_temperature()
    """
    fields = {k: v for k, v in frame.items() if k.endswith("_temp_c")}
    s.temps = fields
    temp_str = "  ".join(f"{k.replace('_temp_c','')}={v:.1f}C"
                         for k, v in fields.items())
    _rx("RESP 100/10", s.instance_id, f"temps: {temp_str or '(raw_hex)'}")

    hot = {k: v for k, v in fields.items() if v > _TEMP_WARN}
    if hot:
        worst = max(hot, key=hot.get)
        _alert(f"{s.instance_id}  {worst}={hot[worst]:.1f}C "
               f"(>{_TEMP_WARN}C) -> throttle 101/25 @-60dBm")
        return [_ack(s, 101, 25, struct.pack("<BBBBf", 0,0,0,0,-60.0).hex())]
    return []

def resp_100_14_uptime(frame: dict, s: HFDeviceState) -> list[dict]:
    """RESP 100/14 — device uptime in seconds."""
    return _resp_log(frame, s, "uptime")

def resp_100_18_hw_status_echo(frame: dict, s: HFDeviceState) -> list[dict]:
    """RESP 100/18 — HW status word echoed back from device."""
    return _resp_log(frame, s, "hw status echo")

def resp_100_26_rf_status(frame: dict, s: HFDeviceState) -> list[dict]:
    """RESP 100/26 — RF front-end status flags."""
    return _resp_log(frame, s, "RF front-end status")


# ===========================================================================
# GROUP 101  HF Signal Detection / Frequency Hopping Control
# Mirrors: parse_cmd_101_25, format_resp_101_26
#          parse_cmd_101_27, format_resp_101_28  (get/reply threshold)
#          parse_cmd_101_37, format_resp_101_38  (set/ack dwell)
#          parse_cmd_101_47, format_resp_101_48  (get notch)
#          parse_cmd_101_55, format_resp_101_56  (set gain)
#          parse_cmd_101_69, format_resp_101_70  (get scan status)
#          parse_cmd_101_83, format_resp_101_84  (set bandwidth)
#          parse_cmd_101_85, format_resp_101_86  (get bandwidth)
#          parse_cmd_101_87, format_resp_101_88  (set center freq)
#          parse_cmd_101_94, format_resp_101_95  (get freq range)
#          parse_cmd_101_140 .. 101_210 (various config ACKs)
#          parse_resp_101_40_hf_fh_detection     (FH detection -> jam)
# ===========================================================================

def cmd_101_25_set_threshold(frame: dict, s: HFDeviceState) -> list[dict]:
    """SET_THRESHOLD — set detection threshold (ch, dBm).  ACK 101/26."""
    _rx("CMD 101/25", s.instance_id, "SET_THRESHOLD -> ACK 101/26", CYAN)
    return [_ack(s, 101, 26)]

def cmd_101_27_get_threshold(frame: dict, s: HFDeviceState) -> list[dict]:
    """GET_THRESHOLD — reply with current threshold: ch=0, -80 dBm."""
    _rx("CMD 101/27", s.instance_id, "GET_THRESHOLD -> RESP 101/28 -80dBm", CYAN)
    return [_ack(s, 101, 28, struct.pack("<BBBBf", 0,0,0,0,-80.0).hex())]

def cmd_101_37_set_dwell(frame: dict, s: HFDeviceState) -> list[dict]:
    """SET_DWELL — set dwell time per step.  ACK 101/38."""
    _rx("CMD 101/37", s.instance_id, "SET_DWELL -> ACK 101/38", CYAN)
    return [_ack(s, 101, 38)]

def cmd_101_47_get_notch(frame: dict, s: HFDeviceState) -> list[dict]:
    """GET_NOTCH — reply with 4 disabled notch filters."""
    _rx("CMD 101/47", s.instance_id, "GET_NOTCH -> RESP 101/48 (4 notches off)", CYAN)
    p = struct.pack("<HH", 0, 4)            # active=0, count=4
    for _ in range(4):
        p += struct.pack("<ffH", 0.0, 0.0, 0)
    return [_ack(s, 101, 48, p.hex())]

def cmd_101_69_get_scan_status(frame: dict, s: HFDeviceState) -> list[dict]:
    """GET_SCAN_STATUS — reply with current scan state."""
    _rx("CMD 101/69", s.instance_id, "GET_SCAN_STATUS -> RESP 101/70", CYAN)
    return [_ack(s, 101, 70, struct.pack("<HH", int(s.scan_active), 0).hex())]

def cmd_101_85_get_bw(frame: dict, s: HFDeviceState) -> list[dict]:
    """GET_BW — reply with instantaneous bandwidth = 20 MHz."""
    _rx("CMD 101/85", s.instance_id, "GET_BW -> RESP 101/86 20MHz", CYAN)
    return [_ack(s, 101, 86, struct.pack("<f", 20.0e6).hex())]

def cmd_101_94_get_freq_range(frame: dict, s: HFDeviceState) -> list[dict]:
    """GET_FREQ_RANGE — reply with HF range: 20 MHz – 500 MHz."""
    _rx("CMD 101/94", s.instance_id, "GET_FREQ_RANGE -> RESP 101/95 20-500MHz", CYAN)
    return [_ack(s, 101, 95, struct.pack("<ff", 20.0e6, 500.0e6).hex())]

def cmd_101_162_get_squelch(frame: dict, s: HFDeviceState) -> list[dict]:
    """GET_SQUELCH — reply with squelch level = -90 dBm."""
    _rx("CMD 101/162", s.instance_id, "GET_SQUELCH -> RESP 101/163 -90dBm", CYAN)
    return [_ack(s, 101, 163, struct.pack("<f", -90.0).hex())]

def cmd_101_176_get_pulse_params(frame: dict, s: HFDeviceState) -> list[dict]:
    """GET_PULSE_PARAMS — reply with min=0.5us, max=5.0us."""
    _rx("CMD 101/176", s.instance_id, "GET_PULSE_PARAMS -> RESP 101/177", CYAN)
    return [_ack(s, 101, 177, struct.pack("<ff", 0.5e-6, 5.0e-6).hex())]

def cmd_101_184_get_iq_params(frame: dict, s: HFDeviceState) -> list[dict]:
    """GET_IQ_PARAMS — reply with sample_rate=40MHz, bits=16."""
    _rx("CMD 101/184", s.instance_id, "GET_IQ_PARAMS -> RESP 101/185", CYAN)
    return [_ack(s, 101, 185, struct.pack("<fH", 40.0e6, 16).hex())]

def cmd_101_202_get_classifier(frame: dict, s: HFDeviceState) -> list[dict]:
    """GET_CLASSIFIER — reply with mode=auto(1), confidence_thr=0.8."""
    _rx("CMD 101/202", s.instance_id, "GET_CLASSIFIER -> RESP 101/203", CYAN)
    return [_ack(s, 101, 203, struct.pack("<Hf", 1, 0.8).hex())]

def resp_101_40_fh_detection(frame: dict, s: HFDeviceState) -> list[dict]:
    """RESP 101/40 — FH detection result (hopper list).
    HF layout: 4B header + n*60B entries.
    Each hopper: hopper_number, min/max_freq, pulse_len, inter_hop,
                 detected_count, toa, power_dbm, active, snr_db,
                 min/max_freq_det, signal_bw, hop_rate, confidence.
    React: on first detection send jam-activate CMD 200/9.
    Mirrors: parse_resp_101_40_hf_fh_detection()
    """
    hoppers = frame.get("hoppers", [])
    count   = frame.get("hopper_count", len(hoppers))
    s.hopper_count = count
    for h in hoppers[:4]:
        num = h.get("hopper_number", "?")
        lo  = h.get("min_freq_hz", 0) / 1e6
        hi  = h.get("max_freq_hz", 0) / 1e6
        pwr = h.get("power_dbm", 0)
        snr = h.get("snr_db", 0)
        _rx("RESP 101/40", s.instance_id,
            f"hopper#{num}  {lo:.1f}-{hi:.1f}MHz  pwr={pwr:.0f}dBm  snr={snr:.1f}dB")

    if count > 0 and not s.jam_active:
        _state(f"{s.instance_id}  HF FH({count} hoppers) -> jam-activate CMD 200/9")
        return [_ack(s, 200, 9)]
    return []


# ===========================================================================
# GROUP 106  HF Jam ACK
# Mirrors: parse_resp_106_54_hf_jam_ack()
# ===========================================================================

def resp_106_54_jam_ack(frame: dict, s: HFDeviceState) -> list[dict]:
    """RESP 106/54 — jam acknowledged by HF device."""
    raw = frame.get("raw_hex", "")
    s.jam_active = True
    _rx("RESP 106/54", s.instance_id, f"jam ACK  raw={raw}")
    _state(f"{s.instance_id}  jam_active=True")
    return []


# ===========================================================================
# GROUP 109  RF / Antenna Control
# Mirrors: parse_cmd_109_11, format_resp_109_12
# ===========================================================================

def cmd_109_11_set_antenna(frame: dict, s: HFDeviceState) -> list[dict]:
    """SET_ANTENNA — select antenna port.  ACK 109/12."""
    _rx("CMD 109/11", s.instance_id, "SET_ANTENNA -> ACK 109/12", CYAN)
    return [_ack(s, 109, 12)]


# ===========================================================================
# GROUP 111  HF Channel / Sub-band Control
# Mirrors: parse_cmd_111_3..111_25, format_resp_111_4..111_26
#          parse_resp_111_4..111_211
# ===========================================================================

def cmd_111_15_get_channel_freq(frame: dict, s: HFDeviceState) -> list[dict]:
    """GET_CHANNEL_FREQ — reply with 150 MHz."""
    _rx("CMD 111/15", s.instance_id, "GET_CHANNEL_FREQ -> RESP 111/16 150MHz", CYAN)
    return [_ack(s, 111, 16, struct.pack("<f", 150.0e6).hex())]

def cmd_111_19_get_channel_mode(frame: dict, s: HFDeviceState) -> list[dict]:
    """GET_CHANNEL_MODE — reply with mode=1 (scan)."""
    _rx("CMD 111/19", s.instance_id, "GET_CHANNEL_MODE -> RESP 111/20 scan", CYAN)
    return [_ack(s, 111, 20, struct.pack("<H", 1).hex())]


# ===========================================================================
# GROUP 112  HF Scan Control
# Mirrors: parse_cmd_112_1, format_resp_112_2  (start scan)
#          parse_cmd_112_5, format_resp_112_6   (get scan config)
#          parse_cmd_112_13, format_resp_112_14 (set scan config)
#          parse_cmd_112_37, format_resp_112_38 (reset scan)
# ===========================================================================

def cmd_112_1_start_scan(frame: dict, s: HFDeviceState) -> list[dict]:
    """START_SCAN — begin frequency sweep.  ACK 112/2."""
    _rx("CMD 112/1", s.instance_id, "START_SCAN -> ACK 112/2", CYAN)
    s.scan_active = True
    _state(f"{s.instance_id}  scan_active=True")
    return [_ack(s, 112, 2)]

def cmd_112_5_get_scan_cfg(frame: dict, s: HFDeviceState) -> list[dict]:
    """GET_SCAN_CFG — reply: start=20MHz, stop=500MHz, step=1MHz, dwell=10ms."""
    _rx("CMD 112/5", s.instance_id, "GET_SCAN_CFG -> RESP 112/6", CYAN)
    return [_ack(s, 112, 6,
        struct.pack("<ffff", 20.0e6, 500.0e6, 1.0e6, 0.01).hex())]


# ===========================================================================
# GROUP 200  HF Jamming Control
# Mirrors: parse_cmd_200_9,  format_resp_200_10  (jam activate)
#          parse_cmd_200_11, format_resp_200_12  (jam deactivate)
#          parse_cmd_200_13, format_resp_200_14  (set jam params)
#          parse_cmd_200_15, format_resp_200_16  (get jam params)
#          parse_cmd_200_17, format_resp_200_18  (pulse enable)
#          parse_cmd_200_19, format_resp_200_20  (pulse disable)
#          parse_cmd_200_21, format_resp_200_22  (set waveform)
#          parse_cmd_200_23, format_resp_200_24  (get waveform)
#          parse_cmd_200_41, format_resp_200_42  (set schedule)
#          parse_cmd_200_54, format_resp_200_55  (set priority)
#          parse_cmd_200_56, format_resp_200_57  (get priority)
#          parse_resp_200_2_hf_jam_ack           (jam confirmed)
# ===========================================================================

def cmd_200_9_jam_activate(frame: dict, s: HFDeviceState) -> list[dict]:
    """JAM_ACTIVATE — arm HF jammer.  ACK 200/10."""
    _rx("CMD 200/9", s.instance_id, "JAM_ACTIVATE -> ACK 200/10", CYAN)
    s.jam_active = True
    _state(f"{s.instance_id}  jam_active=True (activated by DRS)")
    return [_ack(s, 200, 10)]

def cmd_200_11_jam_deactivate(frame: dict, s: HFDeviceState) -> list[dict]:
    """JAM_DEACTIVATE — disarm HF jammer.  ACK 200/12."""
    _rx("CMD 200/11", s.instance_id, "JAM_DEACTIVATE -> ACK 200/12", CYAN)
    s.jam_active = False
    _state(f"{s.instance_id}  jam_active=False")
    return [_ack(s, 200, 12)]

def cmd_200_15_get_jam_params(frame: dict, s: HFDeviceState) -> list[dict]:
    """GET_JAM_PARAMS — reply: freq=150MHz, power=+20dBm, mode=CW(0)."""
    _rx("CMD 200/15", s.instance_id, "GET_JAM_PARAMS -> RESP 200/16", CYAN)
    return [_ack(s, 200, 16, struct.pack("<ffH", 150.0e6, 20.0, 0).hex())]

def cmd_200_23_get_jam_waveform(frame: dict, s: HFDeviceState) -> list[dict]:
    """GET_JAM_WAVEFORM — reply: waveform=CW(0), bw=5MHz."""
    _rx("CMD 200/23", s.instance_id, "GET_JAM_WAVEFORM -> RESP 200/24", CYAN)
    return [_ack(s, 200, 24, struct.pack("<Hf", 0, 5.0e6).hex())]

def cmd_200_56_get_jam_priority(frame: dict, s: HFDeviceState) -> list[dict]:
    """GET_JAM_PRIORITY — reply with priority=1 (high)."""
    _rx("CMD 200/56", s.instance_id, "GET_JAM_PRIORITY -> RESP 200/57 pri=1", CYAN)
    return [_ack(s, 200, 57, struct.pack("<H", 1).hex())]

def resp_200_2_jam_ack(frame: dict, s: HFDeviceState) -> list[dict]:
    """RESP 200/2 — jam confirmed active by HF device."""
    raw = frame.get("raw_hex", "")
    s.jam_active = True
    _rx("RESP 200/2", s.instance_id, f"jam ACK  raw={raw}")
    _state(f"{s.instance_id}  jam_active=True")
    return []


# ===========================================================================
# GROUPS 1-7  MRx (Multi-Receiver) — HF
# Group 1: MRx System  init/start/stop/status/reset/mode/sync
# Group 3: MRx RF      freq/gain/filter config
# Group 4: MRx Channelizer  (19 entries — all ACK pattern)
# Group 5: MRx Detector  threshold/config
# Group 6: MRx Output   format/streaming
# Group 7: MRx Correlator  (11 entries — all ACK pattern)
# Mirrors: parse_cmd_N_U / format_resp_N_{U+1} for each entry
# ===========================================================================

def cmd_1_7_mrx_status(frame: dict, s: HFDeviceState) -> list[dict]:
    """MRX_STATUS — reply: running=1, active_rx=4."""
    _rx("CMD 1/7", s.instance_id, "MRX_STATUS -> RESP 1/8 running,rx=4", CYAN)
    return [_ack(s, 1, 8, struct.pack("<HH", 1, 4).hex())]

def cmd_1_17_mrx_get_mode(frame: dict, s: HFDeviceState) -> list[dict]:
    """MRX_GET_MODE — reply with mode=2 (coherent)."""
    _rx("CMD 1/17", s.instance_id, "MRX_GET_MODE -> RESP 1/18 coherent", CYAN)
    return [_ack(s, 1, 18, struct.pack("<H", 2).hex())]

def cmd_1_33_mrx_get_sync(frame: dict, s: HFDeviceState) -> list[dict]:
    """MRX_GET_SYNC — reply: source=GPS(1), offset=0ns."""
    _rx("CMD 1/33", s.instance_id, "MRX_GET_SYNC -> RESP 1/34 GPS", CYAN)
    return [_ack(s, 1, 34, struct.pack("<Hi", 1, 0).hex())]


# ===========================================================================
# HF Dispatch table  — one row per C++ dispatch_table[] entry
# ===========================================================================

CMD  = "command"
RESP = "response"

HF_DISPATCH: dict[tuple[int, int, str], Callable] = {
    # --- Group 100 ---
    (100, 17, CMD):  cmd_100_17_get_hw_status,
    (100,  2, RESP): resp_100_2_sysver_ack,
    (100,  4, RESP): resp_100_4_build_info,
    (100,  6, RESP): resp_100_6_capabilities,
    (100,  8, RESP): resp_100_8_serial,
    (100, 10, RESP): resp_100_10_temperature,
    (100, 14, RESP): resp_100_14_uptime,
    (100, 18, RESP): resp_100_18_hw_status_echo,
    (100, 26, RESP): resp_100_26_rf_status,
    # --- Group 101 CMDs ---
    (101, 25, CMD):  cmd_101_25_set_threshold,
    (101, 27, CMD):  cmd_101_27_get_threshold,
    (101, 37, CMD):  cmd_101_37_set_dwell,
    (101, 47, CMD):  cmd_101_47_get_notch,
    (101, 69, CMD):  cmd_101_69_get_scan_status,
    (101, 85, CMD):  cmd_101_85_get_bw,
    (101, 94, CMD):  cmd_101_94_get_freq_range,
    (101,162, CMD):  cmd_101_162_get_squelch,
    (101,176, CMD):  cmd_101_176_get_pulse_params,
    (101,184, CMD):  cmd_101_184_get_iq_params,
    (101,202, CMD):  cmd_101_202_get_classifier,
    # --- Group 101 RESPs ---
    (101, 40, RESP): resp_101_40_fh_detection,
    # --- Group 106 ---
    (106, 54, RESP): resp_106_54_jam_ack,
    # --- Group 109 ---
    (109, 11, CMD):  cmd_109_11_set_antenna,
    # --- Group 111 ---
    (111, 15, CMD):  cmd_111_15_get_channel_freq,
    (111, 19, CMD):  cmd_111_19_get_channel_mode,
    # --- Group 112 ---
    (112,  1, CMD):  cmd_112_1_start_scan,
    (112,  5, CMD):  cmd_112_5_get_scan_cfg,
    # --- Group 200 CMDs ---
    (200,  9, CMD):  cmd_200_9_jam_activate,
    (200, 11, CMD):  cmd_200_11_jam_deactivate,
    (200, 15, CMD):  cmd_200_15_get_jam_params,
    (200, 23, CMD):  cmd_200_23_get_jam_waveform,
    (200, 56, CMD):  cmd_200_56_get_jam_priority,
    # --- Group 200 RESPs ---
    (200,  2, RESP): resp_200_2_jam_ack,
    # --- MRx Group 1 ---
    (1,   7, CMD):   cmd_1_7_mrx_status,
    (1,  17, CMD):   cmd_1_17_mrx_get_mode,
    (1,  33, CMD):   cmd_1_33_mrx_get_sync,
}


def _dispatch(data: dict, state: HFDeviceState) -> list[dict]:
    frame = data.get("frame", {})
    ft    = frame.get("frame_type", "")
    g     = frame.get("group_id", 0)
    u     = frame.get("unit_id", 0)
    handler = HF_DISPATCH.get((g, u, ft))
    if handler:
        return handler(frame, state)
    if ft == CMD:
        return _cmd_ack(frame, state)
    return _resp_log(frame, state)


# ===========================================================================
# Main
# ===========================================================================

async def run(kafka: str, temp_warn: float, verbose: bool) -> None:
    global _TEMP_WARN
    _TEMP_WARN = temp_warn

    from aiokafka import AIOKafkaConsumer, AIOKafkaProducer

    producer = AIOKafkaProducer(bootstrap_servers=kafka)
    await producer.start()
    consumer = AIOKafkaConsumer(
        TOPIC_IN,
        bootstrap_servers=kafka,
        auto_offset_reset="latest",
        group_id="drs-server-hf",
        enable_auto_commit=True,
    )
    await consumer.start()

    states: dict[str, HFDeviceState] = {}

    print(f"\n{'='*64}", flush=True)
    print(f"  DRS Server [HF]  |  kafka={kafka}", flush=True)
    print(f"  consuming : {TOPIC_IN}", flush=True)
    print(f"  publishing: {TOPIC_OUT}", flush=True)
    print(f"  temp warn : >{temp_warn}C", flush=True)
    print(f"{'='*64}", flush=True)
    print(f"  {CYAN}RX CMD {RESET}  device requests  -> DRS replies with ACK+data", flush=True)
    print(f"  {GREEN}RX RESP{RESET}  device telemetry -> DRS reads & reacts", flush=True)
    print(f"  {YELLOW}TX     {RESET}  DRS command      -> Bridge UDP -> device", flush=True)
    print(f"{'-'*64}\n", flush=True)

    try:
        async for msg in consumer:
            try:
                data = json.loads(msg.value)
                iid  = data.get("instance_id", "?")

                if iid not in states:
                    states[iid] = HFDeviceState(iid)
                    _state(f"new HF device  {iid}")

                state = states[iid]
                state.rx_count += 1

                if verbose:
                    print(json.dumps(data, indent=2), flush=True)

                for cmd in _dispatch(data, state):
                    await producer.send_and_wait(
                        TOPIC_OUT, value=json.dumps(cmd).encode(), key=iid.encode()
                    )
                    state.tx_count += 1
                    _tx(f"CMD {cmd['group_id']}/{cmd['unit_id']}", iid,
                        f"-> {TOPIC_OUT}" +
                        (f"  payload={cmd['payload_hex']}" if cmd.get("payload_hex") else ""))

            except Exception as exc:
                print(f"{RED}[ERROR]{RESET} {exc}", flush=True)

    except asyncio.CancelledError:
        pass
    finally:
        print(f"\n{'-'*64}", flush=True)
        for s in states.values():
            print(f"  {s.instance_id:<22}  RX={s.rx_count}  TX={s.tx_count}"
                  f"  jam={s.jam_active}  scan={s.scan_active}", flush=True)
        await consumer.stop()
        await producer.stop()


def main() -> None:
    p = argparse.ArgumentParser(description="DRS Server HF — DP-ECM-1071 two-way control")
    p.add_argument("--kafka",     default="localhost:9092")
    p.add_argument("--temp-warn", type=float, default=75.0,
                   help="temp threshold C for throttle command (default 75)")
    p.add_argument("--verbose",   action="store_true")
    args = p.parse_args()
    try:
        asyncio.run(run(args.kafka, args.temp_warn, args.verbose))
    except KeyboardInterrupt:
        pass


if __name__ == "__main__":
    main()
