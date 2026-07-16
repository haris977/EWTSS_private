"""End-to-end test: build the real ddf550_parser.dll via CMake, load it
through parser_loader.load_parser, and drive extract_frame + parse_message
over the ctypes binding.

Scope, per request: request/decode side only (extract_frame -> parse_message,
XML-in -> JSON-out). format_response is intentionally NOT exercised here —
parser_loader.ParserHandle.format_response() is hardcoded to a different
variant family's kwargs shape (group_id/unit_id/status), and does not match
ddf550's format_response contract (msg_type/id/command_name/xml_body). That
mismatch is a separate, pre-existing issue outside this file's scope.

This is the first test that runs the *actual compiled* ddf550.dll through
Python, rather than the native test_ddf550.exe (CTest) build. If the C++
parser's real output ever drifts from what test_frames_ddf550.cpp expects,
or from what this file expects, this test is a second, independent witness.
"""
from __future__ import annotations

import struct

import pytest

from drs_bridge.parser_loader import load_parser


def wrap_xml(xml: str) -> bytes:
    """[MagicStart(4B BE)][Length(4B BE)][XML bytes][MagicEnd(4B BE)] —
    same envelope shape as the C++ suite's wrap_xml() helper. Magic values
    are placeholders (extract_frame's heuristic doesn't check them)."""
    body = xml.encode("utf-8")
    return struct.pack(">I", 0xABCD1234) + struct.pack(">I", len(body)) + body + struct.pack(">I", 0xDCBA4321)


@pytest.fixture()
def handle(built_ddf550_parser):
    return load_parser(built_ddf550_parser)


def _decode(handle, wire: bytes) -> dict:
    frame = handle.extract_frame(wire)
    assert frame is not None, "extract_frame returned no complete frame"
    parsed = handle.parse_message(frame)
    assert parsed is not None, "parse_message failed to decode the frame"
    return parsed


def test_dfmode_set_request(handle):
    xml = (
        '<Request type="set" id="42">'
        '<Command name="DfMode">'
        '<Param name="eOperationMode">DFMODE_FFM</Param>'
        '</Command></Request>'
    )
    out = _decode(handle, wrap_xml(xml))
    assert out["hw"] == "ddf550"
    assert out["channel"] == "control"
    assert out["msg_kind"] == "request"
    assert out["command_name"] == "DfMode"
    assert out["params"]["eOperationMode"] == "DFMODE_FFM"


def test_dfmode_get_reply(handle):
    xml = (
        '<Reply type="get" id="50">'
        '<Command name="DfMode">'
        '<Param name="eOperationMode">DFMODE_SCAN</Param>'
        '</Command></Reply>'
    )
    out = _decode(handle, wrap_xml(xml))
    assert out["msg_kind"] == "reply"
    assert out["command_name"] == "DfMode"
    assert out["params"]["eOperationMode"] == "DFMODE_SCAN"


def test_trace_enable(handle):
    xml = (
        '<Request type="set" id="7">'
        '<Command name="TraceEnable">'
        '<Param name="eTraceTag">TRACETAG_AUDIO</Param>'
        '<Param name="zIP">192.168.1.100</Param>'
        '<Param name="iPort">9152</Param>'
        '</Command></Request>'
    )
    out = _decode(handle, wrap_xml(xml))
    assert out["command_name"] == "TraceEnable"
    assert out["params"]["eTraceTag"] == "TRACETAG_AUDIO"
    assert out["params"]["zIP"] == "192.168.1.100"
    assert out["params"]["iPort"] == 9152


def test_trace_disable(handle):
    xml = (
        '<Request type="set" id="8">'
        '<Command name="TraceDisable">'
        '<Param name="eTraceTag">TRACETAG_AUDIO</Param>'
        '</Command></Request>'
    )
    out = _decode(handle, wrap_xml(xml))
    assert out["command_name"] == "TraceDisable"
    assert out["params"]["eTraceTag"] == "TRACETAG_AUDIO"


def test_scanrange_add(handle):
    xml = (
        '<Request type="set" id="20">'
        '<Command name="ScanRangeAdd">'
        '<Param name="iStartFrequency">30000000</Param>'
        '<Param name="iStopFrequency">88000000</Param>'
        '<Param name="iStepFrequency">25000</Param>'
        '</Command></Request>'
    )
    out = _decode(handle, wrap_xml(xml))
    assert out["command_name"] == "ScanRangeAdd"
    assert out["params"]["iStartFrequency"] == 30000000
    assert out["params"]["iStopFrequency"] == 88000000
    assert out["params"]["iStepFrequency"] == 25000


def test_scanrange_delete_all(handle):
    xml = '<Request type="set" id="21"><Command name="ScanRangeDeleteAll"></Command></Request>'
    out = _decode(handle, wrap_xml(xml))
    assert out["command_name"] == "ScanRangeDeleteAll"
    assert out["params"] == {}


def test_dfselect_preclassifier_filter(handle):
    xml = (
        "<DFSelect>"
        "<EmitterClass>Hopper</EmitterClass>"
        "<EmitterClass>Burst</EmitterClass>"
        "</DFSelect>"
    )
    out = _decode(handle, xml.encode("utf-8"))  # raw XML, no envelope (port 9153)
    assert out["channel"] == "preclassifier"
    assert out["msg_kind"] == "request"


def test_analysis_interval_ms(handle):
    xml = (
        '<DDFCLRequest id="10" type="set">'
        '<Command name="AnalysisIntervalMs">50000</Command>'
        '</DDFCLRequest>'
    )
    out = _decode(handle, wrap_xml(xml))
    assert out["channel"] == "preclassifier"
    assert out["command_name"] == "AnalysisIntervalMs"
    assert out["command_value"] == "50000"


@pytest.mark.parametrize(
    "emitter_class,extra_xml,extra_asserts",
    [
        ("Burst", '<CenterFrequency Unit="Hz">433920000</CenterFrequency>',
         {"CenterFrequency": "433920000"}),
        ("Hopper",
         '<StartFrequency Unit="Hz">430000000</StartFrequency>'
         '<StopFrequency Unit="Hz">440000000</StopFrequency>',
         {"StartFrequency": "430000000", "StopFrequency": "440000000"}),
        ("Chirp",
         '<StartFrequency Unit="Hz">100000000</StartFrequency>'
         '<StopFrequency Unit="Hz">108000000</StopFrequency>',
         {"StartFrequency": "100000000", "StopFrequency": "108000000"}),
    ],
)
def test_dfdata_emitter_classes(handle, emitter_class, extra_xml, extra_asserts):
    xml = (
        f'<DFData DDF-CL-ID="3">'
        f'<EmitterClass>{emitter_class}</EmitterClass>'
        f'{extra_xml}'
        f'<BearingAvg Unit="deg">245.7</BearingAvg>'
        f'</DFData>'
    )
    out = _decode(handle, xml.encode("utf-8"))  # raw XML, port 9154, device push only
    assert out["channel"] == "preclassifier_output"
    assert out["msg_kind"] == "dfdata"
    assert out["fields"]["EmitterClass"] == emitter_class
    for key, val in extra_asserts.items():
        assert out["fields"][key] == val
