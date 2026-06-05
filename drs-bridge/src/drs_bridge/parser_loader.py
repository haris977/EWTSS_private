"""ctypes wrapper for the 4-symbol C++ parser ABI per B1.3 design spec.

Each variant's parser ships as a Windows DLL exporting four symbols:

    int     extract_frame(const uint8_t* buf, size_t length, void** out_frame);
    int     parse_message(void* frame, char** out_json, size_t* out_len);
    int     format_response(const char* kind, const char* kwargs_json,
                            uint8_t** out_buf, size_t* out_len);
    void    free_result(void* ptr);

`load_parser(path)` opens the DLL, binds the symbols, and returns a
`ParserHandle` that satisfies the `Parser` Protocol used by `TimeBeaconCoroutine`.
"""
from __future__ import annotations

import ctypes
import json
from pathlib import Path


class ParserHandle:
    """Thin Pythonic wrapper around the loaded DLL's function pointers."""

    def __init__(self, lib: ctypes.CDLL) -> None:
        self._lib = lib
        # Bind ABI signatures. Done lazily to keep the constructor cheap.
        self._format_response = lib.format_response
        self._format_response.argtypes = [
            ctypes.c_char_p,                                  # kind
            ctypes.c_char_p,                                  # kwargs as JSON
            ctypes.POINTER(ctypes.POINTER(ctypes.c_uint8)),   # out_buf
            ctypes.POINTER(ctypes.c_size_t),                  # out_len
        ]
        self._format_response.restype = ctypes.c_int

        self._free_result = lib.free_result
        self._free_result.argtypes = [ctypes.c_void_p]
        self._free_result.restype = None

    def format_response(self, kind: str, timestamp_ns: int, **kwargs) -> bytes:
        payload = json.dumps({"timestamp_ns": timestamp_ns, **kwargs}).encode("utf-8")
        out_buf = ctypes.POINTER(ctypes.c_uint8)()
        out_len = ctypes.c_size_t(0)
        rc = self._format_response(kind.encode("utf-8"), payload, ctypes.byref(out_buf), ctypes.byref(out_len))
        if rc != 0:
            raise RuntimeError(f"format_response returned {rc} for kind={kind}")
        try:
            return bytes(ctypes.cast(out_buf, ctypes.POINTER(ctypes.c_uint8 * out_len.value)).contents)
        finally:
            self._free_result(ctypes.cast(out_buf, ctypes.c_void_p))

    def parse_message(self, frame_ptr) -> dict:
        raise NotImplementedError("parse_message wiring lands when a real parser ships")

    def extract_frame(self, buf: bytes) -> tuple[int, object]:
        raise NotImplementedError("extract_frame wiring lands when a real parser ships")

    def close(self) -> None:
        # ctypes does not expose dlclose; the OS frees on process exit.
        pass


def load_parser(dll_path: Path) -> ParserHandle:
    """Open the DLL at `dll_path` and return a ParserHandle bound to it.

    Raises FileNotFoundError if the path does not exist.
    """
    if not dll_path.exists():
        raise FileNotFoundError(f"parser DLL not found at {dll_path}")
    lib = ctypes.CDLL(str(dll_path))
    return ParserHandle(lib)
