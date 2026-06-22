"""ctypes wrapper for the 4-symbol C++ parser ABI per B1.3 design spec.

Each variant's parser ships as a Windows DLL exporting four symbols:

    int         extract_frame(const uint8_t* buf, int buf_len,
                              uint8_t* out_frame, int* out_len);
    const char* parse_message(const uint8_t* frame, int frame_len, int frame_type);
    int         format_response(const char* json_response, uint8_t* out_frame);
    void        free_result(const char* ptr);

`load_parser(path)` opens the DLL, binds the symbols, and returns a
`ParserHandle` that satisfies the `Parser` Protocol used by the bridge.
"""
from __future__ import annotations

import ctypes
import json
from pathlib import Path

# Must match MAX_FRAME_BUFFER_BYTES in sdfc_abi.h (1 MB + overhead).
_MAX_FRAME_BYTES = 1048576 + 64


class ParserHandle:
    """Thin Pythonic wrapper around the loaded DLL's function pointers."""

    def __init__(self, lib: ctypes.CDLL) -> None:
        self._lib = lib

        # ---- extract_frame ----
        self._extract_frame = lib.extract_frame
        self._extract_frame.argtypes = [
            ctypes.POINTER(ctypes.c_uint8),  # buf
            ctypes.c_int,                    # buf_len
            ctypes.POINTER(ctypes.c_uint8),  # out_frame
            ctypes.POINTER(ctypes.c_int),    # out_len
        ]
        self._extract_frame.restype = ctypes.c_int

        # ---- parse_message ----
        self._parse_message = lib.parse_message
        self._parse_message.argtypes = [
            ctypes.POINTER(ctypes.c_uint8),  # frame
            ctypes.c_int,                    # frame_len
            ctypes.c_int,                    # frame_type
        ]
        # Returns a malloc'd char* — use c_void_p so we control free timing
        self._parse_message.restype = ctypes.c_void_p

        # ---- format_response ----
        self._format_response = lib.format_response
        self._format_response.argtypes = [
            ctypes.c_char_p,                 # json_response
            ctypes.POINTER(ctypes.c_uint8),  # out_frame (caller-allocated)
        ]
        self._format_response.restype = ctypes.c_int

        # ---- free_result ----
        self._free_result = lib.free_result
        self._free_result.argtypes = [ctypes.c_void_p]
        self._free_result.restype = None

    # ------------------------------------------------------------------
    # Public API
    # ------------------------------------------------------------------

    def extract_frame(self, buf: bytes) -> tuple[int, bytes]:
        """Scan buf for the next complete frame.

        Returns (frame_type, frame_bytes).
        frame_type: 1=command, 2=response, 3=streaming, 0=incomplete, -1=corrupt.
        frame_bytes is empty when frame_type <= 0.
        """
        n = len(buf)
        if n == 0:
            return 0, b""
        c_buf = (ctypes.c_uint8 * n).from_buffer_copy(buf)
        out_frame = (ctypes.c_uint8 * _MAX_FRAME_BYTES)()
        out_len = ctypes.c_int(0)
        frame_type = self._extract_frame(c_buf, n, out_frame, ctypes.byref(out_len))
        if frame_type <= 0:
            return frame_type, b""
        return frame_type, bytes(out_frame[: out_len.value])

    def parse_message(self, frame: bytes, frame_type: int) -> str | None:
        """Decode a complete frame into a JSON string.

        Returns the JSON string, or None on unrecoverable failure.
        The DLL-allocated memory is freed before this method returns.
        """
        n = len(frame)
        if n == 0:
            return None
        c_frame = (ctypes.c_uint8 * n).from_buffer_copy(frame)
        ptr = self._parse_message(c_frame, n, frame_type)
        if not ptr:
            return None
        try:
            return ctypes.cast(ptr, ctypes.c_char_p).value.decode("utf-8")
        finally:
            self._free_result(ptr)

    def format_response(self, kind: str, timestamp_ns: int, **kwargs) -> bytes:
        """Encode a JSON response into a binary frame.

        Returns the encoded bytes.  Raises RuntimeError on DLL error.
        """
        payload = json.dumps({"timestamp_ns": timestamp_ns, **kwargs}).encode("utf-8")
        out_frame = (ctypes.c_uint8 * _MAX_FRAME_BYTES)()
        n = self._format_response(payload, out_frame)
        if n < 0:
            raise RuntimeError(f"format_response returned {n} for kind={kind!r}")
        return bytes(out_frame[:n])

    def close(self) -> None:
        # ctypes does not expose dlclose; the OS frees on process exit.
        pass


def load_parser(dll_path: Path) -> ParserHandle:
    """Open the DLL at `dll_path` and return a bound ParserHandle.

    Raises FileNotFoundError if the path does not exist.
    """
    if not dll_path.exists():
        raise FileNotFoundError(f"parser DLL not found at {dll_path}")
    lib = ctypes.CDLL(str(dll_path))
    return ParserHandle(lib)
