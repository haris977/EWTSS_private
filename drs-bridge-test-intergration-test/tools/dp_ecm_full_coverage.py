#!/usr/bin/env python3
"""Full-coverage test - sends every known HF/VU dispatch entry to drs-bridge
and reports what the DLL decoded for each one.

Usage:
  python tools/dp_ecm_full_coverage.py --variant hf --port 19001
  python tools/dp_ecm_full_coverage.py --variant vu --port 19011
  python tools/dp_ecm_full_coverage.py --variant hf --port 19001 --delay 0.1
"""
from __future__ import annotations

import argparse
import asyncio
import json
import random
import struct
import sys
from dataclasses import dataclass
from typing import Callable

# -- SDFC frame builders ----------------------------------------------------

CMD_HEADER  = bytes([0xAA, 0xAB, 0xBA, 0xBB])
CMD_FOOTER  = bytes([0xCC, 0xCD, 0xDC, 0xDD])
RESP_HEADER = bytes([0xEE, 0xEF, 0xFE, 0xFF])
RESP_FOOTER = bytes([0xFF, 0xFE, 0xEF, 0xEE])


def build_cmd(group: int, unit: int, payload: bytes = b"") -> bytes:
    return (CMD_HEADER + struct.pack("<I", len(payload))
            + struct.pack("<H", group) + struct.pack("<H", unit)
            + payload + CMD_FOOTER)


def build_resp(group: int, unit: int, payload: bytes = b"", status: int = 0) -> bytes:
    return (RESP_HEADER + struct.pack("<h", status) + struct.pack("<I", len(payload))
            + struct.pack("<H", group) + struct.pack("<H", unit)
            + payload + RESP_FOOTER)


# -- Known payload builders -------------------------------------------------

def _rf(lo: float, hi: float) -> float:
    return random.uniform(lo, hi)


def _hf_temp() -> bytes:
    # 9 sensors: internal, external, cpu, fpga, board, pa, rx, adc, psu
    return struct.pack("<9f",
        _rf(20.0, 45.0),   # internal_temp_c
        _rf(15.0, 40.0),   # external_temp_c
        _rf(45.0, 90.0),   # cpu_temp_c
        _rf(50.0, 95.0),   # fpga_temp_c
        _rf(20.0, 45.0),   # board_temp_c
        _rf(55.0, 100.0),  # pa_temp_c
        _rf(25.0, 55.0),   # rx_temp_c
        _rf(60.0, 100.0),  # adc_temp_c
        _rf(30.0, 65.0),   # psu_temp_c
    )


def _vu_temp() -> bytes:
    # 6 sensors: processor, board, pa, rx, adc, psu
    return struct.pack("<6f",
        _rf(20.0, 45.0),   # processor_temp_c
        _rf(15.0, 40.0),   # board_temp_c
        _rf(45.0, 90.0),   # pa_temp_c
        _rf(55.0, 100.0),  # rx_temp_c
        _rf(20.0, 50.0),   # adc_temp_c
        _rf(30.0, 60.0),   # psu_temp_c
    )


def _hf_fh_detection() -> bytes:
    count = random.randint(1, 3)
    header = struct.pack("<HH", count, 0)
    out = header
    for i in range(count):
        min_f = _rf(20.0, 480.0)
        max_f = min_f + _rf(2.0, 20.0)
        min_det = min_f - _rf(0.0, 1.0)
        max_det = max_f + _rf(0.0, 1.0)
        h = random.randint(0, 23)
        m = random.randint(0, 59)
        s = random.randint(0, 59)
        entry = (struct.pack("<I", i + 1)
                 + struct.pack("<ff", min_f, max_f)
                 + struct.pack("<ff", _rf(0.5, 5.0), _rf(50.0, 500.0))
                 + struct.pack("<I", random.randint(1, 500))
                 + bytes([h, m, s, 0])
                 + struct.pack("<f", _rf(-120.0, -30.0))
                 + struct.pack("<HH", 1, 0)
                 + struct.pack("<f", _rf(3.0, 35.0))
                 + struct.pack("<ffff", min_det, max_det, _rf(5.0, 50.0), _rf(50.0, 1000.0))
                 + struct.pack("<f", _rf(0.5, 1.0)))
        assert len(entry) == 60, f"HF hopper must be 60B, got {len(entry)}"
        out += entry
    return out


