from pathlib import Path

import pytest

from drs_bridge.parser_loader import ParserHandle, load_parser


def test_missing_dll_raises_fileNotFound(tmp_path: Path):
    with pytest.raises(FileNotFoundError):
        load_parser(tmp_path / "does-not-exist.dll")


def test_loader_returns_handle_with_expected_attributes():
    # Stub class to verify the public API surface (we'd use a real DLL in lab).
    handle = ParserHandle.__new__(ParserHandle)
    assert hasattr(handle, "format_response")
    assert hasattr(handle, "parse_message")
    assert hasattr(handle, "extract_frame")
    assert hasattr(handle, "close")


def test_extract_frame_accepts_reference_time_frame(built_reference_parser):
    handle = load_parser(built_reference_parser)
    # reference frame: 0xAA magic, len=6, 4-byte LE seconds=1, 2 reserved
    frame = bytes([0xAA, 0x06, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00])
    rc, extracted = handle.extract_frame(frame)
    assert rc == 0
    assert extracted == frame

    rc2, extracted2 = handle.extract_frame(b"\x00\x00\x00")  # no magic
    assert rc2 == -1
    assert extracted2 is None
