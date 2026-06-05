from __future__ import annotations
from abc import ABC, abstractmethod


class IStkService(ABC):
    """Abstract STK integration. MVP3 live + mock implementations both implement this."""

    @abstractmethod
    def build_and_compute(self, exercise_id: str, start_time: str, stop_time: str) -> None:
        """Build the Kashmir two-aircraft-plus-sensors scenario in STK."""

    @abstractmethod
    def export_czml(self, exercise_id: str, output_path: str) -> None:
        """Generate CZML from the current scenario and write to output_path."""

    def shutdown(self) -> None:
        """Optional: terminate STK cleanly. Default no-op for mocks."""