def _vu_fh_detection() -> bytes:
    count = random.randint(1, 3)
    header = struct.pack("<HH", count, 0)
    out = header
    for i in range(count):
        min_f = _rf(100.0, 480.0)
        max_f = min_f + _rf(2.0, 15.0)
        h = random.randint(0, 23)
        m = random.randint(0, 59)
        s = random.randint(0, 59)
        entry = (struct.pack("<I", i + 1)
                 + struct.pack("<ff", min_f, max_f)
                 + struct.pack("<ff", _rf(0.5, 5.0), _rf(50.0, 500.0))
                 + struct.pack("<I", random.randint(1, 200))
                 + bytes([h, m, s, 0])
                 + struct.pack("<f", _rf(-120.0, -30.0))
                 + struct.pack("<HH", 1, 0)
                 + struct.pack("<f", _rf(3.0, 30.0)))
        assert len(entry) == 40, f"VU hopper must be 40B, got {len(entry)}"
        out += entry
    return out


def _hf_jam_ack() -> bytes:
    jam_id = random.randint(1, 15)
    active = random.randint(0, 1)
    return struct.pack("<HHHH", jam_id, 0, active, 0)


def _vu_jam_ack() -> bytes:
    freq = random.randint(100_000_000, 500_000_000)
    return struct.pack("<I", random.randint(1, 8)) + struct.pack("<IHH", freq, 2, 0)


def _set_threshold() -> bytes:
    ch = random.randint(0, 7)
    thr = _rf(-120.0, -40.0)
    return struct.pack("<BBBBf", ch, 0, 0, 0, thr)


# -- Command payload builders (ICD-required input parameters) ----------------

def _cmd_fh_detection(start: float = 20.0, stop: float = 80.0) -> bytes:
    # 101/39: start_freq_mhz (f32le) + stop_freq_mhz (f32le) — 8 bytes
    return struct.pack("<ff", start, stop)


def _cmd_ff_burst_detection(start: int = 20, stop: int = 80) -> bytes:
    # 101/69 and 101/83: start_mhz (u16le) + stop_mhz (u16le) + rf_atten (u8) + if_atten (i8) + 2 rsv
    return struct.pack("<HHBbxx", start, stop, 0, 0)


def _cmd_wideband_fft(start: int = 20, stop: int = 80) -> bytes:
    # 101/43: start_mhz (u16le) + stop_mhz (u16le) + rf_atten (u8) + if_atten (u8) + 2 rsv
    return struct.pack("<HHBBxx", start, stop, 0, 0)


# -- HF response payload builders -------------------------------------------

def _hf_system_version() -> bytes:
    # 28 bytes: 4 fw floats + processor_id + 3 rf_tuner_ids + fpga_type_id + reserved
    return struct.pack("<4fHHHHHH",
        _rf(2.0, 5.0), _rf(1.0, 3.0), _rf(2.0, 4.0), _rf(1.0, 2.0),
        0x0A01, 0x1200, 0x1201, 0x1202, 0x0B01, 0)


def _hf_srx_checksum() -> bytes:
    return bytes(random.randint(0, 255) for _ in range(1000))


def _hf_pbit_status() -> bytes:
    return bytes(88)


def _hf_ibit_status() -> bytes:
    return bytes(68)


def _hf_fan_speed() -> bytes:
    return struct.pack("<I", random.randint(1500, 4500))


def _hf_uart_test() -> bytes:
    return bytes([0xAA, 0xAA, 1, 0])


def _hf_cbit_status() -> bytes:
    return bytes(8)


def _hf_wideband_fft() -> bytes:
    # 16-byte header (center f32, start f32, stop f32, count u16, rsv u16) + count*4 floats
    count = 12
    center = _rf(100.0, 500.0)
    return (struct.pack("<fffHH", center, center - 25.0, center + 25.0, count, 0)
            + struct.pack(f"<{count}f", *[_rf(-120.0, -40.0) for _ in range(count)]))


