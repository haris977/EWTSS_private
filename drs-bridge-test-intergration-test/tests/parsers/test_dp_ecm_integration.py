"""Integration tests for the dp_ecm HF and VU parser DLLs (sdfc_abi.h ABI).

These tests are Python equivalents of parsers/dp_ecm/tests/test_frames.cpp
and test_frames_vu.cpp.  They load the pre-built DLLs via parser_loader and
exercise extract_frame -> parse_message -> format_response end-to-end.

Frame wire format (little-endian, no CRC):
  COMMAND  : [AA AB BA BB][size u32][group u16][unit u16][payload][CC CD DC DD]  (16 B overhead)
  RESPONSE : [EE EF FE FF][status i16][size u32][group u16][unit u16][payload][FF FE EF EE] (18 B overhead)
"""
from __future__ import annotations

import struct
from pathlib import Path

import pytest

from drs_bridge.parsers.parser_loader import frame_type_from_bytes, load_parser

# ---- Frame constants (mirrors sdfc_frame.h) ----

CMD_HEADER  = bytes([0xAA, 0xAB, 0xBA, 0xBB])
CMD_FOOTER  = bytes([0xCC, 0xCD, 0xDC, 0xDD])
RESP_HEADER = bytes([0xEE, 0xEF, 0xFE, 0xFF])
RESP_FOOTER = bytes([0xFF, 0xFE, 0xEF, 0xEE])

CMD_OVERHEAD  = 16
RESP_OVERHEAD = 18

# Byte offset of first payload byte inside a response frame.
RESP_OFF_PAYLOAD = 14


# ---- Frame builders ----

def build_cmd(group: int, unit: int, payload: bytes = b"") -> bytes:
    return (
        CMD_HEADER
        + struct.pack("<I", len(payload))
        + struct.pack("<H", group)
        + struct.pack("<H", unit)
        + payload
        + CMD_FOOTER
    )


def build_resp(status: int, group: int, unit: int, payload: bytes = b"") -> bytes:
    return (
        RESP_HEADER
        + struct.pack("<h", status)
        + struct.pack("<I", len(payload))
        + struct.pack("<H", group)
        + struct.pack("<H", unit)
        + payload
        + RESP_FOOTER
    )


# ============================================================
# HF (libdp_ecm_hf.dll) tests
# ============================================================

class TestHfExtractFrame:
    def test_command_frame_returns_frame_bytes(self, dp_ecm_hf_dll: Path):
        handle = load_parser(dp_ecm_hf_dll)
        frame = build_cmd(101, 25, b"")
        rc, extracted = handle.extract_frame(frame)
        assert rc == 0
        assert extracted is not None
        assert len(extracted) == CMD_OVERHEAD
        assert extracted == frame

    def test_incomplete_frame_returns_none(self, dp_ecm_hf_dll: Path):
        handle = load_parser(dp_ecm_hf_dll)
        partial = build_cmd(101, 25, bytes([0x01, 0x02, 0x03, 0x04]))
        rc, extracted = handle.extract_frame(partial[:-1])
        assert rc == 0
        assert extracted is None

    def test_corrupt_footer_returns_minus_one(self, dp_ecm_hf_dll: Path):
        handle = load_parser(dp_ecm_hf_dll)
        frame = bytearray(build_cmd(101, 25, bytes([0xAA])))
        frame[-1] = 0x00  # corrupt last byte of footer
        rc, extracted = handle.extract_frame(bytes(frame))
        assert rc == -1
        assert extracted is None

    def test_unknown_magic_returns_minus_one(self, dp_ecm_hf_dll: Path):
        handle = load_parser(dp_ecm_hf_dll)
        junk = bytes([0x00, 0x11, 0x22, 0x33]) + b"\x00" * 12
        rc, extracted = handle.extract_frame(junk)
        assert rc == -1
        assert extracted is None


