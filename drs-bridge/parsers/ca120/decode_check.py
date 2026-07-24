"""Ad-hoc CA120 decode-verification tool.

Purpose: run real (or synthetic) input frames through the actual compiled
ca120.dll via ctypes -- the same extract_frame -> parse_message path
drs-bridge uses in production -- and write the input bytes next to the
decoded JSON so a human can eyeball whether the output matches what the
real device's ICD (R&S-CA120-ICD-V15) says it should be.

CA120 has two channels, and this script covers both:
  - XML control (TCP 9001) -- raw, unwrapped UTF-8 XML (<Request>/<Reply>/
    <Event>). No length prefix or magic wrapper, unlike DDF-550's 9150.
  - AMMOS binary (TCP >9200, dynamic) -- magic-synced (0xFB746572,
    little-endian) streaming data frames, one socket per active stream.

This is the "physical verification" companion to test_frames_ca120.cpp
(native CTest, synthetic-only). That suite can't tell you whether a REAL
CA120 unit's wire bytes decode the way you expect -- this script is where
you paste in an actual capture (e.g. from Wireshark on port 9001 or an
AMMOS data port) and check.

Usage:
    python decode_check.py                       # run the built-in vectors
    python decode_check.py --hex <hexstring>      # decode one real capture
    python decode_check.py --hex-file <path>      # one hex string per line

Output: printed to stdout AND appended to decode_report.txt in this
directory (input bytes + decoded JSON side by side), so there's always a
persistent record to compare against the ICD by hand.

Only exercises extract_frame + parse_message (the request/decode side).
format_response is intentionally out of scope here -- see format_check.py.
Note format_response only ever produces XML <Request> frames -- AMMOS is
CA120-to-bridge only, so there is no AMMOS encode direction to verify.
"""
from __future__ import annotations

import argparse
import json
import struct
import sys
from pathlib import Path

_REPO_ROOT = Path(__file__).resolve().parents[3]  # .../ewtss-v2-design-main
sys.path.insert(0, str(_REPO_ROOT / "drs-bridge" / "src"))

from drs_bridge.parser_loader import load_parser  # noqa: E402

_BUILD_DIR = Path(__file__).resolve().parent / "build"
_REPORT_FILE = Path(__file__).resolve().parent / "decode_report.txt"


def find_dll() -> Path:
    for name in ("ca120.dll", "libca120.dll"):
        candidates = list(_BUILD_DIR.glob(f"**/{name}"))
        if candidates:
            return candidates[0]
    raise FileNotFoundError(
        f"No built ca120 DLL found under {_BUILD_DIR} -- run "
        f"'cmake -S . -B build && cmake --build build --target ca120' first."
    )


# ---------------------------------------------------------------------------
# AMMOS frame builders (mirrors the C++ helpers in tests/test_frames_ca120.cpp
# -- build_ammos / make_if_dh / make_audio_dh -- kept in sync with those by
# hand; if the ICD-derived header layouts there change, update here too)
# ---------------------------------------------------------------------------

AMMOS_MAGIC = 0xFB746572


def build_ammos(frame_type: int, frame_count: int, dh_bytes: bytes, payload: bytes = b"") -> bytes:
    total = 24 + len(dh_bytes) + len(payload)
    pad = (-total) % 4
    frame_words = (total + pad) // 4
    dh_words = len(dh_bytes) // 4
    header = struct.pack(
        "<IIIIII",
        AMMOS_MAGIC, frame_words, frame_count, frame_type, dh_words, 0,
    )
    return header + dh_bytes + payload + b"\x00" * pad


def make_if_dh(ts_lo=100_000, freq_lo=10_000_000, bw=1_560_000, sr=2_000_000, source_id=42) -> bytes:
    # 56-byte IF Data_Header (14 x uint32, ICD §6.1)
    return struct.pack(
        "<IIIIIIIIIIIIII",
        1,          # DatablockCount
        256,        # DatablockLength (words)
        ts_lo, 0,   # timestamp lo/hi (us)
        0,          # StatusWord
        source_id,  # SourceID
        1,          # SourceState
        freq_lo, 0, # CenterFreq lo/hi
        bw,         # Bandwidth
        sr,         # SampleRate
        1, 1,       # Interpolation, Decimation
        1000,       # AntVoltageRef (0.1 dBuV)
    )


def make_spectrum_dh(fft_length=1024, window_type=2, display_mode=6) -> bytes:
    # 40-byte Spectrum Data_Header (ICD §6.4)
    status_word = (window_type << 4) | display_mode
    return struct.pack(
        "<IIIIIIIIII",
        500, 0,           # timestamp lo/hi (us)
        100_000_000, 0,   # center_freq lo/hi
        2_000_000,        # sample_rate
        fft_length,
        status_word,
        0,                # ref_value (float, encoded as raw bits; 0.0 == all-zero)
        0, 1023,          # left_bin, right_bin
    )