def _hf_ff_detection() -> bytes:
    # Mandatory 6408-byte FFT block (4-hdr + 1600*4 + 4-trl), then count + 60B entries
    fft_vals = [_rf(-120.0, -30.0)] * 10 + [-100.0] * 1590
    fft_block = (struct.pack("<I", 10)
                 + struct.pack("<1600f", *fft_vals)
                 + struct.pack("<I", 5))
    count = 1
    min_f = _rf(100.0, 400.0)
    max_f = min_f + _rf(2.0, 20.0)
    entry = (struct.pack("<I", 1)
             + struct.pack("<ff", min_f, max_f)
             + struct.pack("<ff", _rf(0.5, 5.0), _rf(50.0, 500.0))
             + struct.pack("<I", random.randint(1, 500))
             + bytes([10, 30, 0, 0])
             + struct.pack("<f", _rf(-120.0, -30.0))
             + struct.pack("<HH", 1, 0)
             + struct.pack("<f", _rf(3.0, 35.0))
             + struct.pack("<ffff", min_f - 1, max_f + 1, _rf(5.0, 50.0), _rf(50.0, 1000.0))
             + struct.pack("<f", _rf(0.5, 1.0)))
    assert len(entry) == 60
    return fft_block + struct.pack("<HH", count, 0) + entry


def _hf_burst_detection() -> bytes:
    count = 2
    return struct.pack("<HH", count, 0) + bytes(52 * count)


def _hf_stop_scan_speed() -> bytes:
    count = 1
    min_f = _rf(100.0, 400.0)
    max_f = min_f + _rf(2.0, 20.0)
    entry = (struct.pack("<I", 1)
             + struct.pack("<ff", min_f, max_f)
             + struct.pack("<ff", _rf(0.5, 5.0), _rf(50.0, 500.0))
             + struct.pack("<I", random.randint(1, 500))
             + bytes([10, 30, 0, 0])
             + struct.pack("<f", _rf(-120.0, -30.0))
             + struct.pack("<HH", 1, 0)
             + struct.pack("<f", _rf(3.0, 35.0))
             + struct.pack("<ffff", min_f - 1, max_f + 1, _rf(5.0, 50.0), _rf(50.0, 1000.0))
             + struct.pack("<f", _rf(0.5, 1.0)))
    assert len(entry) == 60
    return struct.pack("<HH", count, 0) + entry


def _hf_zoom_fft() -> bytes:
    return struct.pack("<20f", *[_rf(-120.0, -30.0) for _ in range(20)])


def _hf_signal_bite_resp() -> bytes:
    return struct.pack("<ffHH", _rf(20.0, 500.0), _rf(-90.0, -10.0), 1, 0)


def _hf_pdw_channelization() -> bytes:
    count = 2
    return struct.pack("<HHHH", count, 0, 0, 0) + bytes(40 * count)


def _hf_storage_details() -> bytes:
    return bytes([75, 80, 90, 0]) + struct.pack("<d", 512e9) + struct.pack("<d", 1024e9)


def _hf_fast_scan_resp() -> bytes:
    return bytes([1, 0, 0, 0])


def _hf_list_jam_report() -> bytes:
    count = 3
    entries = b"".join(
        struct.pack("<IHH", random.randint(100_000_000, 500_000_000), 1, 0)
        for _ in range(count)
    )
    return struct.pack("<HH", count, 0) + entries


def _hf_hpasu_health_rsp() -> bytes:
    return bytes(4)


def _hf_pa_sdu_health_rsp() -> bytes:
    return bytes(8)


# -- MRx shared response payload builders (Groups 1, 3, 4, 5, 6, 7) --------

def _mrx_system_version() -> bytes:
    return struct.pack("<fffHH", _rf(1.0, 5.0), _rf(1.0, 3.0), _rf(2.0, 4.0), 0x1200, 0)


def _mrx_checksum() -> bytes:
    return bytes(1024)


def _mrx_pbit_status() -> bytes:
    return bytes(124)


def _mrx_ibit_status() -> bytes:
    return bytes(112)


def _mrx_temp() -> bytes:
    return struct.pack("<10f", *[_rf(20.0, 75.0) for _ in range(10)])


def _mrx_fan_speed() -> bytes:
    return struct.pack("<I", random.randint(1500, 4500))


def _mrx_uart_test_rsp() -> bytes:
    return bytes([0xAA, 0xAA, 1, 0])


