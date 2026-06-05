"""
MVP2 STK integration via agi.stk13 COM (the direct successor of agi.stk12).
Targets STK Engine 13.x / STK_ODTK 13.

Why agi.stk13 and not ansys.stk.core (PySTK)?  PySTK is still a 0.x dev
wheel with an API that may shift before 1.0. agi.stk13 is a stable 13.1.0
wheel shipped inside the STK install, and its PascalCase COM shape matches
MVP1 exactly, so the porting cost from MVP1 is minimal.
"""
from __future__ import annotations
import logging
from pathlib import Path
from stk_service import IStkService

log = logging.getLogger(__name__)
_service_instance: IStkService | None = None


def _find_czml_exporter_plugin() -> Path | None:
    """Locate the CZML Exporter Plugin install via the Windows registry.

    AGI ships the plugin as its own MSI. The MSI writes an
    InstallHome/InstallPath value under HKLM\\SOFTWARE\\AGI\\...; STK
    auto-loads the plugin on Engine startup. A missing key means the MSI
    has not been installed and ExportCZML will fail.

    The v13 key names are speculative until AGI confirms; adjust below if
    the actual registration differs.
    """
    try:
        import winreg
    except ImportError:
        return None

    candidates = (
        # --- speculative v13 keys ---
        (r"SOFTWARE\AGI\STK CZML Exporter Plugin 13",                  "InstallHome"),
        (r"SOFTWARE\AGI\STK_ODTK CZML Exporter Plugin 13",             "InstallHome"),
        (r"SOFTWARE\AGI\STK CZML Exporter Plugin 13",                  "InstallPath"),
        (r"SOFTWARE\Wow6432Node\AGI\STK CZML Exporter Plugin 13",      "InstallHome"),
        (r"SOFTWARE\Wow6432Node\AGI\STK_ODTK CZML Exporter Plugin 13", "InstallHome"),
        # --- v12 fallback ---
        (r"SOFTWARE\AGI\STK CZML Exporter Plugin 12",                  "InstallHome"),
        (r"SOFTWARE\AGI\STK CZML Exporter Plugin 12",                  "InstallPath"),
        (r"SOFTWARE\Wow6432Node\AGI\STK CZML Exporter Plugin 12",      "InstallHome"),
    )
    for subkey, value_name in candidates:
        try:
            with winreg.OpenKey(winreg.HKEY_LOCAL_MACHINE, subkey) as key:
                install_path, _ = winreg.QueryValueEx(key, value_name)
        except OSError:
            continue
        p = Path(install_path)
        if p.exists():
            return p
    return None


def get_stk_service() -> IStkService:
    global _service_instance
    if _service_instance is not None:
        return _service_instance

    # Step 1 — is agi.stk13 installed at all?
    try:
        import agi.stk13  # noqa: F401  (availability probe)
    except ImportError as exc:
        log.warning("agi.stk13 package not installed (%s) — using MockStkService", exc)
        from stk_mock_service import MockStkService
        _service_instance = MockStkService()
        return _service_instance

    # Step 2 — package is present; try to start STK Engine.
    try:
        _service_instance = StkComServiceV13()
        log.info("using StkComServiceV13 (live agi.stk13)")
    except Exception as exc:
        log.warning("STK Engine failed to start (%s) — using MockStkService", exc)
        from stk_mock_service import MockStkService
        _service_instance = MockStkService()
    return _service_instance


