#!/usr/bin/env python3
"""DRS Server — mirrors the C++ DLL dispatch table in Python.

Each C++ parse_GGG_UUU / format_GGG_UUU function pair becomes one Python
handler here.  CMD handlers return a dummy RESP payload (same structure the
DLL would produce).  RESP handlers read decoded telemetry and react.

Two-way flow:
  Device --TCP--> Bridge --Kafka(hw.*.telemetry)--> [this server]
    ^                                                      |
    +<--UDP-- Bridge <--Kafka(drs.commands)-----------<----+

Usage:
  python tools/drs_server.py
  python tools/drs_server.py --temp-warn 75 --verbose
"""
from __future__ import annotations

import argparse
import asyncio
import json
import logging
import struct
import time
from dataclasses import dataclass, field
from typing import Callable

TELEMETRY_TOPICS = ["hw.dp_ecm_hf.telemetry", "hw.dp_ecm_vu.telemetry"]
COMMANDS_TOPIC   = "drs.commands"

CYAN    = "\033[96m"
GREEN   = "\033[92m"
YELLOW  = "\033[93m"
MAGENTA = "\033[95m"
RED     = "\033[91m"
DIM     = "\033[2m"
RESET   = "\033[0m"

logging.basicConfig(level=logging.WARNING, format="%(asctime)s %(message)s")

_TEMP_WARN: float = 75.0   # set by CLI arg at startup

# ---------------------------------------------------------------------------
# Device state (per instance_id)
# ---------------------------------------------------------------------------

@dataclass
class DeviceState:
    instance_id: str
    variant: str
    temps:        dict  = field(default_factory=dict)
    hopper_count: int   = 0
    jam_active:   bool  = False
    scan_active:  bool  = False
    rx_count:     int   = 0
    tx_count:     int   = 0


# ---------------------------------------------------------------------------
# Logging helpers
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
# Command builder helpers
# ---------------------------------------------------------------------------

def _ack(state: DeviceState, group: int, unit: int,
         payload_hex: str = "") -> dict:
    """Build a drs.commands ACK response."""
    return {
        "instance_id": state.instance_id,
        "variant":     state.variant,
        "group_id":    group,
        "unit_id":     unit,
        "status":      0,
        "payload_hex": payload_hex,
    }

def _cmd_ack(frame: dict, state: DeviceState) -> list[dict]:
    """Generic CMD ACK — unit N -> RESP unit N+1, no payload."""
    g, u = frame["group_id"], frame["unit_id"]
    _rx(f"CMD {g}/{u}", state.instance_id, f"-> ACK {g}/{u+1}", CYAN)
    return [_ack(state, g, u + 1)]

def _resp_log(frame: dict, state: DeviceState, note: str = "") -> list[dict]:
    """Generic RESP log — telemetry received, no command back."""
    g, u = frame["group_id"], frame["unit_id"]
    sz   = frame.get("message_size_bytes", 0)
    raw  = frame.get("raw_hex", "")
    detail = note or (f"raw={raw[:16]}" if raw else f"{sz}B ACK")
    _rx(f"RESP {g}/{u}", state.instance_id, detail)
    return []


# ===========================================================================
# ── GROUP 100  System Management  ──────────────────────────────────────────
# CMD  100/17  GET_HW_STATUS   → RESP 100/18  hardware status word
# RESP 100/2   sysver reply    (device echoes back)
# RESP 100/4   build info
# RESP 100/6   capability flags
# RESP 100/8   serial/model
# RESP 100/10  temperature sensors
# RESP 100/12  VU: extended status
# RESP 100/14  uptime
# RESP 100/16  VU: rf cal status
# RESP 100/18  hw status (echoed back)
# RESP 100/22  VU: hw ver
# RESP 100/24  VU: sw ver
# RESP 100/26  HF: rf status
# RESP 100/28  VU: ext sensor
# RESP 100/30  VU: link status
# ===========================================================================

def cmd_100_17_get_hw_status(frame: dict, s: DeviceState) -> list[dict]:
    """GET_HW_STATUS: reply with 16-bit status word — 0x0001 = all subsystems OK."""
    _rx("CMD 100/17", s.instance_id, "GET_HW_STATUS -> RESP 100/18 status=OK", CYAN)
    return [_ack(s, 100, 18, struct.pack("<H", 0x0001).hex())]

def resp_100_2_sysver(frame: dict, s: DeviceState) -> list[dict]:
    """RESP 100/2: device echo of GET_SYSVER reply."""
    return _resp_log(frame, s, "sysver ACK")

def resp_100_4_build_info(frame: dict, s: DeviceState) -> list[dict]:
    """RESP 100/4: build date / git hash."""
    return _resp_log(frame, s, "build info")

def resp_100_6_capabilities(frame: dict, s: DeviceState) -> list[dict]:
    """RESP 100/6: capability flags bitmask."""
    return _resp_log(frame, s, "capability flags")

def resp_100_8_serial(frame: dict, s: DeviceState) -> list[dict]:
    """RESP 100/8: serial number and model string."""
    return _resp_log(frame, s, "serial / model")