def _mrx_cbit_status() -> bytes:
    return bytes(8)


def _mrx_board_count() -> bytes:
    return struct.pack("<HH", 1, 0)


def _mrx_channel_16b() -> bytes:
    return struct.pack("<HHHHHHHH", 1, 0, 0, 0, 0, 0, 0, 0)


def _mrx_tuning_details() -> bytes:
    return struct.pack("<BBHHBB", 1, 1, 0, 100, 0, 0)


def _mrx_audio_data() -> bytes:
    return struct.pack("<I", 0)


def _mrx_iq_start_rsp() -> bytes:
    return bytes([0, 1, 1, 0])


def _mrx_iq_logging_stop() -> bytes:
    path = b"/data/iq/capture.bin\x00" + bytes(107)
    return struct.pack("<HH", 0, 0) + path


def _mrx_memory_scan_data() -> bytes:
    count = 3
    return struct.pack("<HHHH", count, 0, 0, 0) + bytes(20 * count)


def _mrx_ddc_fft() -> bytes:
    count = 16
    return struct.pack("<HH", count, 0) + struct.pack(f"<{count}f", *[_rf(-120.0, -30.0) for _ in range(count)])


def _mrx_smart_scan_read() -> bytes:
    return struct.pack("<dHHf", _rf(100e6, 500e6), 0, 0, _rf(-90.0, -20.0))


def _mrx_agc_status() -> bytes:
    return struct.pack("<BBH", 10, 15, 1)


def _mrx_signal_bite_rsp() -> bytes:
    return struct.pack("<dfHH", _rf(100e6, 500e6), _rf(-90.0, -20.0), 1, 0)


# -- Entry definition --------------------------------------------------------

@dataclass
class Entry:
    kind: str   # "cmd" or "resp"
    group: int
    unit: int
    payload_fn: Callable[[], bytes] | None = None

    @property
    def label(self) -> str:
        return f"{'CMD' if self.kind == 'cmd' else 'RSP'} {self.group:3d}/{self.unit:<3d}"

    def frame(self) -> bytes:
        p = self.payload_fn() if self.payload_fn else b""
        if self.kind == "cmd":
            return build_cmd(self.group, self.unit, p)
        return build_resp(self.group, self.unit, p)


def _c(g: int, u: int, fn: Callable | None = None) -> Entry:
    return Entry("cmd", g, u, fn)


def _r(g: int, u: int, fn: Callable | None = None) -> Entry:
    return Entry("resp", g, u, fn)


# -- HF (DP-ECM-1071) - all dispatch entries --------------------------------

