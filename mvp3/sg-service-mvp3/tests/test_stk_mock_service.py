import json
from pathlib import Path

from stk_mock_service import MockStkService


def test_mock_service_writes_non_empty_czml(tmp_path):
    svc = MockStkService()
    svc.build_and_compute("test-eid",
                          "1 Jan 2025 00:00:00.000",
                          "1 Jan 2025 01:00:00.000")
    out = tmp_path / "test-eid.czml"
    svc.export_czml("test-eid", str(out))
    assert out.exists()
    assert out.stat().st_size > 0


def test_mock_czml_contains_document_and_aircraft(tmp_path):
    svc = MockStkService()
    svc.build_and_compute("eid1",
                          "1 Jan 2025 00:00:00.000",
                          "1 Jan 2025 01:00:00.000")
    out = tmp_path / "eid1.czml"
    svc.export_czml("eid1", str(out))
    data = json.loads(out.read_text(encoding="utf-8"))
    ids = {p.get("id") for p in data}
    assert "document"  in ids
    assert "EmitterAC" in ids
    assert "RadarAC"   in ids