def resp_100_10_temperature(frame: dict, s: DeviceState) -> list[dict]:
    """RESP 100/10: all temperature sensors.
    HF: 9 floats — internal, external, cpu, fpga, board, pa, rx, adc, psu
    VU: 6 floats — processor, board, pa, rx, adc, psu
    React: if any sensor > threshold, send throttle SET_THRESHOLD 101/25.
    """
    fields = {k: v for k, v in frame.items() if k.endswith("_temp_c")}
    s.temps = fields
    temp_str = "  ".join(f"{k.replace('_temp_c','')}={v:.1f}C" for k, v in fields.items())
    _rx("RESP 100/10", s.instance_id, f"temps: {temp_str or '(raw_hex)'}")

    commands: list[dict] = []
    hot = {k: v for k, v in fields.items() if v > _TEMP_WARN}
    if hot:
        worst = max(hot, key=hot.get)
        _alert(f"{s.instance_id}  {worst}={hot[worst]:.1f}C (>{_TEMP_WARN}C) -> throttle SET_THRESHOLD 101/25")
        commands.append(_ack(s, 101, 25,
            struct.pack("<BBBBf", 0, 0, 0, 0, -60.0).hex()))
    return commands

def resp_100_12_extended_status(frame: dict, s: DeviceState) -> list[dict]:
    """RESP 100/12 (VU): extended hardware status."""
    return _resp_log(frame, s, "extended status")

def resp_100_14_uptime(frame: dict, s: DeviceState) -> list[dict]:
    """RESP 100/14: device uptime in seconds."""
    return _resp_log(frame, s, "uptime")

def resp_100_16_rf_cal(frame: dict, s: DeviceState) -> list[dict]:
    """RESP 100/16 (VU): RF calibration status."""
    return _resp_log(frame, s, "RF cal status")

def resp_100_18_hw_status(frame: dict, s: DeviceState) -> list[dict]:
    """RESP 100/18: hw status word echoed back."""
    return _resp_log(frame, s, "hw status echo")

def resp_100_22_hw_version(frame: dict, s: DeviceState) -> list[dict]:
    """RESP 100/22 (VU): hardware version."""
    return _resp_log(frame, s, "hw version")

def resp_100_24_sw_version(frame: dict, s: DeviceState) -> list[dict]:
    """RESP 100/24 (VU): software version."""
    return _resp_log(frame, s, "sw version")

def resp_100_26_rf_status(frame: dict, s: DeviceState) -> list[dict]:
    """RESP 100/26 (HF): RF front-end status."""
    return _resp_log(frame, s, "RF status")

def resp_100_28_ext_sensor(frame: dict, s: DeviceState) -> list[dict]:
    """RESP 100/28 (VU): external sensor data."""
    return _resp_log(frame, s, "external sensor")

def resp_100_30_link_status(frame: dict, s: DeviceState) -> list[dict]:
    """RESP 100/30 (VU): comms link status."""
    return _resp_log(frame, s, "link status")


# ===========================================================================
# ── GROUP 101  Signal Detection / FH Control  ──────────────────────────────
# CMD  101/25  SET_THRESHOLD       payload: ch(B) pad(3B) thresh(f32 dBm)
# CMD  101/27  GET_THRESHOLD       -> RESP 101/28 current threshold
# CMD  101/31  (VU) SET_SCAN_MODE  -> RESP 101/32 ACK
# CMD  101/33  (VU) SET_FH_MODE    -> RESP 101/34 ACK
# CMD  101/37  SET_DWELL           -> RESP 101/38 ACK
# CMD  101/39  SET_DETECT_PARAMS   -> RESP 101/40* (triggers FH detection)
# CMD  101/43  SET_NOTCH           -> RESP 101/44 ACK
# CMD  101/47  GET_NOTCH           -> RESP 101/48 notch config
# CMD  101/55  SET_GAIN            -> RESP 101/56 ACK
# CMD  101/63  (VU) SET_AGC        -> RESP 101/64 ACK
# CMD  101/69  GET_SCAN_STATUS     -> RESP 101/70 scan state
# CMD  101/73  (VU) SET_PRE_AMP    -> RESP 101/74 ACK
# CMD  101/75  (VU) GET_PRE_AMP    -> RESP 101/76 preamp cfg
# CMD  101/79  (VU) SET_ATTEN      -> RESP 101/80 ACK
# CMD  101/83  SET_BW              -> RESP 101/84 ACK
# CMD  101/85  GET_BW              -> RESP 101/86 current BW
# CMD  101/87  SET_CENTER_FREQ     -> RESP 101/88 ACK
# CMD  101/92  (VU) SET_PULSE_LEN  -> RESP 101/93 ACK
# CMD  101/94  GET_FREQ_RANGE      -> RESP 101/95 freq range
# CMD  101/100 (VU) SET_HOP_RATE   -> RESP 101/101 ACK
# CMD  101/102 (VU) GET_HOP_RATE   -> RESP 101/103 current hop rate
# CMD  101/104 (VU) SET_POWER      -> RESP 101/105 ACK
# CMD  101/106 (VU) GET_POWER      -> RESP 101/107 current power
# CMD  101/140 SET_SCAN_RANGE      -> RESP 101/141 ACK
# CMD  101/158 SET_AGC_THRESHOLD   -> RESP 101/159 ACK
# CMD  101/160 SET_SQUELCH         -> RESP 101/161 ACK
# CMD  101/162 GET_SQUELCH         -> RESP 101/163 squelch value
# CMD  101/164 SET_RF_FILTER       -> RESP 101/165 ACK
# CMD  101/174 SET_PULSE_PARAMS    -> RESP 101/175 ACK
# CMD  101/176 GET_PULSE_PARAMS    -> RESP 101/177 pulse config
# CMD  101/178 SET_DEMOD_MODE      -> RESP 101/179 ACK
# CMD  101/182 SET_IQ_PARAMS       -> RESP 101/183 ACK
# CMD  101/184 GET_IQ_PARAMS       -> RESP 101/185 IQ config
# CMD  101/186 SET_DECIMATION      -> RESP 101/187 ACK
# CMD  101/200 SET_CLASSIFIER      -> RESP 101/201 ACK
# CMD  101/202 GET_CLASSIFIER      -> RESP 101/203 classifier config
# CMD  101/204 SET_TRACK_PARAMS    -> RESP 101/205 ACK
# CMD  101/210 RESET_DETECTOR      -> RESP 101/211 ACK
# RESP 101/40  FH_DETECTION        hopper list — react with jam activate
# ===========================================================================

