import struct
from pathlib import Path

import pytest

from drs_bridge.parsers.parser_loader import ParserHandle, load_parser


def test_missing_dll_raises_fileNotFound(tmp_path: Path):
    with pytest.raises(FileNotFoundError):
        load_parser(tmp_path / "does-not-exist.dll")


def test_loader_returns_handle_with_expected_attributes():
    handle = ParserHandle.__new__(ParserHandle)
    assert hasattr(handle, "format_response")
    assert hasattr(handle, "parse_message")
    assert hasattr(handle, "extract_frame")
    assert hasattr(handle, "close")


def test_extract_frame_sdfc_command_frame(dp_ecm_hf_dll):
    """extract_frame returns (0, frame_bytes) for a complete SDFC command frame
    and (0, None) for a truncated one (sdfc_abi.h ABI)."""
    handle = load_parser(dp_ecm_hf_dll)

    # Minimal SDFC command frame: magic + size=0 + group=101 + unit=25 + footer.
    frame = (
        bytes([0xAA, 0xAB, 0xBA, 0xBB])   # CMD_HEADER
        + struct.pack("<I", 0)              # payload size
        + struct.pack("<H", 101)            # group_id
        + struct.pack("<H", 25)             # unit_id
        + bytes([0xCC, 0xCD, 0xDC, 0xDD])  # CMD_FOOTER
    )
    rc, extracted = handle.extract_frame(frame)
    assert rc == 0
    assert extracted == frame

    # Truncate by one byte → incomplete.
    rc2, extracted2 = handle.extract_frame(frame[:-1])
    assert rc2 == 0
    assert extracted2 is None

    # Unknown magic → corrupt.
    rc3, extracted3 = handle.extract_frame(b"\x00\x11\x22\x33" + b"\x00" * 12)
    assert rc3 == -1
    assert extracted3 is None