class StkComServiceV13(IStkService):
    """Live STK 13 integration via agi.stk13 COM automation."""

    def __init__(self) -> None:
        # noGraphics=False enables the graphics stack — required by the
        # CZML Exporter Plugin. STKEngine still runs without a visible window.
        try:
            from agi.stk13.stkengine import STKEngine  # type: ignore[import]
            self._stk  = STKEngine.StartApplication(noGraphics=False)
            self._root = self._stk.NewObjectRoot()
            log.info("StkComServiceV13: started STK Engine (graphics enabled)")
        except Exception as eng_exc:
            log.warning("StkComServiceV13: STKEngine unavailable (%s), falling back to STKDesktop",
                        eng_exc)
            self._stk, self._root = self._start_stk_desktop()

        self._czml_plugin = _find_czml_exporter_plugin()
        if self._czml_plugin is not None:
            log.info("CZML Exporter Plugin registered at %s", self._czml_plugin)
        else:
            log.warning(
                "CZML Exporter Plugin not found in registry "
                r"(HKLM\SOFTWARE\AGI\STK CZML Exporter Plugin 13 / 12). "
                "Install the plugin MSI from AGI before exporting CZML."
            )

    def _start_stk_desktop(self):
        """Attach to a running STK Desktop, or start a new one. Returns (stk, root).

        ``STKDesktop.AttachToApplication`` in agi.stk13 can return ``None`` rather
        than raising when no Desktop process is available to attach to (observed
        with agi_stk13 13.1.0). Treat that as a miss and fall through to
        ``StartApplication``. Validate that the resulting object exposes a
        non-None ``Root`` before handing control back to the caller.
        """
        from agi.stk13.stkdesktop import STKDesktop  # type: ignore[import]

        stk = None
        try:
            stk = STKDesktop.AttachToApplication()
        except Exception as exc:
            log.info("StkComServiceV13: AttachToApplication raised: %s", exc)

        if stk is None:
            log.info("StkComServiceV13: no running STK Desktop — starting a new instance (visible)")
            stk = STKDesktop.StartApplication(visible=True)
        else:
            log.info("StkComServiceV13: attached to running STK Desktop instance")

        if stk is None:
            raise RuntimeError("Neither AttachToApplication nor StartApplication returned a Desktop object")

        # Try the two common accessors — `.Root` is the agi.stk12/13 default,
        # `.NewObjectRoot()` is the pattern used by STKEngine objects.
        root = getattr(stk, 'Root', None)
        if root is None and hasattr(stk, 'NewObjectRoot'):
            root = stk.NewObjectRoot()
        if root is None:
            raise RuntimeError(
                f"STKDesktop instance has no usable Root (attrs: "
                f"{sorted(a for a in dir(stk) if not a.startswith('_'))})"
            )
        return stk, root

    def build_and_compute(self, exercise_id, start_time, stop_time, entities):
        from scenario_builder_v2 import build_stk_scenario_v2
        build_stk_scenario_v2(self._root, exercise_id, start_time, stop_time, entities)
        log.info("StkComServiceV13: scenario built for exercise %s", exercise_id)

    def export_czml(self, exercise_id, output_path):
        from czml_utils import (patch_czml_sensors, inject_fom_rectangle,
                                render_fom_heatmap, extract_area_target_polygon)

        out = Path(output_path)
        out.parent.mkdir(parents=True, exist_ok=True)

        sc = self._root.CurrentScenario
        self._export_czml_connect(out, sc.InstanceName)
        patch_czml_sensors(out)

        coverage, fom_obj = self._find_coverage_fom(sc)
        if fom_obj:
            grid = self._read_fom_grid_v2(coverage, fom_obj, sc)
            if grid:
                img_dir  = Path(__file__).parent / "fom_images"
                img_dir.mkdir(exist_ok=True)
                img_path = img_dir / f"{exercise_id}.png"
                polygon_deg = extract_area_target_polygon(out)
                render_fom_heatmap(grid, img_path, polygon_deg=polygon_deg)
                inject_fom_rectangle(
                    out,
                    f"http://localhost:8002/exercises/{exercise_id}/fom-image",
                )
                log.info("StkComServiceV13: FOM heatmap -> %s", img_path)
            else:
                log.warning("StkComServiceV13: FOM DataProviders returned no data for %s",
                            exercise_id)
        else:
            log.info("StkComServiceV13: no FigureOfMerit — skipping heatmap")

    def _export_czml_connect(self, output_path: Path, scenario_name: str) -> None:
        abs_path = str(output_path.resolve()).replace('\\', '/')
        cmd = (f'ExportCZML */Scenario/{scenario_name} "{abs_path}" '
               f'http://localhost:8002/models/')
        try:
            self._root.ExecuteCommand(cmd)
        except Exception as exc:
            plugin_state = (
                f"plugin registered at {self._czml_plugin}"
                if self._czml_plugin is not None
                else "plugin NOT found in registry — install the CZML Exporter Plugin MSI"
            )
            raise RuntimeError(
                f"ExportCZML failed ({plugin_state}): {exc}"
            ) from exc
        if not output_path.exists() or output_path.stat().st_size == 0:
            raise RuntimeError("ExportCZML produced no output file")
        log.info("StkComServiceV13: CZML exported -> %s (%.1f KB)",
                 output_path, output_path.stat().st_size / 1024)

    def _find_coverage_fom(self, scenario):
        """Return (CoverageDefinition, FigureOfMerit) pair, or (None, None)."""
        for i in range(scenario.Children.Count):
            child = scenario.Children.Item(i)
            if getattr(child, 'ClassName', '').lower() != 'coveragedefinition':
                continue
            for j in range(child.Children.Count):
                sub = child.Children.Item(j)
                if getattr(sub, 'ClassName', '').lower() == 'figureofmerit':
                    return child, sub
        return None, None

    def _read_fom_grid_v2(self, coverage, fom_obj, scenario) -> list:
        """Return [(lat_deg, lon_deg, value), ...] for the FOM's grid points."""
        start, stop = scenario.StartTime, scenario.StopTime

        def _get_col(result, *candidates):
            for name in candidates:
                try:
                    return list(result.DataSets.GetDataSetByName(name).GetValues())
                except Exception:
                    pass
            return []

        def _try_dp(obj, dp_name, val_cols, exec_args=()):
            try:
                dp = obj.DataProviders.GetItemByName(dp_name)
                result = dp.Exec(*exec_args)
                lats = _get_col(result, 'Latitude', 'Lat', 'lat')
                lons = _get_col(result, 'Longitude', 'Lon', 'lon')
                if not lats or not lons:
                    log.warning("_read_fom_grid_v2: lat/lon columns missing from '%s'", dp_name)
                    return None
                for col in val_cols:
                    vals = _get_col(result, col)
                    if vals:
                        if len(lats) != len(lons) or len(lats) != len(vals):
                            log.warning(
                                "_read_fom_grid_v2: column length mismatch "
                                "(lat=%d lon=%d val=%d) for '%s'",
                                len(lats), len(lons), len(vals), dp_name)
                            return None
                        # Sanity check: lat/lon must be in degrees, not radians
                        if not (-90 <= lats[0] <= 90 and -180 <= lons[0] <= 180):
                            log.error(
                                "_read_fom_grid_v2: lat/lon out of degree range "
                                "(lat=%.4f lon=%.4f) — provider may be returning radians",
                                lats[0], lons[0])
                            return None
                        log.info("_read_fom_grid_v2: %d pts via '%s'/'%s'",
                                 len(lats), dp_name, col)
                        return list(zip(lats, lons, vals))
            except Exception as exc:
                log.warning("_read_fom_grid_v2: '%s' raised: %s", dp_name, exc)
            return None

        val_fom = ('FOM Value', 'Value', 'Satisfaction')
        val_cov = ('Percent Coverage', 'Coverage', 'Value')

        for dp in ('Value By Point', 'Static Satisfaction'):
            r = _try_dp(fom_obj, dp, val_fom, exec_args=())
            if r:
                return r

        r = _try_dp(coverage, 'Percent Coverage', val_cov,
                    exec_args=(start, stop, 3600.0))
        if r:
            return r

        return []

    def shutdown(self) -> None:
        """Terminate the STK application cleanly to avoid atexit access violations.

        Engine instances expose ``Terminate``; Desktop instances expose
        ``ShutDown`` / ``Quit`` (different method set in agi.stk13). Try each.
        """
        for method_name in ('Terminate', 'ShutDown', 'Quit'):
            method = getattr(self._stk, method_name, None)
            if callable(method):
                try:
                    method()
                    log.info("StkComServiceV13: STK %s() called cleanly", method_name)
                    return
                except Exception as exc:
                    log.debug("StkComServiceV13: %s() raised: %s", method_name, exc)
        log.debug("StkComServiceV13: no shutdown method found on %s",
                  type(self._stk).__name__)