def cmd_101_25_set_threshold(frame: dict, s: DeviceState) -> list[dict]:
    """SET_THRESHOLD: set RF detection threshold (dBm).  ACK with 101/26."""
    _rx("CMD 101/25", s.instance_id, "SET_THRESHOLD -> ACK 101/26", CYAN)
    return [_ack(s, 101, 26)]

def cmd_101_27_get_threshold(frame: dict, s: DeviceState) -> list[dict]:
    """GET_THRESHOLD: reply with current threshold = -80 dBm on ch 0."""
    _rx("CMD 101/27", s.instance_id, "GET_THRESHOLD -> RESP 101/28 thr=-80dBm", CYAN)
    return [_ack(s, 101, 28, struct.pack("<BBBBf", 0, 0, 0, 0, -80.0).hex())]

def cmd_101_37_set_dwell(frame: dict, s: DeviceState) -> list[dict]:
    """SET_DWELL: set dwell time per frequency step.  ACK 101/38."""
    _rx("CMD 101/37", s.instance_id, "SET_DWELL -> ACK 101/38", CYAN)
    return [_ack(s, 101, 38)]

def cmd_101_47_get_notch(frame: dict, s: DeviceState) -> list[dict]:
    """GET_NOTCH: reply with 4 notch filter entries (all disabled)."""
    _rx("CMD 101/47", s.instance_id, "GET_NOTCH -> RESP 101/48", CYAN)
    payload = struct.pack("<HH", 0, 4)  # active=0, count=4
    for _ in range(4):
        payload += struct.pack("<ffH", 0.0, 0.0, 0)  # start, stop, enabled
    return [_ack(s, 101, 48, payload.hex())]

def cmd_101_69_get_scan_status(frame: dict, s: DeviceState) -> list[dict]:
    """GET_SCAN_STATUS: return current scan state."""
    _rx("CMD 101/69", s.instance_id, "GET_SCAN_STATUS -> RESP 101/70", CYAN)
    active = 1 if s.scan_active else 0
    return [_ack(s, 101, 70, struct.pack("<HH", active, 0).hex())]

def cmd_101_85_get_bw(frame: dict, s: DeviceState) -> list[dict]:
    """GET_BW: reply with current bandwidth = 20 MHz."""
    _rx("CMD 101/85", s.instance_id, "GET_BW -> RESP 101/86 bw=20MHz", CYAN)
    return [_ack(s, 101, 86, struct.pack("<f", 20.0e6).hex())]

def cmd_101_94_get_freq_range(frame: dict, s: DeviceState) -> list[dict]:
    """GET_FREQ_RANGE: reply with min=20 MHz, max=500 MHz."""
    _rx("CMD 101/94", s.instance_id, "GET_FREQ_RANGE -> RESP 101/95 20-500MHz", CYAN)
    return [_ack(s, 101, 95, struct.pack("<ff", 20.0e6, 500.0e6).hex())]

def cmd_101_162_get_squelch(frame: dict, s: DeviceState) -> list[dict]:
    """GET_SQUELCH: reply with current squelch level = -90 dBm."""
    _rx("CMD 101/162", s.instance_id, "GET_SQUELCH -> RESP 101/163", CYAN)
    return [_ack(s, 101, 163, struct.pack("<f", -90.0).hex())]

def cmd_101_176_get_pulse_params(frame: dict, s: DeviceState) -> list[dict]:
    """GET_PULSE_PARAMS: reply with min_pulse=0.5us, max_pulse=5.0us."""
    _rx("CMD 101/176", s.instance_id, "GET_PULSE_PARAMS -> RESP 101/177", CYAN)
    return [_ack(s, 101, 177, struct.pack("<ff", 0.5e-6, 5.0e-6).hex())]

def cmd_101_184_get_iq_params(frame: dict, s: DeviceState) -> list[dict]:
    """GET_IQ_PARAMS: reply with sample_rate=40MHz, bits=16."""
    _rx("CMD 101/184", s.instance_id, "GET_IQ_PARAMS -> RESP 101/185", CYAN)
    return [_ack(s, 101, 185, struct.pack("<fH", 40.0e6, 16).hex())]

def cmd_101_202_get_classifier(frame: dict, s: DeviceState) -> list[dict]:
    """GET_CLASSIFIER: reply with classifier mode=1 (auto), confidence=0.8."""
    _rx("CMD 101/202", s.instance_id, "GET_CLASSIFIER -> RESP 101/203", CYAN)
    return [_ack(s, 101, 203, struct.pack("<Hf", 1, 0.8).hex())]