class TestHfParseMessage:
    def test_sysver_response_100_2(self, dp_ecm_hf_dll: Path):
        """System Version (group 100, unit 2) response has group_id/processor_id/status."""
        handle = load_parser(dp_ecm_hf_dll)
        p = bytearray(20)
        struct.pack_into("<I", p, 0, 0x00020401)   # fw_version raw
        struct.pack_into("<H", p, 16, 7)            # processor_id
        frame = build_resp(0, 100, 2, bytes(p))

        rc, extracted = handle.extract_frame(frame)
        assert rc == 0 and extracted is not None

        result = handle.parse_message(extracted, frame_type_from_bytes(extracted))
        assert result is not None
        assert result["group_id"] == 100
        assert result["processor_id"] == 7
        assert result["status"] == 0

    def test_temperature_response_100_10(self, dp_ecm_hf_dll: Path):
        """Temperature (group 100, unit 10) response exposes temp fields."""
        handle = load_parser(dp_ecm_hf_dll)
        p = struct.pack("<ffff", 35.5, 28.0, 52.1, 61.3)  # internal, external, cpu, fpga
        frame = build_resp(0, 100, 10, p)

        rc, extracted = handle.extract_frame(frame)
        assert rc == 0 and extracted is not None

        result = handle.parse_message(extracted, frame_type_from_bytes(extracted))
        assert result is not None
        assert result["group_id"] == 100
        assert result["unit_id"] == 10
        assert "internal_temp_c" in result
        assert "fpga_temp_c" in result

    def test_fh_detection_response_101_40(self, dp_ecm_hf_dll: Path):
        """FH Detection (group 101, unit 40) decodes hopper array."""
        handle = load_parser(dp_ecm_hf_dll)
        # 4-byte header: hopper_count=1, reserved=0
        hdr = struct.pack("<HH", 1, 0)
        # 40-byte hopper: hopper_number=3, min_freq=5.0 MHz (as float), active=1
        hop = bytearray(40)
        struct.pack_into("<I", hop, 0, 3)           # hopper_number
        struct.pack_into("<f", hop, 4, 5.0)         # min_freq MHz (float)
        struct.pack_into("<H", hop, 32, 1)           # active
        frame = build_resp(0, 101, 40, hdr + bytes(hop))

        rc, extracted = handle.extract_frame(frame)
        assert rc == 0 and extracted is not None

        result = handle.parse_message(extracted, frame_type_from_bytes(extracted))
        assert result is not None
        assert result["hopper_count"] == 1
        hop0 = result["hoppers"][0]
        assert hop0["hopper_number"] == 3
        assert hop0["min_freq_hz"] == pytest.approx(5_000_000.0, rel=1e-4)
        assert hop0["active"] is True

    def test_wideband_fft_response_101_44(self, dp_ecm_hf_dll: Path):
        """Wideband FFT (group 101, unit 44) exposes point_count/start_freq_hz/power_dbm."""
        handle = load_parser(dp_ecm_hf_dll)
        # Header: start_MHz=1, stop_MHz=30, step_kHz=1000, point_count=3
        hdr = struct.pack("<IIII", 1, 30, 1000, 3)
        # 3 power floats (dBm)
        powers = struct.pack("<fff", -45.5, -50.0, -55.1)
        frame = build_resp(0, 101, 44, hdr + powers)

        rc, extracted = handle.extract_frame(frame)
        assert rc == 0 and extracted is not None

        result = handle.parse_message(extracted, frame_type_from_bytes(extracted))
        assert result is not None
        assert result["unit_id"] == 44
        assert result["point_count"] == 3
        assert result["start_freq_hz"] == pytest.approx(1_000_000.0, rel=1e-4)
        assert "power_dbm" in result

    def test_unknown_unit_fallback_raw_hex(self, dp_ecm_hf_dll: Path):
        """Unknown unit (101/999) returns raw_hex fallback without crashing."""
        handle = load_parser(dp_ecm_hf_dll)
        frame = build_resp(0, 101, 999, bytes([0xDE, 0xAD, 0xBE, 0xEF]))

        rc, extracted = handle.extract_frame(frame)
        assert rc == 0 and extracted is not None

        result = handle.parse_message(extracted, frame_type_from_bytes(extracted))
        assert result is not None
        assert result.get("raw_hex") == "deadbeef"


