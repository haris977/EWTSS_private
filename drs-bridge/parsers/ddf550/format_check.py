"""Ad-hoc DDF-550 format_response()-verification tool.

Purpose: run JSON kwargs through the actual compiled ddf550.dll's
format_response() via ctypes -- encode the JSON into the DDF-550 wire
frame -- and print/persist both the input JSON and the resulting XML side
by side, so a human can eyeball whether the encoded XML matches what the
ICD/Remote_commands.html says it should look like. Also round-trips the
produced wire back through extract_frame + parse_message so you can
confirm encode -> decode symmetry on the spot.

This is the "manual encode verification" companion to decode_check.py
(which only exercises the decode direction: extract_frame -> parse_message)
and to tests/test_frames_ddf550.cpp / test_ddf550_decode_integration.py
(automated, synthetic-only regression coverage of both directions).

NOTE on why this bypasses drs_bridge.parser_loader.ParserHandle.format_response():
that convenience method is hardcoded to a different variant family's kwargs
shape (group_id/unit_id/status) and does not match DDF-550's format_response
contract (msg_kind/msg_type/id/command, or msg_kind=dfjob|dfdata|
df_station_data). This script calls the DLL's raw format_response binding
directly instead (same one ParserHandle sets up internally, just invoked
with the correct kwargs shape for this variant).

Usage:
    python format_check.py                     # run the built-in vectors
    python format_check.py --json <json>        # encode one JSON object
    python format_check.py --json-file <path>   # one JSON object per line

Output: printed to stdout AND appended to format_report.txt in this
directory (input JSON + envelope breakdown + pretty-printed XML + the
round-trip decode), so there's always a persistent record to compare
against the ICD by hand.
"""
from __future__ import annotations

import argparse
import ctypes
import json
import sys
from pathlib import Path
from xml.dom import minidom

_REPO_ROOT = Path(__file__).resolve().parents[3]  # .../ewtss-v2-design-main
sys.path.insert(0, str(_REPO_ROOT / "drs-bridge" / "src"))

from drs_bridge.parser_loader import load_parser  # noqa: E402

_BUILD_DIR = Path(__file__).resolve().parent / "build"
_REPORT_FILE = Path(__file__).resolve().parent / "format_report.txt"

_MAGIC_ENVELOPE_KINDS = {"request", "reply"}
_RAW_XML_KINDS = {"dfjob", "dfdata", "df_station_data"}


def find_dll() -> Path:
    for name in ("ddf550.dll", "libddf550.dll"):
        candidates = list(_BUILD_DIR.glob(f"**/{name}"))
        if candidates:
            return candidates[0]
    raise FileNotFoundError(
        f"No built ddf550 DLL found under {_BUILD_DIR} -- run "
        f"'cmake -S . -B build && cmake --build build --target ddf550' first."
    )


def call_format_response(handle, kwargs: dict) -> bytes | None:
    """Call the DLL's format_response() directly with DDF-550's real kwargs
    shape (msg_kind/msg_type/id/command, or msg_kind=dfjob/dfdata/
    df_station_data) -- see module docstring for why ParserHandle's own
    format_response() wrapper can't be used here."""
    kwargs_json = json.dumps(kwargs).encode("utf-8")
    out_buf = ctypes.POINTER(ctypes.c_uint8)()
    out_len = ctypes.c_size_t(0)
    rc = handle._format_response(  # noqa: SLF001 -- raw ABI binding, see docstring
        kwargs.get("msg_kind", "").encode("utf-8"),
        kwargs_json,
        ctypes.byref(out_buf),
        ctypes.byref(out_len),
    )
    if rc != 0:
        return None
    try:
        return bytes(out_buf[: out_len.value])
    finally:
        handle._free_result(ctypes.cast(out_buf, ctypes.c_void_p))  # noqa: SLF001


# Built-in vectors: one per shape already unit-tested in
# test_frames_ddf550.cpp, expressed here as the exact JSON kwargs
# format_response() expects -- add real ICD-derived examples below as needed.
BUILTIN_VECTORS: list[tuple[str, dict]] = [
    ("DFData -- FORMAT02/03 Hopper, real Unit glyphs (° / dBµV) + whitespace-preserved frequency_list", {
        "msg_kind": "dfdata",
        "df_data": {
            "ddf-cl-id": "17",
            "emitter_class": "Hopper",
            "start_frequency": {"unit": "Hz", "#text": "55237500"},
            "center_frequency": {"unit": "Hz", "#text": "57405418"},
            "stop_frequency": {"unit": "Hz", "#text": "59912500"},
            "bearing_avg": {"unit": "°", "#text": "85.2"},
            "bearing_std_dev": {"unit": "°", "#text": "2.9"},
            "level_avg": {"unit": "dBµV", "#text": "18.0"},
            "level_std_dev": {"unit": "dBµV", "#text": "1.4"},
            "elevation_avg": {"unit": "°", "#text": "7.6"},
            "elevation_std_dev": {"unit": "°", "#text": "0.8"},
            "first_detection_time": "2013-01-15 13:00:14.556000",
            "last_detection_time": "2013-01-15 13:00:23.919000",
            "quality": "95",
            "frequency_count": "42",
            "frequency_list": "\n  55350000,55625000,55700000,55800000,55950000,55975000,56050000,56125000, ...\n  ",
            "burst_duration": {"unit": "ms", "#text": "44"},
            "burst_bandwidth": {"unit": "Hz", "#text": "120"},
        },
    }),
]