def resp_101_40_fh_detection(frame: dict, s: DeviceState) -> list[dict]:
    """FH_DETECTION: frequency hopper detected.
    Log each hopper. On first detection send jam-activate CMD 200/9.
    """
    hoppers = frame.get("hoppers", [])
    count   = frame.get("hopper_count", len(hoppers))
    s.hopper_count = count
    for h in hoppers[:4]:
        num   = h.get("hopper_number", "?")
        lo    = h.get("min_freq_hz", 0) / 1e6
        hi    = h.get("max_freq_hz", 0) / 1e6
        pwr   = h.get("power_dbm", 0)
        snr   = h.get("snr_db", 0)
        _rx("RESP 101/40", s.instance_id,
            f"hopper#{num}  {lo:.1f}-{hi:.1f}MHz  pwr={pwr:.0f}dBm  snr={snr:.1f}dB")

    commands: list[dict] = []
    if count > 0 and not s.jam_active:
        _state(f"{s.instance_id}  FH({count} hoppers) -> send jam-activate CMD 200/9")
        commands.append(_ack(s, 200, 9))
    return commands

def resp_101_generic(frame: dict, s: DeviceState) -> list[dict]:
    return _resp_log(frame, s)


# ===========================================================================
# ── GROUP 106  Jamming / Jam Control (HF: jam ack; VU: full jam control) ───
# HF RESP 106/54  JAM_ACK         jam confirmed active
# VU CMD  106/1   JAM_START        -> RESP 106/2  ACK
# VU CMD  106/3   JAM_STOP         -> RESP 106/4  ACK
# VU CMD  106/5   JAM_STATUS       -> RESP 106/6  jam state
# VU CMD  106/9   SET_JAM_FREQ     -> RESP 106/10 ACK
# VU CMD  106/21  SET_JAM_POWER    -> RESP 106/22 ACK
# VU CMD  106/39  GET_JAM_STATUS   -> RESP 106/40 jam status
# VU CMD  106/41  SET_JAM_WAVEFORM -> RESP 106/42 ACK
# VU CMD  106/45  GET_JAM_WAVEFORM -> RESP 106/46 waveform config
# VU CMD  106/49  SET_JAM_MODE     -> RESP 106/50 ACK
# VU CMD  106/55  GET_JAM_POWER    -> RESP 106/56 current power
# ===========================================================================

def resp_106_54_hf_jam_ack(frame: dict, s: DeviceState) -> list[dict]:
    """HF JAM_ACK: device confirmed jam is active."""
    raw = frame.get("raw_hex", "")
    s.jam_active = True
    _rx("RESP 106/54", s.instance_id, f"jam ACK raw={raw}")
    _state(f"{s.instance_id}  jam_active=True")
    return []

def cmd_106_5_jam_status(frame: dict, s: DeviceState) -> list[dict]:
    """JAM_STATUS: reply with current jam state."""
    _rx("CMD 106/5", s.instance_id, "JAM_STATUS -> RESP 106/6", CYAN)
    active = 1 if s.jam_active else 0
    return [_ack(s, 106, 6, struct.pack("<HH", active, 0).hex())]

def cmd_106_9_set_jam_freq(frame: dict, s: DeviceState) -> list[dict]:
    """SET_JAM_FREQ: set center frequency for jammer. ACK 106/10."""
    _rx("CMD 106/9", s.instance_id, "SET_JAM_FREQ -> ACK 106/10", CYAN)
    return [_ack(s, 106, 10)]

def cmd_106_39_get_jam_status(frame: dict, s: DeviceState) -> list[dict]:
    """GET_JAM_STATUS: reply with jam status word."""
    _rx("CMD 106/39", s.instance_id, "GET_JAM_STATUS -> RESP 106/40", CYAN)
    active = 1 if s.jam_active else 0
    return [_ack(s, 106, 40, struct.pack("<HH", active, 0).hex())]

def cmd_106_45_get_jam_waveform(frame: dict, s: DeviceState) -> list[dict]:
    """GET_JAM_WAVEFORM: reply with waveform=0 (CW), bw=1MHz."""
    _rx("CMD 106/45", s.instance_id, "GET_JAM_WAVEFORM -> RESP 106/46", CYAN)
    return [_ack(s, 106, 46, struct.pack("<Hf", 0, 1.0e6).hex())]

def cmd_106_55_get_jam_power(frame: dict, s: DeviceState) -> list[dict]:
    """GET_JAM_POWER: reply with current output power = +20 dBm."""
    _rx("CMD 106/55", s.instance_id, "GET_JAM_POWER -> RESP 106/56 +20dBm", CYAN)
    return [_ack(s, 106, 56, struct.pack("<f", 20.0).hex())]


# ===========================================================================
# ── GROUP 108  (VU only)  Waveform / Signal Library  ───────────────────────
# RESP 108/6  waveform library info
# ===========================================================================

def resp_108_6_waveform_library(frame: dict, s: DeviceState) -> list[dict]:
    """RESP 108/6 (VU): waveform library count and version."""
    return _resp_log(frame, s, "waveform library")


# ===========================================================================
# ── GROUP 109  RF / Antenna Control  ───────────────────────────────────────
# CMD  109/11  SET_ANTENNA        -> RESP 109/12 ACK
# CMD  109/15  (VU) GET_ANTENNA   -> RESP 109/16 antenna config
# CMD  109/17  (VU) SET_ANT_GAIN  -> RESP 109/18 ACK
# ===========================================================================

def cmd_109_11_set_antenna(frame: dict, s: DeviceState) -> list[dict]:
    """SET_ANTENNA: select antenna port. ACK 109/12."""
    _rx("CMD 109/11", s.instance_id, "SET_ANTENNA -> ACK 109/12", CYAN)
    return [_ack(s, 109, 12)]