BUILTIN_VECTORS: list[tuple[str, bytes]] = [
    (
        "XML Reply -- Available Demodulators/Decoders, two subsystem roots + comments (ICD §5.6.4)",
        (
            '<Reply type="get" id="70054">'
            '<DigitalDemodulator>'
            '<AvailableDemodulators>'
            '<DemodulatorInfo>'
            '<DemodulatorName>ASK2</DemodulatorName>'
            '<DemodulatorVersion>1</DemodulatorVersion>'
            '<ModuleID>1048576</ModuleID>'
            '<ParameterSize>9</ParameterSize>'
            '<SupportsSymbolData>1</SupportsSymbolData>'
            '<SupportsIQ_ConstellationData>0</SupportsIQ_ConstellationData>'
            '<SupportsInstantData>1</SupportsInstantData>'
            '<SupportsImageData>0</SupportsImageData>'
            '<SupportsTransmissionData>0</SupportsTransmissionData>'
            '<SupportsAudioData>0</SupportsAudioData>'
            '<IsUniversal>1</IsUniversal>'
            '<SupportsSpecialData>0</SupportsSpecialData>'
            '</DemodulatorInfo>'
            '<!-- More DemodulatorInfo nodes follow for each available demodulator -->'
            '</AvailableDemodulators>'
            '</DigitalDemodulator>'
            '<BitstreamProcessing>'
            '<AvailableDecoders>'
            '<Decoder id="100000" classificationOnly="1">'
            '<DecoderName>ASCII</DecoderName>'
            '</Decoder>'
            '<!-- More Decoder nodes follow for each available decoder -->'
            '</AvailableDecoders>'
            '</BitstreamProcessing>'
            '</Reply>'
        ).encode("utf-8"),
    ),
]


def decode_one(handle, label: str, wire: bytes) -> dict:
    result: dict = {"label": label, "input_hex": wire.hex()}
    result["input_readable"] = wire.decode("ascii", errors="backslashreplace")
    frame = handle.extract_frame(wire)
    if frame is None:
        result["error"] = "extract_frame: no complete frame recognised"
        return result
    result["frame_hex"] = frame.hex()
    parsed = handle.parse_message(frame)
    if parsed is None:
        result["error"] = "parse_message: failed to decode frame"
        return result
    result["output_json"] = parsed
    return result


_BOUNDARY = "-" * 100


def extract_id(output_json: dict | None) -> str | None:
    """Best-effort pull of body.<msg_kind>.id (or .stream for AMMOS frames)
    so each block's boundary header shows which message it is at a glance."""
    if not output_json:
        return None
    msg_kind = output_json.get("msg_kind")
    body = output_json.get("body")
    if isinstance(body, dict) and msg_kind in body and isinstance(body[msg_kind], dict):
        if "id" in body[msg_kind]:
            return str(body[msg_kind]["id"])
    if "stream" in output_json:  # AMMOS frames have no id/type -- show stream+frame_count instead
        return f"stream={output_json['stream']} frame_count={output_json.get('frame_count')}"
    return None


def format_entry(entry: dict) -> str:
    id_str = extract_id(entry.get("output_json"))
    header = f"VECTOR: {entry['label']}"
    if id_str:
        header += f"   [id={id_str}]"

    lines = [_BOUNDARY, header, _BOUNDARY, f"input_hex     : {entry['input_hex']}"]
    lines.append(f"input_readable: {entry['input_readable']}")
    if "frame_hex" in entry:
        lines.append(f"frame_hex     : {entry['frame_hex']}")
    if "error" in entry:
        lines.append(f"RESULT        : ERROR -- {entry['error']}")
    else:
        lines.append("output_json:")
        lines.append(json.dumps(entry["output_json"], indent=2))
    lines.append(_BOUNDARY)
    lines.append("")
    return "\n".join(lines)


def main() -> int:
    sys.stdout.reconfigure(encoding="utf-8")

    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--hex", help="single hex-encoded frame to decode")
    ap.add_argument("--hex-file", help="file with one hex-encoded frame per line")
    args = ap.parse_args()

    dll = find_dll()
    print(f"Loading {dll}")
    handle = load_parser(dll)

    vectors: list[tuple[str, bytes]]
    if args.hex:
        vectors = [("--hex input", bytes.fromhex(args.hex.strip()))]
    elif args.hex_file:
        lines = Path(args.hex_file).read_text().splitlines()
        vectors = [
            (f"{args.hex_file}:{i+1}", bytes.fromhex(line.strip()))
            for i, line in enumerate(lines) if line.strip()
        ]
    else:
        vectors = BUILTIN_VECTORS

    entries = [decode_one(handle, label, wire) for label, wire in vectors]

    report = "\n".join(format_entry(e) for e in entries)
    print(report)
    with _REPORT_FILE.open("a", encoding="utf-8") as f:
        f.write(report)
        f.write("\n")
    print(f"\nAppended to {_REPORT_FILE}")

    failures = [e for e in entries if "error" in e]
    if failures:
        print(f"\n{len(failures)}/{len(entries)} vectors failed to decode.")
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
