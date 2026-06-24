"""ctypes wrapper for the 4-symbol C ABI defined in sdfc_abi.h.

Each variant DLL exports exactly four symbols with C linkage:

    int  extract_frame (const uint8_t* buf, size_t buf_len,
                        uint8_t** out_frame, size_t* out_len)
         Returns 0 on success (complete frame extracted),
         -1 if no complete frame found (incomplete or corrupt header).
         *out_frame is DLL-heap-allocated; caller MUST call free_result(*out_frame).

    int  parse_message (const uint8_t* frame, size_t frame_len,
                        char** out_json, size_t* out_len)
         Returns 0 on success, -1 on failure.
         Frame type is inferred from magic bytes — no frame_type param.
         *out_json is DLL-heap-allocated; caller MUST call free_result(*out_json).

    int  format_response (const char* kind, const char* kwargs_json,
                          uint8_t** out_buf, size_t* out_len)
         Returns 0 on success, -1 on failure.
         *out_buf is DLL-heap-allocated; caller MUST call free_result(*out_buf).

    void free_result (void* ptr)
         Frees any DLL-heap pointer. Safe with NULL.
"""

from __future__ import annotations

import ctypes
import json
import os
from pathlib import Path

# Magic bytes for quick frame-type detection without re-calling the DLL.
_CMD_MAGIC  = bytes([0xAA, 0xAB, 0xBA, 0xBB])
_RESP_MAGIC = bytes([0xEE, 0xEF, 0xFE, 0xFF])


def frame_type_from_bytes(frame: bytes) -> int:
    """Detect SDFC frame type from the leading 4 magic bytes.

    Returns 1 (command), 2 (response), or 0 (unrecognised).
    """
    if len(frame) < 4:
        return 0
    magic = frame[:4]
    if magic == _CMD_MAGIC:
        return 1
    if magic == _RESP_MAGIC:
        return 2
    return 0