def cmd_109_15_get_antenna(frame: dict, s: DeviceState) -> list[dict]:
    """GET_ANTENNA (VU): reply with antenna port = 0, gain = 3.0 dB."""
    _rx("CMD 109/15", s.instance_id, "GET_ANTENNA -> RESP 109/16", CYAN)
    return [_ack(s, 109, 16, struct.pack("<Hf", 0, 3.0).hex())]

def cmd_109_17_set_ant_gain(frame: dict, s: DeviceState) -> list[dict]:
    """SET_ANT_GAIN (VU): set antenna gain. ACK 109/18."""
    _rx("CMD 109/17", s.instance_id, "SET_ANT_GAIN -> ACK 109/18", CYAN)
    return [_ack(s, 109, 18)]

def resp_109_generic(frame: dict, s: DeviceState) -> list[dict]:
    return _resp_log(frame, s)


# ===========================================================================
# ── GROUP 111  Channel / Sub-band Control  ─────────────────────────────────
# CMD  111/3   SET_CHANNEL       -> RESP 111/4   ACK
# CMD  111/5   SET_CHANNEL_BW    -> RESP 111/6   ACK
# CMD  111/7   (VU) GET_CHANNEL  -> RESP 111/8   channel config
# CMD  111/9   SET_CHANNEL_GAIN  -> RESP 111/10  ACK
# CMD  111/13  SET_CHANNEL_FREQ  -> RESP 111/14  ACK
# CMD  111/15  GET_CHANNEL_FREQ  -> RESP 111/16  current freq
# CMD  111/17  SET_CHANNEL_MODE  -> RESP 111/18  ACK
# CMD  111/19  GET_CHANNEL_MODE  -> RESP 111/20  current mode
# CMD  111/21  (VU) SET_CH_FILT  -> RESP 111/22  ACK
# CMD  111/23  (VU) GET_CH_FILT  -> RESP 111/24  filter config
# CMD  111/25  SET_CHANNEL_AGC   -> RESP 111/26  ACK
# CMD  111/27  (VU) SET_CH_DEMOD -> RESP 111/28  ACK
# CMD  111/29  (VU) GET_CH_DEMOD -> RESP 111/30  demod config
# CMD  111/31  (VU) SET_CH_SQU   -> RESP 111/32  ACK
# HF  RESP 111/211  extended channel status
# ===========================================================================

def cmd_111_7_get_channel(frame: dict, s: DeviceState) -> list[dict]:
    """GET_CHANNEL (VU): reply with ch=0, freq=150MHz, bw=5MHz."""
    _rx("CMD 111/7", s.instance_id, "GET_CHANNEL -> RESP 111/8", CYAN)
    return [_ack(s, 111, 8, struct.pack("<Hff", 0, 150.0e6, 5.0e6).hex())]

def cmd_111_15_get_channel_freq(frame: dict, s: DeviceState) -> list[dict]:
    """GET_CHANNEL_FREQ: reply with 150 MHz."""
    _rx("CMD 111/15", s.instance_id, "GET_CHANNEL_FREQ -> RESP 111/16 150MHz", CYAN)
    return [_ack(s, 111, 16, struct.pack("<f", 150.0e6).hex())]

def cmd_111_19_get_channel_mode(frame: dict, s: DeviceState) -> list[dict]:
    """GET_CHANNEL_MODE: reply with mode=1 (scan)."""
    _rx("CMD 111/19", s.instance_id, "GET_CHANNEL_MODE -> RESP 111/20 mode=scan", CYAN)
    return [_ack(s, 111, 20, struct.pack("<H", 1).hex())]

def cmd_111_23_get_ch_filter(frame: dict, s: DeviceState) -> list[dict]:
    """GET_CH_FILT (VU): reply with filter=low-pass, cutoff=5MHz."""
    _rx("CMD 111/23", s.instance_id, "GET_CH_FILT -> RESP 111/24", CYAN)
    return [_ack(s, 111, 24, struct.pack("<Hf", 0, 5.0e6).hex())]

def cmd_111_29_get_ch_demod(frame: dict, s: DeviceState) -> list[dict]:
    """GET_CH_DEMOD (VU): reply with demod=FM."""
    _rx("CMD 111/29", s.instance_id, "GET_CH_DEMOD -> RESP 111/30 FM", CYAN)
    return [_ack(s, 111, 30, struct.pack("<H", 2).hex())]

def resp_111_generic(frame: dict, s: DeviceState) -> list[dict]:
    return _resp_log(frame, s)


# ===========================================================================
# ── GROUP 112  Scan Control  ───────────────────────────────────────────────
# CMD  112/1  START_SCAN     -> RESP 112/2  ACK (scan begins)
# CMD  112/3  (VU) STOP_SCAN -> RESP 112/4  ACK
# CMD  112/5  GET_SCAN_CFG   -> RESP 112/6  scan config
# CMD  112/13 SET_SCAN_CFG   -> RESP 112/14 ACK
# CMD  112/37 RESET_SCAN     -> RESP 112/38 ACK
# ===========================================================================

def cmd_112_1_start_scan(frame: dict, s: DeviceState) -> list[dict]:
    """START_SCAN: start frequency scan. ACK 112/2."""
    _rx("CMD 112/1", s.instance_id, "START_SCAN -> ACK 112/2", CYAN)
    s.scan_active = True
    _state(f"{s.instance_id}  scan_active=True")
    return [_ack(s, 112, 2)]

