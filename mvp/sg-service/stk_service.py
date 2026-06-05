from __future__ import annotations
from abc import ABC, abstractmethod


class IStkService(ABC):
    """Contract that both StkComService and MockStkService fulfil."""

    @abstractmethod
    def build_and_compute(
        self,
        exercise_id: str,
        start_time: str,
        stop_time: str,
    ) -> None:
        """Build scenario in STK from parameters and run all computations."""

    @abstractmethod
    def load_scenario(self, file_path: str) -> None:
        """Load an existing STK scenario file (.sc / .vdf) into the STK instance.

        Replaces build_and_compute for file-based exercises.
        Access intervals are recomputed after loading.
        """

    @abstractmethod
    def export_czml(self, exercise_id: str, output_path: str) -> None:
        """Export the computed scenario to a CZML file at output_path."""

    def shutdown(self) -> None:
        """Terminate the STK instance.  No-op for implementations that don't need it."""