HF_ENTRIES: list[Entry] = [
    # Group 100 commands
    _c(100, 17),
    # Group 101 commands
    _c(101, 25, _set_threshold), _c(101, 27), _c(101, 37),
    _c(101, 39, lambda: _cmd_fh_detection()),
    _c(101, 43, lambda: _cmd_wideband_fft()),
    _c(101, 47), _c(101, 55),
    _c(101, 69, lambda: _cmd_ff_burst_detection()),
    _c(101, 83, lambda: _cmd_ff_burst_detection()),
    _c(101, 85), _c(101, 87), _c(101, 94), _c(101, 140), _c(101, 158),
    _c(101, 160), _c(101, 162), _c(101, 164), _c(101, 174), _c(101, 176),
    _c(101, 178), _c(101, 182), _c(101, 184), _c(101, 186), _c(101, 200),
    _c(101, 202), _c(101, 204), _c(101, 210),
    # Group 109 commands
    _c(109, 11),
    # Group 111 commands
    _c(111,  3), _c(111,  5), _c(111,  9), _c(111, 13), _c(111, 15),
    _c(111, 17), _c(111, 19), _c(111, 25),
    # Group 112 commands
    _c(112,  1), _c(112,  5), _c(112, 13), _c(112, 37),
    # Group 200 commands
    _c(200,  9), _c(200, 11), _c(200, 13), _c(200, 15), _c(200, 17),
    _c(200, 19), _c(200, 21), _c(200, 23), _c(200, 41), _c(200, 54), _c(200, 56),
    # MRx Group 1 commands
    _c(1,  1), _c(1,  3), _c(1,  5), _c(1,  7), _c(1,  9),
    _c(1, 13), _c(1, 17), _c(1, 25), _c(1, 33),
    # MRx Group 3 commands
    _c(3,  1), _c(3, 17), _c(3, 19), _c(3, 21), _c(3, 23),
    # MRx Group 4 commands
    _c(4,  5), _c(4,  7), _c(4,  9), _c(4, 11), _c(4, 15), _c(4, 17),
    _c(4, 23), _c(4, 25), _c(4, 33), _c(4, 35), _c(4, 39), _c(4, 41),
    _c(4, 43), _c(4, 53), _c(4, 55), _c(4, 57), _c(4, 59), _c(4, 61), _c(4, 63),
    # MRx Group 5 commands
    _c(5,  1), _c(5,  3), _c(5,  9), _c(5, 13),
    # MRx Group 6 commands
    _c(6,  7), _c(6,  9), _c(6, 11), _c(6, 13), _c(6, 15),
    # MRx Group 7 commands
    _c(7,  1), _c(7,  3), _c(7,  5), _c(7,  9), _c(7, 11),
    _c(7, 13), _c(7, 15), _c(7, 17), _c(7, 19), _c(7, 21), _c(7, 23),

    # Group 100 responses
    _r(100,  2, _hf_system_version), _r(100,  4, _hf_srx_checksum),
    _r(100,  6, _hf_pbit_status),    _r(100,  8, _hf_ibit_status),
    _r(100, 10, _hf_temp), _r(100, 14, _hf_fan_speed),
    _r(100, 18, _hf_uart_test), _r(100, 26, _hf_cbit_status),
    # Group 101 responses
    _r(101, 26), _r(101, 28), _r(101, 38), _r(101, 40, _hf_fh_detection),
    _r(101, 44, _hf_wideband_fft), _r(101, 48), _r(101, 56),
    _r(101, 70, _hf_ff_detection), _r(101, 84, _hf_burst_detection),
    _r(101, 86), _r(101, 88, _hf_stop_scan_speed), _r(101, 95, _hf_zoom_fft),
    _r(101, 141), _r(101, 159),
    _r(101, 161), _r(101, 163), _r(101, 165), _r(101, 175), _r(101, 177),
    _r(101, 179), _r(101, 183), _r(101, 185), _r(101, 187), _r(101, 201),
    _r(101, 203), _r(101, 205),
    # Group 109 responses
    _r(109, 12),
    # Group 111 responses
    _r(111,  4, _hf_signal_bite_resp), _r(111,  6), _r(111, 10), _r(111, 14),
    _r(111, 16, _hf_pdw_channelization), _r(111, 18), _r(111, 20),
    _r(111, 26, _hf_storage_details), _r(111, 211),
    # Group 112 responses
    _r(112,  2, _hf_fast_scan_resp), _r(112,  6), _r(112, 14), _r(112, 38),
    # Group 200 responses
    _r(200, 10), _r(200, 12), _r(200, 14), _r(200, 16, _hf_list_jam_report),
    _r(200, 18), _r(200, 20), _r(200, 22), _r(200, 24),
    _r(200, 42, _hf_hpasu_health_rsp), _r(200, 55, _hf_pa_sdu_health_rsp), _r(200, 57),
    # Group 106 responses
    _r(106, 54, _hf_jam_ack),
    # MRx Group 1 responses
    _r(1,  2, _mrx_system_version), _r(1,  4, _mrx_checksum),
    _r(1,  6, _mrx_pbit_status),    _r(1,  8, _mrx_ibit_status),
    _r(1, 10, _mrx_temp), _r(1, 14, _mrx_fan_speed),
    _r(1, 18, _mrx_uart_test_rsp),  _r(1, 26, _mrx_cbit_status), _r(1, 34),
    # MRx Group 3 responses
    _r(3,  2, _mrx_board_count), _r(3, 18, _mrx_channel_16b),
    _r(3, 20), _r(3, 22, _mrx_channel_16b), _r(3, 24, _mrx_tuning_details),
    # MRx Group 4 responses
    _r(4,  6), _r(4,  8, _mrx_audio_data), _r(4, 10), _r(4, 12), _r(4, 16), _r(4, 18),
    _r(4, 24, _mrx_iq_start_rsp), _r(4, 26, _mrx_iq_logging_stop),
    _r(4, 34, _mrx_iq_start_rsp), _r(4, 36), _r(4, 40),
    _r(4, 42, _mrx_memory_scan_data), _r(4, 44, _mrx_ddc_fft),
    _r(4, 54), _r(4, 56), _r(4, 58), _r(4, 60), _r(4, 62, _mrx_smart_scan_read), _r(4, 64),
    # MRx Group 5 responses
    _r(5,  2), _r(5,  4), _r(5, 10), _r(5, 14, _mrx_agc_status),
    # MRx Group 6 responses
    _r(6,  8), _r(6, 10), _r(6, 12), _r(6, 14, _mrx_iq_start_rsp), _r(6, 16),
    # MRx Group 7 responses
    _r(7,  2, _mrx_signal_bite_rsp), _r(7,  4), _r(7,  6), _r(7, 10), _r(7, 12),
    _r(7, 14), _r(7, 16), _r(7, 18), _r(7, 20), _r(7, 22), _r(7, 24),
]