class TestHfFormatResponse:
    def test_ack_frame_round_trips(self, dp_ecm_hf_dll: Path):
        """format_response({group_id, unit_id, status}) produces a valid
        18-byte response frame that extract_frame accepts."""
        handle = load_parser(dp_ecm_hf_dll)
        frame_bytes = handle.format_response({"group_id": 101, "unit_id": 26, "status": 0})
        assert isinstance(frame_bytes, bytes)
        assert len(frame_bytes) == RESP_OVERHEAD

        rc, extracted = handle.extract_frame(frame_bytes)
        assert rc == 0
        assert extracted is not None
        assert frame_type_from_bytes(extracted) == 2  # response

    def test_payload_hex_embedded_correctly(self, dp_ecm_hf_dll: Path):
        """payload_hex field is appended as raw bytes in the frame."""
        handle = load_parser(dp_ecm_hf_dll)
        # 4-byte payload: 0xDEADBEEF LE
        frame_bytes = handle.format_response(
            {"group_id": 101, "unit_id": 26, "status": 0, "payload_hex": "deadbeef"}
        )
        assert len(frame_bytes) == RESP_OVERHEAD + 4
        assert frame_bytes[RESP_OFF_PAYLOAD: RESP_OFF_PAYLOAD + 4] == bytes([0xDE, 0xAD, 0xBE, 0xEF])


# ============================================================
# VU (libdp_ecm_vu.dll) tests
# ============================================================

class TestVuExtractFrame:
    def test_command_frame_returned(self, dp_ecm_vu_dll: Path):
        handle = load_parser(dp_ecm_vu_dll)
        frame = build_cmd(200, 1, b"")
        rc, extracted = handle.extract_frame(frame)
        assert rc == 0
        assert extracted is not None
        assert len(extracted) == CMD_OVERHEAD

    def test_incomplete_frame(self, dp_ecm_vu_dll: Path):
        handle = load_parser(dp_ecm_vu_dll)
        partial = build_resp(0, 200, 2, bytes([0x01, 0x02, 0x03, 0x04]))
        rc, extracted = handle.extract_frame(partial[:-1])
        assert rc == 0
        assert extracted is None

    def test_corrupt_footer(self, dp_ecm_vu_dll: Path):
        handle = load_parser(dp_ecm_vu_dll)
        frame = bytearray(build_cmd(200, 1, bytes([0xAA])))
        frame[-1] = 0x00
        rc, extracted = handle.extract_frame(bytes(frame))
        assert rc == -1


