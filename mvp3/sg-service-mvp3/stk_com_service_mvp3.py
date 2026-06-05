"""Live STK orchestrator for MVP3.

Flow per compute:
  scenario_builder.build_stk_scenario  -> create entities in STK
  czml_builder_mvp3.build_czml         -> generate CZML from DataProviders
  fom_pipeline.read_fom_grid           -> extract FOM grid (if any)
  fom_pipeline.render_heatmap_png      -> render PNG
  fom_pipeline.inject_rectangle_overlay-> add CZML rectangle
  write JSON to disk

Uses agi.stk12 COM bindings, same as MVP1. If Engine license is missing,
falls back to an attached STK Desktop instance.
"""
from __future__ import annotations
import json
import logging
import math
from pathlib import Path

from stk_service import IStkService

log = logging.getLogger(__name__)
_service_instance: IStkService | None = None


def get_stk_service() -> IStkService:
    global _service_instance
    if _service_instance is not None:
        return _service_instance

    try:
        import agi.stk12  # noqa: F401  (availability probe)
    except ImportError as exc:
        log.warning("agi.stk12 not installed (%s) - using MockStkService", exc)
        from stk_mock_service import MockStkService
        _service_instance = MockStkService()
        return _service_instance

    try:
        _service_instance = StkComServiceMvp3()
        log.info("using StkComServiceMvp3 (live agi.stk12)")
    except Exception as exc:
        log.warning("STK Engine failed to start (%s) - using MockStkService", exc)
        from stk_mock_service import MockStkService
        _service_instance = MockStkService()
    return _service_instance


class StkComServiceMvp3(IStkService):
    def __init__(self) -> None:
        try:
            from agi.stk12.stkengine import STKEngine  # type: ignore[import]
            self._stk  = STKEngine.StartApplication(noGraphics=False)
            self._root = self._stk.NewObjectRoot()
            log.info("StkComServiceMvp3: started STK Engine")
        except Exception as eng_exc:
            log.warning("StkComServiceMvp3: Engine unavailable (%s), trying Desktop", eng_exc)
            from agi.stk12.stkdesktop import STKDesktop  # type: ignore[import]
            try:
                self._stk = STKDesktop.AttachToApplication()
            except Exception:
                self._stk = STKDesktop.StartApplication(visible=True)
            if self._stk is None:
                raise RuntimeError("Could not attach to or start STK Desktop")
            self._root = getattr(self._stk, "Root", None)
            if self._root is None and hasattr(self._stk, "NewObjectRoot"):
                self._root = self._stk.NewObjectRoot()
            if self._root is None:
                raise RuntimeError("STKDesktop instance exposed no Root")

        self._start = self._stop = ""

    def build_and_compute(self, exercise_id: str, start_time: str, stop_time: str) -> None:
        from scenario_builder import build_stk_scenario
        build_stk_scenario(self._root, exercise_id, start_time, stop_time)
        self._start, self._stop = start_time, stop_time
        log.info("StkComServiceMvp3: scenario built for exercise %s", exercise_id)

    def export_czml(self, exercise_id: str, output_path: str) -> None:
        from czml_builder_mvp3 import build_czml
        from fom_pipeline import read_fom_grid, render_heatmap_png, inject_rectangle_overlay

        sc = self._root.CurrentScenario
        czml_json = build_czml(
            self._root, sc.InstanceName, self._start, self._stop,
            model_base_url="http://localhost:8003/models/",
        )
        doc = json.loads(czml_json)

        # FOM overlay (optional). Find first CoverageDefinition/FigureOfMerit pair.
        coverage, fom = None, None
        for i in range(sc.Children.Count):
            child = sc.Children.Item(i)
            if getattr(child, "ClassName", "").lower() != "coveragedefinition":
                continue
            coverage = child
            for j in range(child.Children.Count):
                sub = child.Children.Item(j)
                if getattr(sub, "ClassName", "").lower() == "figureofmerit":
                    fom = sub
                    break
            if fom:
                break

        if coverage and fom:
            grid = read_fom_grid(coverage, fom, sc)
            if grid:
                polygon_deg = _extract_area_target_polygon(doc)
                img_dir  = Path(__file__).parent / "fom_images"
                img_dir.mkdir(exist_ok=True)
                img_path = img_dir / f"{exercise_id}.png"
                render_heatmap_png(grid, polygon_deg, img_path)
                bounds_rad = _polygon_to_bounds_rad(polygon_deg) if polygon_deg else None
                doc = inject_rectangle_overlay(
                    doc,
                    f"http://localhost:8003/exercises/{exercise_id}/fom-image",
                    bounds_rad,
                )
                log.info("StkComServiceMvp3: FOM heatmap -> %s", img_path)

        out = Path(output_path)
        out.parent.mkdir(parents=True, exist_ok=True)
        out.write_text(json.dumps(doc, indent=2), encoding="utf-8")
        log.info("StkComServiceMvp3: CZML exported -> %s (%.1f KB)",
                 out, out.stat().st_size / 1024)

    def shutdown(self) -> None:
        for method_name in ("Terminate", "ShutDown", "Quit"):
            method = getattr(self._stk, method_name, None)
            if callable(method):
                try:
                    method()
                    log.info("StkComServiceMvp3: STK %s()", method_name)
                    return
                except Exception as exc:
                    log.debug("StkComServiceMvp3: %s() raised: %s", method_name, exc)


def _extract_area_target_polygon(document):
    """Return [(lon_deg, lat_deg), ...] for the first AreaTarget polygon, or None."""
    import math as _m
    for p in document:
        rad = p.get("polygon", {}).get("positions", {}).get("cartographicRadians")
        if rad and len(rad) >= 6:
            lons = rad[0::3]
            lats = rad[1::3]
            return [(_m.degrees(lo), _m.degrees(la)) for lo, la in zip(lons, lats)]
    return None


def _polygon_to_bounds_rad(polygon_deg):
    lons = [p[0] for p in polygon_deg]
    lats = [p[1] for p in polygon_deg]
    return (math.radians(min(lons)), math.radians(min(lats)),
            math.radians(max(lons)), math.radians(max(lats)))
