"""ctypes wrapper for the 4-symbol C++ parser ABI (sdfc_abi.h).

Each variant's parser ships as a Windows DLL exporting exactly four symbols:

    int  extract_frame (const uint8_t* buf,     size_t buf_len,
                        uint8_t**      out_frame, size_t* out_len);

    int  parse_message (const uint8_t* frame,   size_t frame_len,
                        char**         out_json, size_t* out_len);

    int  format_response(const char* kind,       const char* kwargs_json,
                         uint8_t**  out_buf,      size_t*    out_len);

    void free_result   (void* ptr);

ABI contract (sdfc_abi.h):
  - All out-pointers are DLL-heap-allocated; caller MUST call free_result().
  - All functions return 0 on success, -1 on error.
  - parse_message infers frame type (command vs response) from magic bytes.
  - format_response kwargs_json MUST contain "group_id", "unit_id", "status".
    Optional "payload_hex" (hex string) is used as the frame payload.
"""
from __future__ import annotations

import ctypes
import json
from pathlib import Path


class ParserHandle:
    """Thin Pythonic wrapper around the loaded DLL's function pointers."""

    def __init__(self, lib: ctypes.CDLL) -> None:
        self._lib = lib

        # ---- extract_frame ----
        self._extract_frame = lib.extract_frame
        self._extract_frame.argtypes = [
            ctypes.POINTER(ctypes.c_uint8),                       # buf
            ctypes.c_size_t,                                       # buf_len
            ctypes.POINTER(ctypes.POINTER(ctypes.c_uint8)),       # out_frame (uint8_t**)
            ctypes.POINTER(ctypes.c_size_t),                      # out_len
        ]
        self._extract_frame.restype = ctypes.c_int

        # ---- parse_message ----
        self._parse_message = lib.parse_message
        self._parse_message.argtypes = [
            ctypes.POINTER(ctypes.c_uint8),                       # frame
            ctypes.c_size_t,                                       # frame_len
            ctypes.POINTER(ctypes.POINTER(ctypes.c_char)),        # out_json (char**)
            ctypes.POINTER(ctypes.c_size_t),                      # out_len
        ]
        self._parse_message.restype = ctypes.c_int

        # ---- format_response ----
        self._format_response = lib.format_response
        self._format_response.argtypes = [
            ctypes.c_char_p,                                       # kind
            ctypes.c_char_p,                                       # kwargs_json
            ctypes.POINTER(ctypes.POINTER(ctypes.c_uint8)),       # out_buf (uint8_t**)
            ctypes.POINTER(ctypes.c_size_t),                      # out_len
        ]
        self._format_response.restype = ctypes.c_int

        # ---- free_result ----
        self._free_result = lib.free_result
        self._free_result.argtypes = [ctypes.c_void_p]
        self._free_result.restype = None

    # ------------------------------------------------------------------
    # Public API
    # ------------------------------------------------------------------

    def extract_frame(self, buf: bytes) -> bytes | None:
        """Scan buf for the next complete, valid frame.

        Called by the TCP layer before parse_message. Returns the raw frame
        bytes, or None if buf contains no complete frame yet.
        The DLL allocates the frame buffer; it is freed before this returns.
        """
        n = len(buf)
        if n == 0:
            return None
        c_buf = (ctypes.c_uint8 * n).from_buffer_copy(buf)
        out_frame = ctypes.POINTER(ctypes.c_uint8)()
        out_len = ctypes.c_size_t(0)
        rc = self._extract_frame(
            c_buf,
            ctypes.c_size_t(n),
            ctypes.byref(out_frame),
            ctypes.byref(out_len),
        )
        if rc != 0:
            return None
        try:
            return bytes(out_frame[: out_len.value])
        finally:
            self._free_result(ctypes.cast(out_frame, ctypes.c_void_p))

    def parse_message(self, frame: bytes) -> dict | None:
        """Decode a complete frame (from extract_frame) into a dict.

        Called by the TCP layer for every frame received from the hardware.
        Frame type (command vs response) is inferred from the frame's magic
        bytes — no frame_type argument is needed.
        Returns the parsed message as a dict, or None on malformed input.
        The DLL-allocated JSON buffer is freed before this method returns.
        """
        n = len(frame)
        if n == 0:
            return None
        c_frame = (ctypes.c_uint8 * n).from_buffer_copy(frame)
        out_json = ctypes.POINTER(ctypes.c_char)()
        out_len = ctypes.c_size_t(0)
        rc = self._parse_message(
            ctypes.cast(c_frame, ctypes.POINTER(ctypes.c_uint8)),
            ctypes.c_size_t(n),
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

    def format_response(
        self,
        group_id: int,
        unit_id: int,
        status: int = 0,
        payload_hex: str = "",
    ) -> bytes | None:
        """Encode a DRS->SDFC response frame and return its binary bytes.

        Called by the Kafka layer to re-encode a JSON response back to the
        binary frame format before forwarding it via TCP.
        Returns None on encoding error.
        The DLL allocates the output buffer; it is freed before this returns.
        """
        kwargs: dict = {"group_id": group_id, "unit_id": unit_id, "status": status}
        if payload_hex:
            kwargs["payload_hex"] = payload_hex
        kwargs_json = json.dumps(kwargs).encode("utf-8")
        out_buf = ctypes.POINTER(ctypes.c_uint8)()
        out_len = ctypes.c_size_t(0)
        rc = self._format_response(
            b"response",
            kwargs_json,
            ctypes.byref(out_buf),
            ctypes.byref(out_len),
        )
        if rc != 0:
            return None
        try:
            return bytes(out_buf[: out_len.value])
        finally:
            self._free_result(ctypes.cast(out_buf, ctypes.c_void_p))

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
