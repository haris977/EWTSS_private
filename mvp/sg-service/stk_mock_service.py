from __future__ import annotations

import logging
import json
from pathlib import Path

from czml_builder import build_synthetic_czml
from stk_service import IStkService

log = logging.getLogger(__name__)

_DEFAULT_START = "1 Jan 2025 00:00:00.000"
_DEFAULT_STOP  = "1 Jan 2025 01:00:00.000"


class MockStkService(IStkService):
    """
    STK-free fallback service.
    Stores exercise parameters in memory; export_czml() writes synthetic CZML.
    Used when agi.stk12 is not installed, or during CI/test runs.
    """

    def __init__(self) -> None:
        self._exercises: dict[str, dict[str, str]] = {}

    def build_and_compute(
        self,
        exercise_id: str,
        start_time: str,
        stop_time: str,
    ) -> None:
        """Record exercise parameters — computation is deferred to export_czml."""
        self._exercises[exercise_id] = {
            "start_time": start_time,
            "stop_time": stop_time,
        }

    def load_scenario(self, file_path: str) -> None:
        """STK not available — log warning and fall back to synthetic CZML."""
        log.warning(
            "MockStkService: cannot load '%s' — agi.stk12 not installed. "
            "Synthetic CZML will be generated instead.",
            file_path,
        )

    def export_czml(self, exercise_id: str, output_path: str) -> None:
        """Write synthetic CZML to output_path using czml_builder."""
        ex = self._exercises.get(exercise_id)
        if ex is None:
            # File-based exercise: no times recorded (load_scenario is a no-op).
            # Use defaults and generate synthetic CZML as a visual placeholder.
            log.warning(
                "MockStkService: no parameters for exercise %s "
                "(file-based exercise without STK) — using default scenario times",
                exercise_id,
            )
            ex = {"start_time": _DEFAULT_START, "stop_time": _DEFAULT_STOP}

        packets = build_synthetic_czml(
            exercise_id,
            ex["start_time"],
            ex["stop_time"],
        )
        Path(output_path).write_text(
            json.dumps(packets, indent=2),
            encoding="utf-8",
        )