def pretty_xml(xml_bytes: bytes) -> str:
    try:
        return minidom.parseString(xml_bytes).toprettyxml(indent="  ").strip()
    except Exception as exc:  # malformed/partial XML -- show raw text instead
        return f"(could not pretty-print: {exc})\n{xml_bytes.decode('utf-8', errors='backslashreplace')}"


def encode_one(handle, label: str, kwargs: dict) -> dict:
    result: dict = {"label": label, "input_json": kwargs}
    wire = call_format_response(handle, kwargs)
    if wire is None:
        result["error"] = "format_response: encoding failed (rc != 0)"
        return result
    result["output_hex"] = wire.hex()

    msg_kind = kwargs.get("msg_kind", "")
    if msg_kind in _MAGIC_ENVELOPE_KINDS:
        if len(wire) < 12:
            result["error"] = f"wire too short for envelope ({len(wire)} bytes)"
            return result
        magic_start = wire[0:4].hex()
        length_field = int.from_bytes(wire[4:8], "big")
        xml_bytes = wire[8:8 + length_field]
        magic_end = wire[8 + length_field:12 + length_field].hex()
        result["envelope"] = {
            "magic_start": magic_start,
            "length_field": length_field,
            "magic_end": magic_end,
            "xml_byte_count_actual": len(wire) - 12,
        }
        result["output_xml"] = pretty_xml(xml_bytes)
    elif msg_kind in _RAW_XML_KINDS:
        result["output_xml"] = pretty_xml(wire)
    else:
        result["output_xml"] = pretty_xml(wire)

    # Round-trip: feed the wire we just built back through the decode side
    # and confirm it comes back out as a sane message (extract_frame's
    # heuristic works uniformly across wrapped and raw-XML frames).
    frame = handle.extract_frame(wire)
    if frame is None:
        result["roundtrip_error"] = "extract_frame: did not recognise the frame we just built"
    else:
        parsed = handle.parse_message(frame)
        if parsed is None:
            result["roundtrip_error"] = "parse_message: failed to decode the frame we just built"
        else:
            result["roundtrip_json"] = parsed

    return result


def format_entry(entry: dict) -> str:
    lines = [f"=== {entry['label']} ===", "input_json:", json.dumps(entry["input_json"], indent=2)]
    if "error" in entry:
        lines.append(f"RESULT: ERROR -- {entry['error']}")
        lines.append("")
        return "\n".join(lines)

    lines.append(f"output_hex: {entry['output_hex']}")
    if "envelope" in entry:
        env = entry["envelope"]
        lines.append(
            f"envelope  : magic_start={env['magic_start']}  length_field={env['length_field']}"
            f"  magic_end={env['magic_end']}  (actual XML bytes: {env['xml_byte_count_actual']})"
        )
    lines.append("output_xml:")
    lines.append(entry["output_xml"])

    if "roundtrip_error" in entry:
        lines.append(f"ROUNDTRIP: ERROR -- {entry['roundtrip_error']}")
    elif "roundtrip_json" in entry:
        lines.append("roundtrip_json (decoded back via extract_frame + parse_message):")
        lines.append(json.dumps(entry["roundtrip_json"], indent=2))
    lines.append("")
    return "\n".join(lines)


def main() -> int:
    # Windows consoles default to a codepage (e.g. cp1252) that can't
    # represent every JSON/XML character; force utf-8 so this never crashes.
    sys.stdout.reconfigure(encoding="utf-8")

    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--json", help="single JSON kwargs object to encode")
    ap.add_argument("--json-file", help="file with one JSON kwargs object per line")
    args = ap.parse_args()

    dll = find_dll()
    print(f"Loading {dll}")
    handle = load_parser(dll)

    vectors: list[tuple[str, dict]]
    if args.json:
        vectors = [("--json input", json.loads(args.json))]
    elif args.json_file:
        lines = Path(args.json_file).read_text().splitlines()
        vectors = [
            (f"{args.json_file}:{i + 1}", json.loads(line))
            for i, line in enumerate(lines) if line.strip()
        ]
    else:
        vectors = BUILTIN_VECTORS

    entries = [encode_one(handle, label, kwargs) for label, kwargs in vectors]

    report = "\n".join(format_entry(e) for e in entries)
    print(report)
    with _REPORT_FILE.open("a", encoding="utf-8") as f:
        f.write(report)
        f.write("\n")
    print(f"\nAppended to {_REPORT_FILE}")

    failures = [e for e in entries if "error" in e or "roundtrip_error" in e]
    if failures:
        print(f"\n{len(failures)}/{len(entries)} vectors failed to encode or round-trip cleanly.")
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