def cmd_112_3_stop_scan(frame: dict, s: DeviceState) -> list[dict]:
    """STOP_SCAN (VU): stop frequency scan. ACK 112/4."""
    _rx("CMD 112/3", s.instance_id, "STOP_SCAN -> ACK 112/4", CYAN)
    s.scan_active = False
    _state(f"{s.instance_id}  scan_active=False")
    return [_ack(s, 112, 4)]

def cmd_112_5_get_scan_cfg(frame: dict, s: DeviceState) -> list[dict]:
    """GET_SCAN_CFG: reply with start=20MHz, stop=500MHz, step=1MHz, dwell=10ms."""
    _rx("CMD 112/5", s.instance_id, "GET_SCAN_CFG -> RESP 112/6", CYAN)
    return [_ack(s, 112, 6,
        struct.pack("<ffff", 20.0e6, 500.0e6, 1.0e6, 0.01).hex())]

def resp_112_generic(frame: dict, s: DeviceState) -> list[dict]:
    return _resp_log(frame, s)


# ===========================================================================
# ── GROUP 200  Jamming Control (HF)  ───────────────────────────────────────
# CMD  200/9   JAM_ACTIVATE      -> RESP 200/10  ACK
# CMD  200/11  JAM_DEACTIVATE    -> RESP 200/12  ACK
# CMD  200/13  SET_JAM_PARAMS    -> RESP 200/14  ACK
# CMD  200/15  GET_JAM_PARAMS    -> RESP 200/16  jam config
# CMD  200/17  JAM_PULSE_ENABLE  -> RESP 200/18  ACK
# CMD  200/19  JAM_PULSE_DISABLE -> RESP 200/20  ACK
# CMD  200/21  SET_JAM_WAVEFORM  -> RESP 200/22  ACK
# CMD  200/23  GET_JAM_WAVEFORM  -> RESP 200/24  waveform
# CMD  200/41  SET_JAM_SCHED     -> RESP 200/42  ACK
# CMD  200/54  SET_JAM_PRIORITY  -> RESP 200/55  ACK
# CMD  200/56  GET_JAM_PRIORITY  -> RESP 200/57  priority word
# HF RESP 200/2  JAM_ACK         jam confirmed
# VU RESP 200/2,4,6,8 jam acks
# ===========================================================================

def cmd_200_9_jam_activate(frame: dict, s: DeviceState) -> list[dict]:
    """JAM_ACTIVATE: arm jammer. RESP 200/10 ACK."""
    _rx("CMD 200/9", s.instance_id, "JAM_ACTIVATE -> ACK 200/10", CYAN)
    s.jam_active = True
    _state(f"{s.instance_id}  jam_active=True (DRS activated)")
    return [_ack(s, 200, 10)]

def cmd_200_11_jam_deactivate(frame: dict, s: DeviceState) -> list[dict]:
    """JAM_DEACTIVATE: disarm jammer. RESP 200/12 ACK."""
    _rx("CMD 200/11", s.instance_id, "JAM_DEACTIVATE -> ACK 200/12", CYAN)
    s.jam_active = False
    _state(f"{s.instance_id}  jam_active=False")
    return [_ack(s, 200, 12)]

def cmd_200_15_get_jam_params(frame: dict, s: DeviceState) -> list[dict]:
    """GET_JAM_PARAMS: reply with freq=150MHz, power=+20dBm, mode=CW."""
    _rx("CMD 200/15", s.instance_id, "GET_JAM_PARAMS -> RESP 200/16", CYAN)
    return [_ack(s, 200, 16,
        struct.pack("<ffH", 150.0e6, 20.0, 0).hex())]

def cmd_200_23_get_jam_waveform(frame: dict, s: DeviceState) -> list[dict]:
    """GET_JAM_WAVEFORM: reply with waveform=0 (CW), bw=5MHz."""
    _rx("CMD 200/23", s.instance_id, "GET_JAM_WAVEFORM -> RESP 200/24", CYAN)
    return [_ack(s, 200, 24, struct.pack("<Hf", 0, 5.0e6).hex())]

def cmd_200_56_get_jam_priority(frame: dict, s: DeviceState) -> list[dict]:
    """GET_JAM_PRIORITY: reply with priority=1 (high)."""
    _rx("CMD 200/56", s.instance_id, "GET_JAM_PRIORITY -> RESP 200/57 pri=1", CYAN)
    return [_ack(s, 200, 57, struct.pack("<H", 1).hex())]

def resp_200_2_jam_ack(frame: dict, s: DeviceState) -> list[dict]:
    """RESP 200/2: jam acknowledged by device."""
    raw = frame.get("raw_hex", "")
    s.jam_active = True
    _rx("RESP 200/2", s.instance_id, f"jam ACK  raw={raw}")
    _state(f"{s.instance_id}  jam_active=True")
    return []

def resp_200_generic(frame: dict, s: DeviceState) -> list[dict]:
    return _resp_log(frame, s)


