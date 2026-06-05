"""End-to-end test: build the reference C++ parser via CMake, load the
resulting shared library via parser_loader.load_parser, and exercise
format_response over the ctypes binding.

This is the first test in the repo that actually invokes C code from
Python. If the argtypes/restype declarations in parser_loader.py drift
from the C ABI in parsers/reference/include/reference_parser.h, this
test fails — regardless of whether unit-level tests still pass.
"""
import struct
from pathlib import Path

import pytest

from drs_bridge.parser_loader import load_parser


def test_format_response_time_round_trip(built_reference_parser: Path):
    """format_response(kind='time', timestamp_ns=...) must produce a
    well-formed reference frame: 0xAA magic, 0x06 length, 4-byte LE
    seconds, 2 reserved zero bytes."""
    handle = load_parser(built_reference_parser)

    timestamp_ns = 1_700_000_000_000_000_000  # ~Nov 2023
    expected_seconds = timestamp_ns // 1_000_000_000

    frame = handle.format_response(kind="time", timestamp_ns=timestamp_ns)

    assert isinstance(frame, bytes)
    assert len(frame) == 8
    assert frame[0] == 0xAA          # magic
    assert frame[1] == 0x06          # payload length
    actual_seconds, = struct.unpack("<I", frame[2:6])
    # uint32 wrap is acceptable; the reference parser truncates to 32 bits.
    assert actual_seconds == (expected_seconds & 0xFFFFFFFF)
    assert frame[6] == 0x00
    assert frame[7] == 0x00


def test_format_response_unknown_kind_raises(built_reference_parser: Path):
    """The reference parser returns -1 for any kind other than 'time';
    parser_loader's wrapper translates that into a RuntimeError."""
    handle = load_parser(built_reference_parser)
    with pytest.raises(RuntimeError, match="format_response returned"):
        handle.format_response(kind="unknown-kind", timestamp_ns=0)
