"""Ad-hoc CA120 format_response()-verification tool.

Purpose: run JSON kwargs through the actual compiled ca120.dll's
format_response() via ctypes -- encode the JSON into a CA120 XML <Request>
wire frame -- and print/persist both the input JSON and the resulting XML
side by side, so a human can eyeball whether the encoded XML matches the
ICD (R&S-CA120-ICD-V15) control-command shape. Also round-trips the
produced wire back through extract_frame + parse_message so you can
confirm encode -> decode symmetry on the spot.

This is the "manual encode verification" companion to decode_check.py
(which only exercises the decode direction: extract_frame -> parse_message)
and to tests/test_frames_ca120.cpp (automated, synthetic-only regression
coverage of both directions).

CA120's format_response() contract (see ca120_parser.cpp's own doc comment)
is narrower than DDF-550's: it only ever encodes an XML <Request> frame for
the control channel (TCP 9001). Required JSON fields:
    "msg_type" : "set" | "get" | "suppress"
    "id"       : integer request correlation ID
    "xml_body" : inner XML string (children of <Request>), JSON-escaped
Optional:
    "time"     : integer CA120 time value (us)
The DLL's first ("kind") argument is currently unused by CA120's
format_response (see the `const char* /*kind*/` parameter in
ca120_parser.cpp) -- this script still passes msg_type through it for
consistency with the ABI signature, but changing it has no effect.

There is no AMMOS encode direction to verify: AMMOS is CA120-to-bridge
only (a receive-only data stream), so format_response has nothing to
produce for it.

NOTE on why this bypasses drs_bridge.parser_loader.ParserHandle.format_response():
that convenience method is hardcoded to a different variant family's kwargs
shape (group_id/unit_id/status) and does not match CA120's format_response
contract (msg_type/id/xml_body[/time]). This script calls the DLL's raw
format_response binding directly instead (same one ParserHandle sets up
internally, just invoked with the correct kwargs shape for this variant).

Usage:
    python format_check.py                     # run the built-in vectors
    python format_check.py --json <json>        # encode one JSON object
    python format_check.py --json-file <path>   # one JSON object per line

Output: printed to stdout AND appended to format_report.txt in this
directory (input JSON + pretty-printed XML + the round-trip decode), so
there's always a persistent record to compare against the ICD by hand.
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


def find_dll() -> Path:
    for name in ("ca120.dll", "libca120.dll"):
        candidates = list(_BUILD_DIR.glob(f"**/{name}"))
        if candidates:
            return candidates[0]
    raise FileNotFoundError(
        f"No built ca120 DLL found under {_BUILD_DIR} -- run "
        f"'cmake -S . -B build && cmake --build build --target ca120' first."
    )


def call_format_response(handle, kwargs: dict) -> bytes | None:
    """Call the DLL's format_response() directly with CA120's real kwargs
    shape (msg_type/id/xml_body[/time]) -- see module docstring for why
    ParserHandle's own format_response() wrapper can't be used here."""
    kwargs_json = json.dumps(kwargs).encode("utf-8")
    out_buf = ctypes.POINTER(ctypes.c_uint8)()
    out_len = ctypes.c_size_t(0)
    rc = handle._format_response(  # noqa: SLF001 -- raw ABI binding, see docstring
        kwargs.get("msg_type", "").encode("utf-8"),
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
# test_frames_ca120.cpp, expressed here as the exact JSON kwargs
# format_response() expects -- add real ICD-derived examples below as needed.
BUILTIN_VECTORS: list[tuple[str, dict]] = [
    ("Full parse_message-shaped Reply JSON, as drs-server would actually hold it "
     "(hw/channel/msg_kind/body wrapper) -- NOT format_response's real kwargs shape "
     "(msg_type/id/xml_body). Kept in as-received to show what actually happens if "
     "this exact object is handed to format_response() unmodified.", {
        "hw": "ca120",
        "channel": "xml",
        "msg_kind": "reply",
        "body": {
            "reply": {
                "type": "get",
                "id": "70054",
                "digital_demodulator": {
                    "available_demodulators": {
                        "demodulator_info": {
                            "demodulator_name": "ASK2",
                            "demodulator_version": "1",
                            "module_id": "1048576",
                            "parameter_size": "9",
                            "supports_symbol_data": "1",
                            "supports_iq_constellation_data": "0",
                            "supports_instant_data": "1",
                            "supports_image_data": "0",
                            "supports_transmission_data": "0",
                            "supports_audio_data": "0",
                            "is_universal": "1",
                            "supports_special_data": "0",
                        }
                    }
                },
                "bitstream_processing": {
                    "available_decoders": {
                        "decoder": {
                            "id": "100000",
                            "classification_only": "1",
                            "decoder_name": "ASCII",
                        }
                    }
                },
            }
        },
    }),
    ("Same data, reshaped to format_response's actual contract "
     "(msg_type/id/xml_body per ca120_parser.cpp:744-749) -- msg_type/id pulled "
     "from body.reply.type/id, xml_body hand-built from body.reply.* (this "
     "reshaping step is exactly the not-yet-ported 'unmirror' work flagged "
     "earlier; nothing in the DLL does it automatically today).", {
        "msg_type": "get",
        "id": 70054,
        "xml_body": (
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
            '</AvailableDemodulators>'
            '</DigitalDemodulator>'
            '<BitstreamProcessing>'
            '<AvailableDecoders>'
            '<Decoder id="100000" classificationOnly="1">'
            '<DecoderName>ASCII</DecoderName>'
            '</Decoder>'
            '</AvailableDecoders>'
            '</BitstreamProcessing>'
        ),
    }),
]


def pretty_xml(xml_bytes: bytes) -> str:
    try:
        pretty = minidom.parseString(xml_bytes).toprettyxml(indent="  ").strip()
        # minidom.toprettyxml() always injects a "<?xml version=...?>" line --
        # the real wire bytes have no such declaration (see output_hex), so
        # drop it here to avoid implying it's part of what format_response emits.
        return pretty.split("\n", 1)[1] if pretty.startswith("<?xml") else pretty
    except Exception as exc:  # malformed/partial XML -- show raw text instead
        return f"(could not pretty-print: {exc})\n{xml_bytes.decode('utf-8', errors='backslashreplace')}"


def encode_one(handle, label: str, kwargs: dict) -> dict:
    result: dict = {"label": label, "input_json": kwargs}
    wire = call_format_response(handle, kwargs)
    if wire is None:
        result["error"] = "format_response: encoding failed (rc != 0)"
        return result
    result["output_hex"] = wire.hex()
    result["output_xml"] = pretty_xml(wire)

    # Round-trip: feed the wire we just built back through the decode side
    # (CA120's XML control channel is unwrapped -- no envelope to strip).
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