# ===========================================================================
# ── GROUPS 1-7  MRx (Multi-Receiver) Control  ──────────────────────────────
# Group 1: MRx System
#   CMD 1/1  MRX_INIT      -> RESP 1/2  ACK
#   CMD 1/3  MRX_START     -> RESP 1/4  ACK
#   CMD 1/5  MRX_STOP      -> RESP 1/6  ACK
#   CMD 1/7  MRX_STATUS    -> RESP 1/8  status
#   CMD 1/9  MRX_RESET     -> RESP 1/10 ACK
#   CMD 1/13 MRX_SET_MODE  -> RESP 1/14 ACK
#   CMD 1/17 MRX_GET_MODE  -> RESP 1/18 mode
#   CMD 1/25 MRX_SET_SYNC  -> RESP 1/26 ACK
#   CMD 1/33 MRX_GET_SYNC  -> RESP 1/34 sync config
# Group 3: MRx RF
#   CMD 3/1  -> 3/2   CMD 3/17 -> 3/18 ... (ACK pattern)
# Group 4: MRx Channelizer
#   CMD 4/5 -> 4/6  ... (ACK pattern, many entries)
# Group 5: MRx Detector
#   CMD 5/1 -> 5/2  CMD 5/3 -> 5/4  CMD 5/9 -> 5/10  CMD 5/13 -> 5/14
# Group 6: MRx Output
#   CMD 6/7 -> 6/8  ... (ACK pattern)
# Group 7: MRx Correlator
#   CMD 7/1 -> 7/2  ... (ACK pattern)
# ===========================================================================

def cmd_1_7_mrx_status(frame: dict, s: DeviceState) -> list[dict]:
    """MRX_STATUS: reply with status=running, rx_count=4."""
    _rx("CMD 1/7", s.instance_id, "MRX_STATUS -> RESP 1/8 running", CYAN)
    return [_ack(s, 1, 8, struct.pack("<HH", 1, 4).hex())]

def cmd_1_17_mrx_get_mode(frame: dict, s: DeviceState) -> list[dict]:
    """MRX_GET_MODE: reply with mode=2 (coherent)."""
    _rx("CMD 1/17", s.instance_id, "MRX_GET_MODE -> RESP 1/18 coherent", CYAN)
    return [_ack(s, 1, 18, struct.pack("<H", 2).hex())]

def cmd_1_33_mrx_get_sync(frame: dict, s: DeviceState) -> list[dict]:
    """MRX_GET_SYNC: reply with sync=GPS, offset=0ns."""
    _rx("CMD 1/33", s.instance_id, "MRX_GET_SYNC -> RESP 1/34", CYAN)
    return [_ack(s, 1, 34, struct.pack("<Hi", 1, 0).hex())]

def resp_mrx_generic(frame: dict, s: DeviceState) -> list[dict]:
    return _resp_log(frame, s)


# ===========================================================================
# Dispatch tables  — one entry per C++ dispatch_table[] row
# (group, unit, frame_type)  -> handler function
# frame_type: "command" | "response"
# ===========================================================================

CMD  = "command"
RESP = "response"

HF_DISPATCH: dict[tuple[int,int,str], Callable] = {
    # Group 100
    (100, 17, CMD):  cmd_100_17_get_hw_status,
    (100,  2, RESP): resp_100_2_sysver,
    (100,  4, RESP): resp_100_4_build_info,
    (100,  6, RESP): resp_100_6_capabilities,
    (100,  8, RESP): resp_100_8_serial,
    (100, 10, RESP): resp_100_10_temperature,
    (100, 14, RESP): resp_100_14_uptime,
    (100, 18, RESP): resp_100_18_hw_status,
    (100, 26, RESP): resp_100_26_rf_status,
    # Group 101 CMDs
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
    # Group 101 RESPs
    (101, 40, RESP): resp_101_40_fh_detection,
    # Group 106
    (106, 54, RESP): resp_106_54_hf_jam_ack,
    # Group 109
    (109, 11, CMD):  cmd_109_11_set_antenna,
    # Group 111 CMDs
    (111, 15, CMD):  cmd_111_15_get_channel_freq,
    (111, 19, CMD):  cmd_111_19_get_channel_mode,
    # Group 112
    (112,  1, CMD):  cmd_112_1_start_scan,
    (112,  5, CMD):  cmd_112_5_get_scan_cfg,
    # Group 200 CMDs
    (200,  9, CMD):  cmd_200_9_jam_activate,
    (200, 11, CMD):  cmd_200_11_jam_deactivate,
    (200, 15, CMD):  cmd_200_15_get_jam_params,
    (200, 23, CMD):  cmd_200_23_get_jam_waveform,
    (200, 56, CMD):  cmd_200_56_get_jam_priority,
    # Group 200 RESPs
    (200,  2, RESP): resp_200_2_jam_ack,
}

VU_DISPATCH: dict[tuple[int,int,str], Callable] = {
    # Group 100
    (100, 10, RESP): resp_100_10_temperature,
    (100, 12, RESP): resp_100_12_extended_status,
    (100, 14, RESP): resp_100_14_uptime,
    (100, 16, RESP): resp_100_16_rf_cal,
    (100, 22, RESP): resp_100_22_hw_version,
    (100, 24, RESP): resp_100_24_sw_version,
    (100, 28, RESP): resp_100_28_ext_sensor,
    (100, 30, RESP): resp_100_30_link_status,
    # Group 101
    (101, 25, CMD):  cmd_101_25_set_threshold,
    (101, 27, CMD):  cmd_101_27_get_threshold,
    (101, 37, CMD):  cmd_101_37_set_dwell,
    (101, 69, CMD):  cmd_101_69_get_scan_status,
    (101, 85, CMD):  cmd_101_85_get_bw,
    (101, 94, CMD):  cmd_101_94_get_freq_range,
    (101, 40, RESP): resp_101_40_fh_detection,
    # Group 106 CMDs (VU)
    (106,  5, CMD):  cmd_106_5_jam_status,
    (106,  9, CMD):  cmd_106_9_set_jam_freq,
    (106, 39, CMD):  cmd_106_39_get_jam_status,
    (106, 45, CMD):  cmd_106_45_get_jam_waveform,
    (106, 55, CMD):  cmd_106_55_get_jam_power,
    # Group 108
    (108,  6, RESP): resp_108_6_waveform_library,
    # Group 109
    (109, 11, CMD):  cmd_109_11_set_antenna,
    (109, 15, CMD):  cmd_109_15_get_antenna,
    (109, 17, CMD):  cmd_109_17_set_ant_gain,
    # Group 111 CMDs (VU)
    (111,  7, CMD):  cmd_111_7_get_channel,
    (111, 15, CMD):  cmd_111_15_get_channel_freq,
    (111, 19, CMD):  cmd_111_19_get_channel_mode,
    (111, 23, CMD):  cmd_111_23_get_ch_filter,
    (111, 29, CMD):  cmd_111_29_get_ch_demod,
    # Group 112
    (112,  1, CMD):  cmd_112_1_start_scan,
    (112,  3, CMD):  cmd_112_3_stop_scan,
    (112,  5, CMD):  cmd_112_5_get_scan_cfg,
    # Group 200 RESPs (VU)
    (200,  2, RESP): resp_200_2_jam_ack,
    (200,  4, RESP): resp_200_2_jam_ack,
    (200,  6, RESP): resp_200_2_jam_ack,
    (200,  8, RESP): resp_200_2_jam_ack,
}


