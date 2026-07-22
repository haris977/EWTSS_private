"""Ad-hoc DDF-550 decode-verification tool.

Purpose: run real (or synthetic) input frames through the actual compiled
ddf550.dll via ctypes -- the same extract_frame -> parse_message path
drs-bridge uses in production -- and write the input bytes next to the
decoded JSON so a human can eyeball whether the output matches what the
real device's ICD says it should be.

This is the "physical verification" companion to test_frames_ddf550.cpp
(native CTest, synthetic-only) and tests/test_ddf550_decode_integration.py
(pytest, also synthetic-only). Neither of those can tell you whether a
REAL DDF-550 unit's wire bytes decode the way you expect -- this script is
where you paste in an actual capture (e.g. from Wireshark on port 9150/
9153/9154) and check.

Usage:
    python decode_check.py                       # run the built-in vectors
    python decode_check.py --hex <hexstring>      # decode one real capture
    python decode_check.py --hex-file <path>      # one hex string per line

Output: printed to stdout AND appended to decode_report.txt in this
directory (input bytes + decoded JSON side by side), so there's always a
persistent record to compare against the ICD by hand.

Only exercises extract_frame + parse_message (the request/decode side).
format_response is intentionally out of scope here -- see
tests/test_ddf550_decode_integration.py's module docstring for why.
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
    for name in ("ddf550.dll", "libddf550.dll"):
        candidates = list(_BUILD_DIR.glob(f"**/{name}"))
        if candidates:
            return candidates[0]
    raise FileNotFoundError(
        f"No built ddf550 DLL found under {_BUILD_DIR} -- run "
        f"'cmake -S . -B build && cmake --build build --target ddf550' first."
    )


def wrap_xml(xml: str) -> bytes:
    """Same envelope the real DDF-550 control channel (9150) uses:
    [MagicStart(4B BE)][Length(4B BE)][XML][MagicEnd(4B BE)]."""
    body = xml.encode("utf-8")
    return struct.pack(">I", 0xABCD1234) + struct.pack(">I", len(body)) + body + struct.pack(">I", 0xDCBA4321)


# Built-in vectors: one per command already unit-tested in
# test_frames_ddf550.cpp, expressed here as either wrapped-XML (port 9150)
# or raw XML (ports 9153/9154) bytes -- add real captured hex below as you
# get access to hardware.
BUILTIN_VECTORS: list[tuple[str, bytes]] = [
    ("DFData -- FORMAT02/03 Hopper, real Unit glyphs (deg/dBuV as ° / µ), multi-line FrequencyList", (
        '<DFData DDF-CL-ID="17">'
        '<EmitterClass>Hopper</EmitterClass>'
        '<StartFrequency Unit="Hz">55237500</StartFrequency>'
        '<CenterFrequency Unit="Hz">57405418</CenterFrequency>'
        '<StopFrequency Unit="Hz">59912500</StopFrequency>'
        '<BearingAvg Unit="°">85.2</BearingAvg>'
        '<BearingStdDev Unit="°">2.9</BearingStdDev>'
        '<LevelAvg Unit="dBµV">18.0</LevelAvg>'
        '<LevelStdDev Unit="dBµV">1.4</LevelStdDev>'
        '<ElevationAvg Unit="°">7.6</ElevationAvg>'
        '<ElevationStdDev Unit="°">0.8</ElevationStdDev>'
        '<FirstDetectionTime>2013-01-15 13:00:14.556000</FirstDetectionTime>'
        '<LastDetectionTime>2013-01-15 13:00:23.919000</LastDetectionTime>'
        '<Quality>95</Quality>'
        '<FrequencyCount>42</FrequencyCount>'
        '<FrequencyList>\n'
        '  55350000,55625000,55700000,55800000,55950000,55975000,56050000,56125000, ...\n'
        '  </FrequencyList>'
        '<BurstDuration Unit="ms">44</BurstDuration>'
        '<BurstBandwidth Unit="Hz">120</BurstBandwidth>'
        '</DFData>'
    ).encode("utf-8")),
]


def decode_one(handle, label: str, wire: bytes) -> dict:
    result: dict = {"label": label, "input_hex": wire.hex()}
    # Best-effort readable view of the raw wire bytes as sent/received --
    # non-text envelope bytes (magic/length, for wrapped-XML frames) show up
    # as \xNN escapes rather than replacement chars, so this always prints
    # safely regardless of the terminal's codepage.
    result["input_readable"] = wire.decode("ascii", errors="backslashreplace")
    frame = handle.extract_frame(wire)
    if frame is None:
        result["error"] = "extract_frame: no complete frame recognised"
        return result
    # The frame extract_frame hands back has the binary envelope already
    # stripped (for wrapped-XML/raw-XML channels) -- this is the clean XML
    # the parser actually decoded, not the raw envelope bytes.
    result["frame_xml"] = frame.decode("ascii", errors="backslashreplace")
    parsed = handle.parse_message(frame)
    if parsed is None:
        result["error"] = "parse_message: failed to decode frame"
        return result
    result["output_json"] = parsed
    return result


def format_entry(entry: dict) -> str:
    lines = [f"=== {entry['label']} ===", f"input_hex     : {entry['input_hex']}"]
    lines.append(f"input_readable: {entry['input_readable']}")
    if "frame_xml" in entry:
        lines.append(f"frame_xml     : {entry['frame_xml']}")
    if "error" in entry:
        lines.append(f"RESULT        : ERROR -- {entry['error']}")
    else:
        lines.append("output_json:")
        lines.append(json.dumps(entry["output_json"], indent=2))
    lines.append("")
    return "\n".join(lines)


def main() -> int:
    # Windows consoles default to a codepage (e.g. cp1252) that can't
    # represent every JSON/XML character; force utf-8 so this never crashes
    # on printable, but non-cp1252, text in a real device's params/fields.
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