# -- VU (DP-ECM-1074) - all dispatch entries --------------------------------

VU_ENTRIES: list[Entry] = [
    # Group 101 commands
    _c(101, 25, _set_threshold), _c(101, 27), _c(101, 31), _c(101, 33),
    _c(101, 37), _c(101, 39), _c(101, 43), _c(101, 47), _c(101, 55),
    _c(101, 63), _c(101, 69), _c(101, 73), _c(101, 75), _c(101, 79),
    _c(101, 83), _c(101, 85), _c(101, 87), _c(101, 92), _c(101, 94),
    _c(101, 100), _c(101, 102), _c(101, 104), _c(101, 106), _c(101, 158),
    # Group 111 commands
    _c(111,  5), _c(111,  7), _c(111,  9), _c(111, 13), _c(111, 15),
    _c(111, 17), _c(111, 19), _c(111, 21), _c(111, 23), _c(111, 25),
    _c(111, 27), _c(111, 29), _c(111, 31),
    # Group 106 commands
    _c(106,  1), _c(106,  3), _c(106,  5), _c(106,  9), _c(106, 21),
    _c(106, 39), _c(106, 41), _c(106, 45), _c(106, 49), _c(106, 55),
    # Group 109 commands
    _c(109, 11), _c(109, 15), _c(109, 17),
    # Group 112 commands
    _c(112,  1), _c(112,  3), _c(112,  5),
    # MRx Group 1 commands
    _c(1,  1), _c(1,  3), _c(1,  5), _c(1,  7), _c(1,  9), _c(1, 33),
    # MRx Group 3 commands
    _c(3,  1), _c(3, 17), _c(3, 19), _c(3, 21), _c(3, 25),
    # MRx Group 4 commands
    _c(4,  5), _c(4,  7), _c(4,  9), _c(4, 11), _c(4, 15), _c(4, 17),
    _c(4, 23), _c(4, 25), _c(4, 33), _c(4, 35), _c(4, 39), _c(4, 41),
    _c(4, 43), _c(4, 57), _c(4, 65), _c(4, 67), _c(4, 69), _c(4, 71),
    # MRx Group 5 commands
    _c(5,  1), _c(5,  3),
    # MRx Group 6 commands
    _c(6,  9), _c(6, 11), _c(6, 13), _c(6, 15),
    # MRx Group 7 commands
    _c(7,  1), _c(7,  3), _c(7,  5), _c(7,  9), _c(7, 11),
    _c(7, 13), _c(7, 15), _c(7, 17), _c(7, 19), _c(7, 21),

    # Group 100 responses
    _r(100,  2, _hf_system_version), _r(100,  4, _hf_srx_checksum),
    _r(100,  6, _hf_pbit_status),    _r(100,  8, _hf_ibit_status),
    _r(100, 10, _vu_temp), _r(100, 12), _r(100, 14), _r(100, 16),
    _r(100, 18), _r(100, 22), _r(100, 24), _r(100, 26), _r(100, 28), _r(100, 30),
    # Group 101 responses
    _r(101, 26), _r(101, 28), _r(101, 32), _r(101, 34), _r(101, 38),
    _r(101, 40, _vu_fh_detection), _r(101, 44, _hf_wideband_fft), _r(101, 48), _r(101, 56),
    _r(101, 64), _r(101, 70, _hf_ff_detection), _r(101, 74), _r(101, 76), _r(101, 80),
    _r(101, 84, _hf_burst_detection), _r(101, 86), _r(101, 88, _hf_stop_scan_speed),
    _r(101, 93), _r(101, 95, _hf_zoom_fft),
    _r(101, 101), _r(101, 103), _r(101, 105), _r(101, 107), _r(101, 159),
    # Group 111 responses
    _r(111,  6), _r(111,  8), _r(111, 10), _r(111, 14), _r(111, 16),
    _r(111, 18), _r(111, 20), _r(111, 22), _r(111, 24), _r(111, 26),
    _r(111, 28), _r(111, 30), _r(111, 32),
    # Group 106 responses
    _r(106,  2), _r(106,  4), _r(106,  6), _r(106, 10), _r(106, 22),
    _r(106, 40), _r(106, 42), _r(106, 46), _r(106, 50), _r(106, 56),
    # Group 108 responses
    _r(108,  6),
    # Group 109 responses
    _r(109, 12), _r(109, 16), _r(109, 18),
    # Group 112 responses
    _r(112,  2), _r(112,  4), _r(112,  6),
    # Group 200 responses
    _r(200,  2, _vu_jam_ack), _r(200,  4, _vu_jam_ack),
    _r(200,  6, _vu_jam_ack), _r(200,  8, _vu_jam_ack),
    # MRx Group 1 responses
    _r(1,  2, _mrx_system_version), _r(1,  4, _mrx_checksum),
    _r(1,  6, _mrx_pbit_status),    _r(1,  8, _mrx_ibit_status),
    _r(1, 10, _mrx_temp), _r(1, 34),
    # MRx Group 3 responses
    _r(3,  2, _mrx_board_count), _r(3, 18, _mrx_channel_16b),
    _r(3, 20), _r(3, 22, _mrx_channel_16b), _r(3, 26),
    # MRx Group 4 responses
    _r(4,  6), _r(4,  8, _mrx_audio_data), _r(4, 10), _r(4, 12), _r(4, 16), _r(4, 18),
    _r(4, 24, _mrx_iq_start_rsp), _r(4, 26, _mrx_iq_logging_stop),
    _r(4, 34, _mrx_iq_start_rsp), _r(4, 36), _r(4, 40),
    _r(4, 42, _mrx_memory_scan_data), _r(4, 44, _mrx_ddc_fft),
    _r(4, 58), _r(4, 66), _r(4, 68), _r(4, 70), _r(4, 72),
    # MRx Group 5 responses
    _r(5,  2), _r(5,  4),
    # MRx Group 6 responses
    _r(6, 10), _r(6, 12), _r(6, 14, _mrx_iq_start_rsp), _r(6, 16),
    # MRx Group 7 responses
    _r(7,  2, _mrx_signal_bite_rsp), _r(7,  4), _r(7,  6), _r(7, 10), _r(7, 12),
    _r(7, 14), _r(7, 16), _r(7, 18), _r(7, 20), _r(7, 22),
]


