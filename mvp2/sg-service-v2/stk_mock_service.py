from __future__ import annotations
import json
import logging
from pathlib import Path

from czml_builder_v2 import build_synthetic_czml
from stk_service import IStkService

log = logging.getLogger(__name__)

_DEFAULT_START = "1 Jan 2025 00:00:00.000"
_DEFAULT_STOP  = "1 Jan 2025 01:00:00.000"


class MockStkService(IStkService):

    def __init__(self) -> None:
        self._exercises: dict[str, dict] = {}

    def build_and_compute(self, exercise_id, start_time, stop_time, entities):
        self._exercises[exercise_id] = {
            "start_time": start_time,
            "stop_time":  stop_time,
            "entities":   entities,
        }

    def export_czml(self, exercise_id, output_path):
        ex = self._exercises.get(exercise_id) or {
            "start_time": _DEFAULT_START,
            "stop_time":  _DEFAULT_STOP,
            "entities":   [],
        }
        packets = build_synthetic_czml(
            exercise_id, ex["start_time"], ex["stop_time"], ex["entities"]
        )
        Path(output_path).write_text(json.dumps(packets, indent=2), encoding="utf-8")
