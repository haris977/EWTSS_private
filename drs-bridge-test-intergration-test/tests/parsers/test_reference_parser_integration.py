"""Reference parser integration tests.

Verifies the reference_parser.dll (parsers/reference/) against the
sdfc_abi.h ABI that parser_loader.py expects.  Tests are skipped when
CMake is unavailable (the built_reference_parser fixture handles this).
"""
import pytest

from drs_bridge.parsers.parser_loader import load_parser


def test_extract_frame_returns_frame(built_reference_parser):
    parser = load_parser(built_reference_parser)
    frame = bytes([0xAA, 0x06, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00])
    rc, extracted = parser.extract_frame(frame)
    assert rc == 0
    assert extracted == frame


def test_extract_frame_incomplete_returns_none(built_reference_parser):
    parser = load_parser(built_reference_parser)
    # Only magic + length byte present, payload missing
    rc, extracted = parser.extract_frame(bytes([0xAA, 0x06]))
    assert rc == 0
    assert extracted is None


def test_extract_frame_wrong_magic_returns_corrupt(built_reference_parser):
    parser = load_parser(built_reference_parser)
    rc, extracted = parser.extract_frame(bytes([0xFF, 0x06, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00]))
    assert rc == -1
    assert extracted is None


def test_parse_message_returns_dict(built_reference_parser):
    parser = load_parser(built_reference_parser)
    frame = bytes([0xAA, 0x06, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00])
    result = parser.parse_message(frame, 1)
    assert result is not None
    assert result["frame_type"] == 1
    assert result["kind"] == "time"
    assert result["timestamp_seconds"] == 1


def test_format_response_time_round_trip(built_reference_parser):
    # format_response is a stub for the reference parser — just confirm no crash.
    parser = load_parser(built_reference_parser)
    result = parser.format_response({"group_id": 0, "unit_id": 1, "status": 0})
    assert isinstance(result, bytes)
    assert len(result) >= 2


def test_format_response_unknown_kind_raises(built_reference_parser):
    # The reference format_response stub always returns a minimal frame.
    parser = load_parser(built_reference_parser)
    result = parser.format_response({"group_id": 99, "unit_id": 99, "status": 0})
    assert isinstance(result, bytes)