class ParserHandle:
    """Thin Pythonic wrapper around the loaded DLL's four function pointers."""

    def __init__(self, lib: ctypes.CDLL) -> None:
        self._lib = lib

        # ── free_result ───────────────────────────────────────────────────────
        # void free_result(void* ptr)
        self._free_result = lib.free_result
        self._free_result.argtypes = [ctypes.c_void_p]
        self._free_result.restype  = None

        # ── extract_frame ─────────────────────────────────────────────────────
        # int extract_frame(const uint8_t* buf, size_t buf_len,
        #                   uint8_t** out_frame, size_t* out_len)
        # *out_frame is DLL-heap-allocated — use free_result after reading.
        self._extract_frame = lib.extract_frame
        self._extract_frame.argtypes = [
            ctypes.POINTER(ctypes.c_uint8),                   # buf
            ctypes.c_size_t,                                   # buf_len
            ctypes.POINTER(ctypes.POINTER(ctypes.c_uint8)),   # out_frame**
            ctypes.POINTER(ctypes.c_size_t),                  # out_len
        ]
        self._extract_frame.restype = ctypes.c_int

        # ── parse_message ─────────────────────────────────────────────────────
        # int parse_message(const uint8_t* frame, size_t frame_len,
        #                   char** out_json, size_t* out_len)
        # *out_json is DLL-heap-allocated — use free_result after reading.
        # Frame type is inferred from magic bytes; no frame_type argument.
        self._parse_message = lib.parse_message
        self._parse_message.argtypes = [
            ctypes.POINTER(ctypes.c_uint8),   # frame
            ctypes.c_size_t,                   # frame_len
            ctypes.POINTER(ctypes.c_char_p),  # out_json**
            ctypes.POINTER(ctypes.c_size_t),  # out_len
        ]
        self._parse_message.restype = ctypes.c_int

        # ── format_response ───────────────────────────────────────────────────
        # int format_response(const char* kind, const char* kwargs_json,
        #                     uint8_t** out_buf, size_t* out_len)
        # *out_buf is DLL-heap-allocated — use free_result after reading.
        self._format_response = lib.format_response
        self._format_response.argtypes = [
            ctypes.c_char_p,                                   # kind
            ctypes.c_char_p,                                   # kwargs_json
            ctypes.POINTER(ctypes.POINTER(ctypes.c_uint8)),   # out_buf**
            ctypes.POINTER(ctypes.c_size_t),                  # out_len
        ]
        self._format_response.restype = ctypes.c_int

    # ── extract_frame ─────────────────────────────────────────────────────────

    def extract_frame(self, buf: bytes) -> tuple[int, bytes | None]:
        """Scan ``buf`` for a complete SDFC frame.

        Returns ``(rc, frame_bytes)``:
          - ``(0, bytes)``   — complete frame extracted.
          - ``(-1, None)``   — incomplete or corrupt; need more bytes or resync.

        Transport contract: rc != 0 or frame_bytes is None → no frame yet.
        """
        if not buf:
            return -1, None

        in_buf      = (ctypes.c_uint8 * len(buf)).from_buffer_copy(buf)
        out_frame_p = ctypes.POINTER(ctypes.c_uint8)()   # null — DLL fills it
        out_len     = ctypes.c_size_t(0)

        rc = self._extract_frame(
            ctypes.cast(in_buf, ctypes.POINTER(ctypes.c_uint8)),
            ctypes.c_size_t(len(buf)),
            ctypes.byref(out_frame_p),
            ctypes.byref(out_len),
        )

        if rc != 0:
            return rc, None

        try:
            frame_bytes = bytes(out_frame_p[: out_len.value])
        finally:
            self._free_result(ctypes.cast(out_frame_p, ctypes.c_void_p))

        return 0, frame_bytes

    # ── parse_message ─────────────────────────────────────────────────────────

    def parse_message(self, frame: bytes, frame_type: int = 0) -> dict | None:
        """Decode a complete SDFC frame into a Python dict.

        ``frame_type`` is accepted for API compatibility but is NOT passed to
        the DLL — the DLL infers type from the frame's magic bytes.

        Returns the parsed JSON as a dict, or None on failure.
        All numeric scalars are in SI units (Hz, dBm, seconds).
        """
        if not frame:
            return None

        in_buf   = (ctypes.c_uint8 * len(frame)).from_buffer_copy(frame)
        out_json = ctypes.c_char_p(None)
        out_len  = ctypes.c_size_t(0)

        rc = self._parse_message(
            ctypes.cast(in_buf, ctypes.POINTER(ctypes.c_uint8)),
            ctypes.c_size_t(len(frame)),
            ctypes.byref(out_json),
            ctypes.byref(out_len),
        )

        if rc != 0 or not out_json.value:
            return None

        try:
            return json.loads(out_json.value.decode("utf-8"))
        finally:
            self._free_result(ctypes.cast(out_json, ctypes.c_void_p))

    # ── format_response ───────────────────────────────────────────────────────

    def format_response(self, json_response: dict) -> bytes:
        """Encode a response dict into a binary DRS→SDFC response frame.

        ``json_response`` must contain ``group_id`` (int), ``unit_id`` (int),
        ``status`` (int). Any additional fields are forwarded to the DLL as
        the kwargs_json payload.

        Returns the raw frame bytes, or raises RuntimeError on failure.
        """
        kind       = b"response"
        kwargs     = json.dumps(json_response).encode("utf-8")
        out_buf_p  = ctypes.POINTER(ctypes.c_uint8)()   # null — DLL fills it
        out_len    = ctypes.c_size_t(0)

        rc = self._format_response(
            kind,
            kwargs,
            ctypes.byref(out_buf_p),
            ctypes.byref(out_len),
        )

        if rc != 0:
            raise RuntimeError(f"format_response failed (rc={rc})")

        try:
            return bytes(out_buf_p[: out_len.value])
        finally:
            self._free_result(ctypes.cast(out_buf_p, ctypes.c_void_p))

    def close(self) -> None:
        pass


def load_parser(dll_path: Path) -> ParserHandle:
    """Open the DLL at ``dll_path`` and return a bound ParserHandle.

    Raises FileNotFoundError if the path does not exist.
    On Windows (Python ≥ 3.8) adds the DLL's directory to the search path
    so sibling runtime DLLs are found without needing them on PATH.
    """
    if not dll_path.exists():
        raise FileNotFoundError(f"parser DLL not found at {dll_path}")
    if hasattr(os, "add_dll_directory"):
        os.add_dll_directory(str(dll_path.resolve().parent))
    lib = ctypes.CDLL(str(dll_path))
    return ParserHandle(lib)
