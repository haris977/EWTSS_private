import json
import pytest
from czml_builder import build_synthetic_czml


def test_czml_document_packet():
    packets = build_synthetic_czml("ex-001", "1 Jan 2025 00:00:00.000", "1 Jan 2025 01:00:00.000")
    assert isinstance(packets, list)
    doc = packets[0]
    assert doc["id"] == "document"
    assert doc["version"] == "1.0"
    assert "clock" in doc


def test_czml_has_entity_packet():
    packets = build_synthetic_czml("ex-001", "1 Jan 2025 00:00:00.000", "1 Jan 2025 01:00:00.000")
    entity_ids = [p["id"] for p in packets if p["id"] != "document"]
    assert len(entity_ids) >= 1


def test_czml_entity_has_position():
    packets = build_synthetic_czml("ex-001", "1 Jan 2025 00:00:00.000", "1 Jan 2025 01:00:00.000")
    entity = next(p for p in packets if p["id"] != "document")
    assert "position" in entity
    pos = entity["position"]
    assert "cartographicDegrees" in pos
    # Must be [time, lon, lat, alt, time, lon, lat, alt, ...]
    coords = pos["cartographicDegrees"]
    assert len(coords) % 4 == 0
    assert len(coords) >= 8   # at least two sample points


def test_czml_is_json_serialisable():
    packets = build_synthetic_czml("ex-001", "1 Jan 2025 00:00:00.000", "1 Jan 2025 01:00:00.000")
    text = json.dumps(packets)
    reloaded = json.loads(text)
    assert reloaded[0]["id"] == "document"
