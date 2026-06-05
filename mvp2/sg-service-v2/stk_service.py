from __future__ import annotations
from abc import ABC, abstractmethod


class IStkService(ABC):
    """
    Abstract STK service interface for MVP2.
    load_scenario() is intentionally excluded — MVP2 accepts only PlanningResult
    from the frontend draw tool, not uploaded .sc/.vdf files.
    """

    @abstractmethod
    def build_and_compute(
        self,
        exercise_id: str,
        start_time: str,
        stop_time: str,
        entities: list[dict],
    ) -> None:
        """
        Build the STK scenario and compute accesses.

        entities: list of GeoJSON Feature dicts, each with
          properties.entityType in ('AreaTarget','Aircraft','Facility','Sensor')
          and geometry matching that type (Polygon / LineString / Point).
        """
        ...

    @abstractmethod
    def export_czml(self, exercise_id: str, output_path: str) -> None: ...

    def shutdown(self) -> None:
        pass
