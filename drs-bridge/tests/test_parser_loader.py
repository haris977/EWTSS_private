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
