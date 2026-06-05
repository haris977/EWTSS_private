import json
import tempfile
from pathlib import Path

import pytest

from stk_mock_service import MockStkService
from stk_service import IStkService


def test_mock_is_istk_service():
    assert issubclass(MockStkService, IStkService)


def test_mock_build_and_compute_succeeds():
    svc = MockStkService()
    # Should not raise
    svc.build_and_compute(
        "ex-001",
        "1 Jan 2025 00:00:00.000",
        "1 Jan 2025 01:00:00.000",
    )


def test_mock_export_czml_writes_valid_file():
    svc = MockStkService()
    svc.build_and_compute(
        "ex-002",
        "1 Jan 2025 00:00:00.000",
        "1 Jan 2025 01:00:00.000",
    )
    with tempfile.NamedTemporaryFile(suffix=".czml", delete=False) as f:
        output_path = f.name

    svc.export_czml("ex-002", output_path)

    content = Path(output_path).read_text(encoding="utf-8")
    packets = json.loads(content)
    assert isinstance(packets, list)
    assert packets[0]["id"] == "document"


def test_mock_export_czml_contains_correct_exercise_id():
    svc = MockStkService()
    svc.build_and_compute(
        "ex-003",
        "1 Jan 2025 00:00:00.000",
        "1 Jan 2025 01:00:00.000",
    )
    with tempfile.NamedTemporaryFile(suffix=".czml", delete=False, mode="w") as f:
        output_path = f.name

    svc.export_czml("ex-003", output_path)
    packets = json.loads(Path(output_path).read_text(encoding="utf-8"))
    entity_ids = [p["id"] for p in packets if p["id"] != "document"]
    assert any("ex-003" in eid for eid in entity_ids)