def _dispatch(data: dict, state: DeviceState) -> list[dict]:
    """Route a telemetry message to its handler.

    Falls back to:
      - _cmd_ack()   for unregistered CMD frames   (unit N -> ACK N+1)
      - _resp_log()  for unregistered RESP frames  (log only)
    """
    frame = data.get("frame", {})
    ft    = frame.get("frame_type", "")
    g     = frame.get("group_id",  0)
    u     = frame.get("unit_id",   0)

    table = HF_DISPATCH if "hf" in state.variant else VU_DISPATCH
    handler = table.get((g, u, ft))

    if handler:
        return handler(frame, state)
    if ft == CMD:
        return _cmd_ack(frame, state)
    return _resp_log(frame, state)


# ===========================================================================
# Main server loop
# ===========================================================================

async def run(kafka: str, temp_warn: float, verbose: bool) -> None:
    global _TEMP_WARN
    _TEMP_WARN = temp_warn

    from aiokafka import AIOKafkaConsumer, AIOKafkaProducer

    producer = AIOKafkaProducer(bootstrap_servers=kafka)
    await producer.start()
    consumer = AIOKafkaConsumer(
        *TELEMETRY_TOPICS,
        bootstrap_servers=kafka,
        auto_offset_reset="latest",
        group_id="drs-server",
        enable_auto_commit=True,
    )
    await consumer.start()

    states: dict[str, DeviceState] = {}

    print(f"\n{'='*66}", flush=True)
    print(f"  DRS Server  |  kafka={kafka}  temp-warn={temp_warn}C", flush=True)
    print(f"  consuming : {', '.join(TELEMETRY_TOPICS)}", flush=True)
    print(f"  publishing: {COMMANDS_TOPIC}", flush=True)
    print(f"{'='*66}", flush=True)
    print(f"  {CYAN}RX CMD {RESET} = device requesting   -> DRS replies with ACK + data", flush=True)
    print(f"  {GREEN}RX RESP{RESET} = device pushing data  -> DRS reads telemetry & reacts", flush=True)
    print(f"  {YELLOW}TX     {RESET} = DRS command          -> Bridge encodes & sends UDP", flush=True)
    print(f"{'-'*66}\n", flush=True)

    try:
        async for msg in consumer:
            try:
                data = json.loads(msg.value)
                iid  = data.get("instance_id", "?")
                var  = data.get("variant", "?")

                if iid not in states:
                    states[iid] = DeviceState(iid, var)
                    _state(f"new device  {iid}  variant={var}")

                state = states[iid]
                state.rx_count += 1

                if verbose:
                    print(json.dumps(data, indent=2), flush=True)

                commands = _dispatch(data, state)

                for cmd in commands:
                    await producer.send_and_wait(
                        COMMANDS_TOPIC,
                        value=json.dumps(cmd).encode(),
                        key=iid.encode(),
                    )
                    state.tx_count += 1
                    g  = cmd["group_id"]
                    u  = cmd["unit_id"]
                    px = cmd.get("payload_hex", "")
                    _tx(f"CMD {g}/{u}", iid,
                        f"-> {COMMANDS_TOPIC}" + (f"  payload={px}" if px else ""))

            except Exception as exc:
                print(f"{RED}[ERROR]{RESET} {exc}", flush=True)

    except asyncio.CancelledError:
        pass
    finally:
        print(f"\n{'-'*66}", flush=True)
        for s in states.values():
            print(f"  {s.instance_id:<22}  RX={s.rx_count}  TX={s.tx_count}"
                  f"  jam={s.jam_active}  scan={s.scan_active}", flush=True)
        await consumer.stop()
        await producer.stop()


def main() -> None:
    p = argparse.ArgumentParser(description="DRS Server — full two-way device control")
    p.add_argument("--kafka",     default="localhost:9092")
    p.add_argument("--temp-warn", type=float, default=75.0,
                   help="temp threshold C to send throttle command (default 75)")
    p.add_argument("--verbose",   action="store_true",
                   help="print full raw JSON for every telemetry message")
    args = p.parse_args()
    try:
        asyncio.run(run(args.kafka, args.temp_warn, args.verbose))
    except KeyboardInterrupt:
        pass


if __name__ == "__main__":
    main()
