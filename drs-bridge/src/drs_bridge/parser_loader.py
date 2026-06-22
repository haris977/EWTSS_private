"""ctypes wrapper for the 4-symbol C++ parser ABI per B1.3 design spec.

Each variant's parser ships as a Windows DLL exporting four symbols:

    int     extract_frame(const uint8_t* buf, size_t length,
                          uint8_t** out_frame, size_t* out_len);
    int     parse_message(const uint8_t* frame, size_t frame_len,
                          char** out_json, size_t* out_len);
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
            ctypes.c_char_p,  # kind
            ctypes.c_char_p,  # kwargs as JSON
            ctypes.POINTER(ctypes.POINTER(ctypes.c_uint8)),  # out_buf
            ctypes.POINTER(ctypes.c_size_t),  # out_len
        ]
        self._format_response.restype = ctypes.c_int

        self._free_result = lib.free_result
        self._free_result.argtypes = [ctypes.c_void_p]
        self._free_result.restype = None

        self._extract_frame = lib.extract_frame
        self._extract_frame.argtypes = [
            ctypes.POINTER(ctypes.c_uint8),  # buf
            ctypes.c_size_t,  # length
            ctypes.POINTER(ctypes.POINTER(ctypes.c_uint8)),  # out_frame
            ctypes.POINTER(ctypes.c_size_t),  # out_len
        ]
        self._extract_frame.restype = ctypes.c_int

        self._parse_message = lib.parse_message
        self._parse_message.argtypes = [
            ctypes.POINTER(ctypes.c_uint8),                   # frame
            ctypes.c_size_t,                                   # frame_len
            ctypes.POINTER(ctypes.POINTER(ctypes.c_char)),    # out_json (char**)
            ctypes.POINTER(ctypes.c_size_t),                  # out_len
        ]
        self._parse_message.restype = ctypes.c_int

    def format_response(self, kind: str, timestamp_ns: int, **kwargs) -> bytes:
        payload = json.dumps({"timestamp_ns": timestamp_ns, **kwargs}).encode("utf-8")
        out_buf = ctypes.POINTER(ctypes.c_uint8)()
        out_len = ctypes.c_size_t(0)
        rc = self._format_response(
            kind.encode("utf-8"), payload, ctypes.byref(out_buf), ctypes.byref(out_len)
        )
        if rc != 0:
            raise RuntimeError(f"format_response returned {rc} for kind={kind}")
        try:
            return bytes(
                ctypes.cast(out_buf, ctypes.POINTER(ctypes.c_uint8 * out_len.value)).contents
            )
        finally:
            self._free_result(ctypes.cast(out_buf, ctypes.c_void_p))

    def parse_message(self, frame: bytes) -> dict | None:
        """Decode a complete frame (from extract_frame) into a dict.

        Called by the TCP layer for every frame received from the hardware.
        Returns the parsed message as a dict, or None on malformed input.
        The DLL-allocated JSON buffer is freed before this method returns.
        """
        if not frame:
            return None
        in_buf = (ctypes.c_uint8 * len(frame)).from_buffer_copy(frame)
        out_json = ctypes.POINTER(ctypes.c_char)()
        out_len = ctypes.c_size_t(0)
        rc = self._parse_message(
            ctypes.cast(in_buf, ctypes.POINTER(ctypes.c_uint8)),
            ctypes.c_size_t(len(frame)),
            ctypes.byref(out_json),
            ctypes.byref(out_len),
        )
        if rc != 0:
            return None
        try:
            raw = ctypes.string_at(out_json, out_len.value)
            return json.loads(raw.decode("utf-8"))
        finally:
            self._free_result(ctypes.cast(out_json, ctypes.c_void_p))

    def extract_frame(self, buf: bytes) -> tuple[int, bytes | None]:
        """Scan `buf` for a complete frame. Returns (rc, frame_bytes).

        rc == 0  -> a complete frame was extracted (frame_bytes set).
        rc != 0  -> no complete frame yet (frame_bytes is None). Callers buffer
                    more bytes and retry; the framing-probation logic in
                    transport.py decides when 'no frame yet' becomes a failure.
        """
        if not buf:
            return -1, None
        in_buf = (ctypes.c_uint8 * len(buf)).from_buffer_copy(buf)
        out_frame = ctypes.POINTER(ctypes.c_uint8)()
        out_len = ctypes.c_size_t(0)
        rc = self._extract_frame(
            ctypes.cast(in_buf, ctypes.POINTER(ctypes.c_uint8)),
            ctypes.c_size_t(len(buf)),
            ctypes.byref(out_frame),
            ctypes.byref(out_len),
        )
        if rc != 0:
            return rc, None
        try:
            frame = bytes(
                ctypes.cast(out_frame, ctypes.POINTER(ctypes.c_uint8 * out_len.value)).contents
            )
        finally:
            self._free_result(ctypes.cast(out_frame, ctypes.c_void_p))
        return 0, frame

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