class TestVuParseMessage:
    def test_sysver_response_100_2(self, dp_ecm_vu_dll: Path):
        """VU System Version includes hw=dp_ecm_vu and fpga_type_id."""
        handle = load_parser(dp_ecm_vu_dll)
        p = bytearray(20)
        struct.pack_into("<I", p, 0, 0x00010200)   # fw_version raw
        struct.pack_into("<H", p, 16, 12)           # processor_id
        struct.pack_into("<H", p, 18, 3)            # fpga_type_id
        frame = build_resp(0, 100, 2, bytes(p))

        rc, extracted = handle.extract_frame(frame)
        assert rc == 0 and extracted is not None

        result = handle.parse_message(extracted, frame_type_from_bytes(extracted))
        assert result is not None
        assert result["hw"] == "dp_ecm_vu"
        assert result["group_id"] == 100
        assert result["unit_id"] == 2
        assert result["processor_id"] == 12
        assert result["fpga_type_id"] == 3
        assert result["status"] == 0

    def test_fh_detection_response_101_40_vu_band(self, dp_ecm_vu_dll: Path):
        """VU FH Detection at 150 MHz — frequency converted to Hz."""
        handle = load_parser(dp_ecm_vu_dll)
        hdr = struct.pack("<HH", 1, 0)  # hopper_count=1, reserved
        hop = bytearray(40)
        struct.pack_into("<I", hop, 0, 7)             # hopper_number
        struct.pack_into("<f", hop, 4, 150.0)         # min_freq 150 MHz
        struct.pack_into("<f", hop, 8, 155.0)         # max_freq 155 MHz
        struct.pack_into("<H", hop, 32, 1)             # active
        frame = build_resp(0, 101, 40, hdr + bytes(hop))

        rc, extracted = handle.extract_frame(frame)
        assert rc == 0 and extracted is not None

        result = handle.parse_message(extracted, frame_type_from_bytes(extracted))
        assert result is not None
        assert result["hopper_count"] == 1
        hop0 = result["hoppers"][0]
        assert hop0["hopper_number"] == 7
        assert hop0["min_freq_hz"] == pytest.approx(150_000_000.0, rel=1e-4)
        assert hop0["active"] is True

    def test_immediate_jam_ack_200_2(self, dp_ecm_vu_dll: Path):
        """Immediate Jam ACK (group 200, unit 2) has jam_kind/jam_id/jam_active."""
        handle = load_parser(dp_ecm_vu_dll)
        p = bytearray(8)
        struct.pack_into("<H", p, 0, 42)  # jam_id
        struct.pack_into("<H", p, 4, 1)   # active = true
        frame = build_resp(0, 200, 2, bytes(p))

        rc, extracted = handle.extract_frame(frame)
        assert rc == 0 and extracted is not None

        result = handle.parse_message(extracted, frame_type_from_bytes(extracted))
        assert result is not None
        assert result["group_id"] == 200
        assert result["unit_id"] == 2
        assert result["jam_kind"] == "immediate_jam"
        assert result["jam_id"] == 42
        assert result["jam_active"] is True

    def test_follow_on_jam_ack_200_4(self, dp_ecm_vu_dll: Path):
        """Follow-On Jam ACK (200/4) — status-only ACK."""
        handle = load_parser(dp_ecm_vu_dll)
        frame = build_resp(0, 200, 4, b"")

        rc, extracted = handle.extract_frame(frame)
        assert rc == 0 and extracted is not None

        result = handle.parse_message(extracted, frame_type_from_bytes(extracted))
        assert result is not None
        assert result["jam_kind"] == "follow_on_jam"
        assert result["status"] == 0

    def test_jam_list_ack_200_6_error_status(self, dp_ecm_vu_dll: Path):
        """Jam List ACK with status=-1 includes status_name='Error'."""
        handle = load_parser(dp_ecm_vu_dll)
        frame = build_resp(-1, 200, 6, b"")

        rc, extracted = handle.extract_frame(frame)
        assert rc == 0 and extracted is not None

        result = handle.parse_message(extracted, frame_type_from_bytes(extracted))
        assert result is not None
        assert result["jam_kind"] == "jam_list"
        assert result["status"] == -1
        assert result["status_name"] == "Error"

    def test_responsive_sweep_200_8(self, dp_ecm_vu_dll: Path):
        """Responsive Sweep Jam ACK (200/8) with jam_active=false."""
        handle = load_parser(dp_ecm_vu_dll)
        p = bytearray(8)
        struct.pack_into("<H", p, 0, 99)  # jam_id
        struct.pack_into("<H", p, 4, 0)   # active = false
        frame = build_resp(0, 200, 8, bytes(p))

        rc, extracted = handle.extract_frame(frame)
        assert rc == 0 and extracted is not None

        result = handle.parse_message(extracted, frame_type_from_bytes(extracted))
        assert result is not None
        assert result["jam_kind"] == "responsive_sweep"
        assert result["jam_id"] == 99
        assert result["jam_active"] is False

    def test_unknown_unit_fallback_raw_hex(self, dp_ecm_vu_dll: Path):
        """Unknown group/unit falls through to raw_hex without crashing."""
        handle = load_parser(dp_ecm_vu_dll)
        frame = build_resp(0, 112, 6, bytes([0xCA, 0xFE, 0xBA, 0xBE]))

        rc, extracted = handle.extract_frame(frame)
        assert rc == 0 and extracted is not None

        result = handle.parse_message(extracted, frame_type_from_bytes(extracted))
        assert result is not None
        assert result.get("raw_hex") == "cafebabe"


class TestVuFormatResponse:
    def test_ack_frame_round_trips(self, dp_ecm_vu_dll: Path):
        """format_response ACK for 200/2 produces a valid 18-byte response frame."""
        handle = load_parser(dp_ecm_vu_dll)
        frame_bytes = handle.format_response({"group_id": 200, "unit_id": 2, "status": 0})
        assert len(frame_bytes) == RESP_OVERHEAD

        rc, extracted = handle.extract_frame(frame_bytes)
        assert rc == 0
        assert frame_type_from_bytes(extracted) == 2

    def test_payload_hex_round_trips_jam_fields(self, dp_ecm_vu_dll: Path):
        """payload_hex '2a0000000100' (jam_id=42, active=1) embeds into frame correctly."""
        handle = load_parser(dp_ecm_vu_dll)
        frame_bytes = handle.format_response(
            {"group_id": 200, "unit_id": 2, "status": 0, "payload_hex": "2a0000000100"}
        )
        assert len(frame_bytes) == RESP_OVERHEAD + 6
        jam_id = struct.unpack_from("<H", frame_bytes, RESP_OFF_PAYLOAD)[0]
        assert jam_id == 42