# -- Runner -----------------------------------------------------------------

async def run_coverage(
    host: str,
    port: int,
    variant: str,
    kafka_bootstrap: str,
    delay: float,
) -> None:
    from aiokafka import AIOKafkaConsumer

    entries = HF_ENTRIES if variant == "hf" else VU_ENTRIES
    topic = f"hw.dp_ecm_{variant}.telemetry"

    print(f"\n[coverage] {variant.upper()} - {len(entries)} entries")
    print(f"[coverage] bridge  : {host}:{port}")
    print(f"[coverage] topic   : {topic}")
    print(f"[coverage] delay   : {delay}s/frame  (~{len(entries)*delay:.0f}s total)\n")

    # Start Kafka consumer and seek to end of topic
    consumer = AIOKafkaConsumer(
        topic,
        bootstrap_servers=kafka_bootstrap,
        auto_offset_reset="latest",
        group_id=None,
        enable_auto_commit=False,
    )
    await consumer.start()
    await consumer.seek_to_end()

    # Connect to bridge TCP port
    try:
        reader, writer = await asyncio.open_connection(host, port)
    except (OSError, asyncio.CancelledError) as exc:
        print(f"[coverage] ERROR: cannot connect to {host}:{port} - {exc}", file=sys.stderr)
        print("[coverage] Is drs-bridge running with a roster entry on this port?", file=sys.stderr)
        await consumer.stop()
        sys.exit(1)

    print(f"[coverage] connected - sending {len(entries)} frames...")

    for i, entry in enumerate(entries, 1):
        writer.write(entry.frame())
        await writer.drain()
        if i % 50 == 0:
            print(f"[coverage]   {i}/{len(entries)} sent...")
        await asyncio.sleep(delay)

    writer.close()
    try:
        await writer.wait_closed()
    except Exception:
        pass

    print(f"[coverage] all frames sent - reading Kafka (timeout={max(5.0, len(entries)*delay + 3.0):.0f}s)...\n")

    # Collect messages
    received: list[dict] = []
    deadline = asyncio.get_event_loop().time() + max(5.0, len(entries) * delay + 3.0)

    while len(received) < len(entries):
        remaining = deadline - asyncio.get_event_loop().time()
        if remaining <= 0:
            break
        try:
            batch = await asyncio.wait_for(
                consumer.getmany(max_records=100), timeout=min(remaining, 1.0)
            )
            for _tp, messages in batch.items():
                for msg in messages:
                    received.append(json.loads(msg.value))
        except asyncio.TimeoutError:
            pass

    await consumer.stop()

    # Print results table
    SKIP = {"hw", "frame_type", "group_id", "unit_id", "message_size_bytes",
            "status", "status_name"}
    decoded_count = raw_hex_count = no_reply_count = 0

    print(f"{'#':<5} {'Entry':<14} {'Result':<10} Details")
    print("-" * 72)

    for i, entry in enumerate(entries):
        idx = f"{i+1}."
        if i < len(received):
            frame = received[i].get("frame", {})
            if "raw_hex" in frame:
                result = "raw_hex"
                detail = frame["raw_hex"][:24] + ("..." if len(frame["raw_hex"]) > 24 else "")
                raw_hex_count += 1
            else:
                fields = [k for k in frame if k not in SKIP]
                if fields:
                    result = "DECODED"
                    parts = []
                    for k in fields[:3]:
                        v = frame[k]
                        s = f"{k}={v!r}" if not isinstance(v, list) else f"{k}=[{len(v)} items]"
                        parts.append(s[:28])
                    detail = "  ".join(parts)
                    decoded_count += 1
                else:
                    result = "ack"
                    detail = "(0-byte ACK - no payload)"
                    decoded_count += 1
        else:
            result = "NO REPLY"
            detail = "Kafka message not received (timeout or bridge dropped frame)"
            no_reply_count += 1

        print(f"{idx:<5} {entry.label:<14} {result:<10} {detail}")

    # Summary
    print("-" * 72)
    print(f"\nResults for {variant.upper()} ({len(entries)} entries):")
    print(f"  DECODED / ack : {decoded_count:3d}  - DLL parsed the frame")
    print(f"  raw_hex       : {raw_hex_count:3d}  - Frame known but no field decoder (passes through)")
    print(f"  NO REPLY      : {no_reply_count:3d}  - Frame not received in Kafka (bridge dropped or timeout)")
    print()


def main() -> None:
    p = argparse.ArgumentParser(
        description="Send all DLL dispatch entries to drs-bridge and report DLL decode results",
    )
    p.add_argument("--host",    default="127.0.0.1", help="Bridge host (default: 127.0.0.1)")
    p.add_argument("--port",    type=int, required=True, help="Bridge TCP command port")
    p.add_argument("--variant", choices=["hf", "vu"], required=True, help="hf or vu")
    p.add_argument("--kafka",   default="localhost:9092", help="Kafka bootstrap (default: localhost:9092)")
    p.add_argument("--delay",   type=float, default=0.05,
                   help="Delay between frames in seconds (default: 0.05)")
    args = p.parse_args()
    asyncio.run(run_coverage(args.host, args.port, args.variant, args.kafka, args.delay))


if __name__ == "__main__":
    main()
