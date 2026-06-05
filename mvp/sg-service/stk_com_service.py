from __future__ import annotations

import json
import logging
import math
from pathlib import Path

from stk_service import IStkService

log = logging.getLogger(__name__)


class StkComService(IStkService):
    """
    Live STK integration via agi.stk12 COM automation.
    STK 12 must already be running (or this service will start it).
    Call get_stk_service() instead of instantiating directly.
    """

    def __init__(self) -> None:
        try:
            from agi.stk12.stkengine import STKEngine  # type: ignore[import]
            # noGraphics=False enables the graphics stack — required for the
            # CZML export plugin.  STKEngine still runs without a visible window.
            self._stk = STKEngine.StartApplication(noGraphics=False)
            # STKEngine returns IAgStkEngineApplication — root is obtained via NewObjectRoot()
            self._root = self._stk.NewObjectRoot()
            log.info("StkComService: started STK Engine (graphics enabled)")
        except Exception as eng_exc:
            log.warning("StkComService: STKEngine unavailable (%s), falling back to STKDesktop", eng_exc)
            from agi.stk12.stkdesktop import STKDesktop  # type: ignore[import]
            try:
                self._stk = STKDesktop.AttachToApplication()
                log.info("StkComService: attached to running STK Desktop instance")
            except Exception:
                self._stk = STKDesktop.StartApplication(visible=True)
                log.info("StkComService: started new STK Desktop instance (visible)")
            self._root = self._stk.Root

    def build_and_compute(
        self,
        exercise_id: str,
        start_time: str,
        stop_time: str,
    ) -> None:
        from scenario_builder import build_stk_scenario

        build_stk_scenario(
            root=self._root,
            exercise_id=exercise_id,
            start_time=start_time,
            stop_time=stop_time,
        )
        log.info("StkComService: computation complete for exercise %s", exercise_id)

    def load_scenario(self, file_path: str) -> None:
        """Load an existing STK scenario file and recompute platform access."""
        try:
            self._root.CloseScenario()
        except Exception:
            pass

        log.info("StkComService: loading scenario from %s", file_path)
        ext = Path(file_path).suffix.lower()
        if ext == '.vdf':
            self._root.LoadVDF(file_path, "")
        else:
            self._root.LoadScenario(file_path)
        sc = self._root.CurrentScenario
        log.info("StkComService: loaded scenario '%s'", sc.InstanceName)


    def _export_czml_via_connect(self, output_path: Path) -> bool:
        """
        Attempt CZML export using STK's ExportCZML Connect Command.
        Returns True if the file was written successfully, False otherwise.

        NOTE: ExportCZML is only available in STK Desktop (GUI) builds.
              STKEngine (headless) does not expose this Connect Command —
              ExecuteCommand raises "Command has failed" for every variant.
              When running headless the manual DataProvider export is used instead.
        """
        sc_name    = self._root.CurrentScenario.InstanceName
        abs_path   = str(output_path.resolve()).replace('\\', '/')
        # Use local model server so CZML resolves in air-gapped environments.
        # GET /models/{filename} searches all STK model subdirs, so a flat
        # base URL works for Air/, Ground/, Sea/, Space/, and any other category.
        model_base = "http://localhost:8001/models/"
        cmd        = f'ExportCZML */Scenario/{sc_name} "{abs_path}" {model_base}'
        log.info("StkComService: trying ExportCZML Connect Command: %s", cmd)
        try:
            self._root.ExecuteCommand(cmd)
            # AgExecCmdResult has no IsError — ExecuteCommand raises on failure.
        except Exception as exc:
            log.info("StkComService: ExportCZML not available: %s", exc)
            return False

        if output_path.exists() and output_path.stat().st_size > 0:
            log.info(
                "StkComService: ExportCZML succeeded → %s (%.1f KB)",
                output_path, output_path.stat().st_size / 1024,
            )
            return True

        return False

    # ── ECEF / ENU helpers (pure math, no external deps) ─────────────────────

    @staticmethod
    def _ecef_to_lonlat_rad(x: float, y: float, z: float) -> tuple[float, float]:
        """Return (lon_rad, lat_rad) for an ECEF point (alt not needed here)."""
        a  = 6_378_137.0
        e2 = 0.006_694_379_990_14
        lon = math.atan2(y, x)
        p   = math.sqrt(x * x + y * y)
        lat = math.atan2(z, p)
        for _ in range(10):
            N   = a / math.sqrt(1.0 - e2 * math.sin(lat) ** 2)
            lat = math.atan2(z + e2 * N * math.sin(lat), p)
        return lon, lat

    @staticmethod
    def _enu_quaternion(lon: float, lat: float) -> tuple[float, float, float, float]:
        """
        Quaternion (x, y, z, w) for the ENU frame at (lon_rad, lat_rad).
        Local axes in ECEF: X=East, Y=North, Z=Up (zenith).
        Derived from the rotation matrix M whose columns are E, N, U in ECEF.
        """
        sl, cl = math.sin(lon), math.cos(lon)
        sp, cp = math.sin(lat), math.cos(lat)
        # M columns: East=[-sl, cl, 0], North=[-sp·cl, -sp·sl, cp], Up=[cp·cl, cp·sl, sp]
        m00, m01, m02 = -sl,     -sp * cl,  cp * cl
        m10, m11, m12 =  cl,     -sp * sl,  cp * sl
        m20, m21, m22 =   0.0,       cp,       sp
        tr = m00 + m11 + m22
        if tr > 0:
            s = 0.5 / math.sqrt(tr + 1.0)
            return (m21 - m12) * s, (m02 - m20) * s, (m10 - m01) * s, 0.25 / s
        if m00 > m11 and m00 > m22:
            s = 2.0 * math.sqrt(1.0 + m00 - m11 - m22)
            return 0.25 * s, (m01 + m10) / s, (m02 + m20) / s, (m21 - m12) / s
        if m11 > m22:
            s = 2.0 * math.sqrt(1.0 + m11 - m00 - m22)
            return (m01 + m10) / s, 0.25 * s, (m12 + m21) / s, (m02 - m20) / s
        s = 2.0 * math.sqrt(1.0 + m22 - m00 - m11)
        return (m02 + m20) / s, (m12 + m21) / s, 0.25 * s, (m10 - m01) / s

    @staticmethod
    def _cone_position_and_orientation(
        aircraft_cartesian: list[float],
        half_shift: float,
    ) -> dict[str, list[float]]:
        """
        For each aircraft ECEF sample [t, x, y, z, ...], compute:
          - Cone-center position: aircraft_ecef − half_shift × zenith_unit
            → ensures the cylinder TOP (topRadius=0, local+Z=up) sits exactly
              at the aircraft ECEF position (apex at aircraft).
          - ENU orientation quaternion so local+Z = zenith (cone axis = nadir).
        Returns {'cartesian': [...], 'unitQuaternion': [...]}.
        """
        cart, quat = [], []
        for i in range(0, len(aircraft_cartesian), 4):
            t, x, y, z = (aircraft_cartesian[i], aircraft_cartesian[i + 1],
                          aircraft_cartesian[i + 2], aircraft_cartesian[i + 3])
            lon, lat = StkComService._ecef_to_lonlat_rad(x, y, z)
            # Zenith unit vector in ECEF
            zx = math.cos(lat) * math.cos(lon)
            zy = math.cos(lat) * math.sin(lon)
            zz = math.sin(lat)
            # Shift center down so the apex (top of cylinder) is at the aircraft
            cart.extend([t, x - half_shift * zx, y - half_shift * zy, z - half_shift * zz])
            qx, qy, qz, qw = StkComService._enu_quaternion(lon, lat)
            quat.extend([t, qx, qy, qz, qw])
        return {'cartesian': cart, 'unitQuaternion': quat}

    @staticmethod
    def inject_fom_rectangle(czml_path: Path, image_url: str) -> None:
        """
        Patch an existing CZML file to add a georeferenced image overlay for the
        FigureOfMerit result.

        Bounds are computed automatically from the first AreaTarget polygon found
        in the CZML (cartographicRadians format: [lon, lat, 0, ...]).
        The overlay is inserted only when FOM entities are present in the file.
        Idempotent: re-uploading a new image replaces the previous overlay.
        """
        data = json.loads(czml_path.read_text(encoding='utf-8'))

        has_fom    = any('figureofmerit' in p.get('id', '').lower() for p in data)
        if not has_fom:
            log.warning("inject_fom_rectangle: no FigureOfMerit entity in CZML — skipping")
            return

        # Derive bounding box from the first AreaTarget polygon.
        west = south = east = north = None
        for p in data:
            positions = p.get('polygon', {}).get('positions', {})
            rad = positions.get('cartographicRadians')
            if rad and len(rad) >= 6:
                lons = rad[0::3]
                lats = rad[1::3]
                west  = min(lons)
                east  = max(lons)
                south = min(lats)
                north = max(lats)
                log.info(
                    "inject_fom_rectangle: bounds W=%.4f S=%.4f E=%.4f N=%.4f (deg)",
                    math.degrees(west), math.degrees(south),
                    math.degrees(east), math.degrees(north),
                )
                break

        if west is None:
            log.warning("inject_fom_rectangle: no AreaTarget polygon found — cannot derive bounds")
            return

        overlay_id = "fom-overlay"
        # Remove any previous overlay so the call is idempotent.
        data = [p for p in data if p.get('id') != overlay_id]

        doc_avail = next(
            (p['clock']['interval'] for p in data if p.get('id') == 'document' and 'clock' in p),
            None,
        )
        data.append({
            "id":           overlay_id,
            "name":         "FigureOfMerit overlay",
            **({"availability": doc_avail} if doc_avail else {}),
            "rectangle": {
                "coordinates": {"wsen": [west, south, east, north]},
                "material": {
                    "image": {
                        "image":       image_url,
                        "transparent": True,
                        "repeat":      {"cartesian2": [1, 1]},
                    }
                },
                "height":             0.0,
                "classificationType": "TERRAIN",
                "zIndex":             1,
            },
        })

        czml_path.write_text(json.dumps(data, indent=2), encoding='utf-8')
        log.info("inject_fom_rectangle: overlay added to %s", czml_path.name)

    @staticmethod
    def _patch_czml_sensors(path: Path) -> None:
        """
        Replace agi_conicSensor packets with native Cesium cylinder packets.

        Geometry (ENU orientation, local +Z = zenith):
          topRadius=0   → apex at local +Z end → AT the aircraft position
          bottomRadius=R → wide base at local -Z end → below aircraft (toward Earth)
        The cylinder center is shifted down by length/2 so the apex falls exactly
        at the aircraft ECEF position rather than length/2 above it.
        """
        data    = json.loads(path.read_text(encoding='utf-8'))
        by_id   = {p['id']: p for p in data if isinstance(p, dict) and 'id' in p}
        out     : list[dict] = []
        patched = False

        for pkt in data:
            if 'agi_conicSensor' not in pkt:
                out.append(pkt)
                continue

            cs     = pkt.pop('agi_conicSensor')
            outer  = cs.get('outerHalfAngle', 0.5)
            length = min(float(cs.get('radius', 20_000)), 20_000)
            b_rad  = length * math.tan(outer)
            rgba   = (
                (cs.get('lateralSurfaceMaterial') or {})
                .get('solidColor', {}).get('color', {}).get('rgba', [0, 255, 255, 100])
            )
            parent_id = pkt.get('parent', '')
            parent    = by_id.get(parent_id)

            if parent:
                pos_prop = parent.get('position', {})
                raw      = pos_prop.get('cartesian', [])
                if isinstance(raw, list) and len(raw) >= 4 and len(raw) % 4 == 0:
                    epoch  = pos_prop.get('epoch', '2000-01-01T00:00:00Z')
                    sampled = StkComService._cone_position_and_orientation(raw, length / 2.0)
                    pkt['position'] = {
                        'epoch':                   epoch,
                        'interpolationAlgorithm':  'LAGRANGE',
                        'interpolationDegree':     1,
                        'cartesian':               sampled['cartesian'],
                    }
                    pkt['orientation'] = {
                        'epoch':                   epoch,
                        'interpolationAlgorithm':  'LINEAR',
                        'interpolationDegree':     1,
                        'unitQuaternion':           sampled['unitQuaternion'],
                    }
                else:
                    pkt['position'] = {'reference': f'{parent_id}#position'}
            elif parent_id:
                pkt['position'] = {'reference': f'{parent_id}#position'}

            pkt['cylinder'] = {
                'length':                length,
                'topRadius':             0.0,
                'bottomRadius':          b_rad,
                'material':              {'solidColor': {'color': {'rgba': rgba}}},
                'outline':               True,
                'outlineColor':          {'rgba': rgba[:3] + [220]},
                'numberOfVerticalLines': 8,
            }
            # Label at aircraft position so it's not buried in the cone base
            pkt['label'] = {
                'show':             True,
                'text':             pkt.get('name', 'Sensor'),
                'font':             'bold 12pt Consolas',
                'style':            'FILL_AND_OUTLINE',
                'scale':            0.6,
                'fillColor':        {'rgba': rgba[:3] + [255]},
                'outlineColor':     {'rgba': [0, 0, 0, 255]},
                'outlineWidth':     2,
                'pixelOffset':      {'cartesian2': [5, -4]},
                'horizontalOrigin': 'LEFT',
                'verticalOrigin':   'CENTER',
                'eyeOffset':        {'cartesian': [0.0, length / 2.0, 0.0]},
            }
            out.append(pkt)
            patched = True
            log.info(
                "StkComService: sensor '%s' → cylinder(length=%.0fm, bottomR=%.0fm)",
                pkt.get('id', '?'), length, b_rad,
            )

        if patched:
            path.write_text(json.dumps(out, indent=2), encoding='utf-8')

    def export_czml(self, exercise_id: str, output_path: str) -> None:
        out = Path(output_path)
        out.parent.mkdir(parents=True, exist_ok=True)

        if not self._export_czml_via_connect(out):
            raise RuntimeError(
                "ExportCZML Connect Command failed. "
                "Ensure the CZML Export plugin is installed in STK and "
                "STK Engine is started with noGraphics=False."
            )

        self._patch_czml_sensors(out)
        self._auto_export_fom_image(exercise_id, out)

    # ── FOM image auto-generation ─────────────────────────────────────────────

    def _auto_export_fom_image(self, exercise_id: str, czml_path: Path) -> None:
        """
        Locate CoverageDefinition/FigureOfMerit in the loaded scenario, read per-
        grid-point FOM values via STK DataProviders, render a transparent PNG
        heatmap, and inject it as a rectangle overlay into the CZML.

        Silently skips when no FOM objects exist or when the DataProvider call
        fails (different STK builds expose different provider names).  The manual
        POST /exercises/{id}/fom-image endpoint remains as a fallback.
        """
        try:
            from PIL import Image  # noqa: F401 — presence check only
        except ImportError:
            log.warning("_auto_export_fom_image: Pillow not installed — run 'pip install Pillow'")
            return

        sc = self._root.CurrentScenario

        # Find first CoverageDefinition that has a FigureOfMerit child.
        coverage = fom_obj = None
        for i in range(sc.Children.Count):
            child = sc.Children.Item(i)
            if getattr(child, 'ClassName', '').lower() != 'coveragedefinition':
                continue
            coverage = child
            for j in range(child.Children.Count):
                fom_child = child.Children.Item(j)
                if getattr(fom_child, 'ClassName', '').lower() == 'figureofmerit':
                    fom_obj = fom_child
                    break
            if fom_obj:
                break

        if not fom_obj:
            log.info("_auto_export_fom_image: no FigureOfMerit in scenario — skipping")
            return

        log.info("_auto_export_fom_image: reading grid from '%s'", fom_obj.InstanceName)
        grid = self._read_fom_grid(coverage, fom_obj, sc)
        if not grid:
            log.warning(
                "_auto_export_fom_image: all DataProvider attempts failed — "
                "use POST /exercises/%s/fom-image to upload manually", exercise_id,
            )
            return

        img_dir  = Path("fom_images")
        img_dir.mkdir(exist_ok=True)
        img_path = img_dir / f"{exercise_id}.png"
        # Extract AreaTarget polygon for pixel-level clipping of the heatmap.
        polygon_deg = self._extract_area_target_polygon(czml_path)
        self._render_fom_heatmap(grid, img_path, polygon_deg=polygon_deg)
        log.info(
            "_auto_export_fom_image: heatmap written → %s (%.1f KB, %d cells)",
            img_path, img_path.stat().st_size / 1024, len(grid),
        )
        self.inject_fom_rectangle(czml_path, f"http://localhost:8001/exercises/{exercise_id}/fom-image")

    def _read_fom_grid(self, coverage, fom_obj, sc) -> list[tuple[float, float, float]]:
        """
        Return [(lat_deg, lon_deg, value), ...] for all grid cells.
        Tries DataProvider names in order of likelihood across STK 12 builds.
        Returns an empty list if every attempt fails.
        """
        start, stop = sc.StartTime, sc.StopTime

        def _col_names(result) -> list[str]:
            try:
                return [result.DataSets.Item(k).Name for k in range(result.DataSets.Count)]
            except Exception:
                return []

        def _get_col(result, *candidates):
            for name in candidates:
                try:
                    return list(result.DataSets.GetDataSetByName(name).GetValues())
                except Exception:
                    continue
            return []

        def _try_dp(obj, dp_name: str, val_cols: tuple, exec_args=()) -> list | None:
            """Try a named DataProvider on obj; return [(lat,lon,val),...] or None."""
            try:
                result = obj.DataProviders.GetItemByName(dp_name).Exec(*exec_args)
                cols   = _col_names(result)
                lats   = _get_col(result, 'Latitude', 'Lat', 'lat')
                lons   = _get_col(result, 'Longitude', 'Lon', 'lon')
                if not lats:
                    log.warning("_read_fom_grid: '%s' exec OK but no lat column; columns=%s",
                                dp_name, cols)
                    return None
                for col in val_cols:
                    vals = _get_col(result, col)
                    if vals:
                        log.info("_read_fom_grid: %d points via '%s'/'%s'",
                                 len(lats), dp_name, col)
                        return list(zip(lats, lons, vals))
                log.warning("_read_fom_grid: '%s' exec OK, lat found, but no value column; columns=%s",
                            dp_name, cols)
            except Exception as exc:
                log.warning("_read_fom_grid: '%s' on %s raised: %s",
                            dp_name, type(obj).__name__, exc)
            return None

        val_cols_fom = ('Value', 'FOM Value', 'Percent Coverage', 'Coverage')
        val_cols_cov = ('Percent Coverage', 'Coverage', 'Value')

        # FOM "Value By Point": static DataProvider — Exec() takes no time arguments.
        result = _try_dp(fom_obj, 'Value By Point', val_cols_fom, exec_args=())
        if result:
            return result

        # FOM "Static Satisfaction": also static, no time args.
        result = _try_dp(fom_obj, 'Static Satisfaction', val_cols_fom, exec_args=())
        if result:
            return result

        # Coverage "Percent Coverage": needs Exec(start, stop, stepTime) — use full interval.
        result = _try_dp(coverage, 'Percent Coverage', val_cols_cov,
                         exec_args=(start, stop, 3600.0))
        if result:
            return result

        # --- Fallback: Grid Point Locations (no-arg static) + Value By Point (no-arg) ---
        try:
            loc_res = coverage.DataProviders.GetItemByName('Grid Point Locations').Exec()
            lats = list(loc_res.DataSets.GetDataSetByName('Latitude').GetValues())
            lons = list(loc_res.DataSets.GetDataSetByName('Longitude').GetValues())
            if not lats:
                raise RuntimeError("Grid Point Locations returned no rows")

            val_res = fom_obj.DataProviders.GetItemByName('Value By Point').Exec()
            for col in ('Value', 'FOM Value', 'Percent Coverage'):
                try:
                    vals = list(val_res.DataSets.GetDataSetByName(col).GetValues())
                    if vals:
                        n = min(len(lats), len(lons), len(vals))
                        log.info("_read_fom_grid: %d points via Grid Point Locations + Value By Point", n)
                        return list(zip(lats[:n], lons[:n], vals[:n]))
                except Exception:
                    continue
        except Exception as exc:
            log.warning("_read_fom_grid: Grid Point Locations fallback raised: %s", exc)

        # Nothing matched — log available names to help diagnose
        for obj, label in ((fom_obj, 'FOM'), (coverage, 'Coverage')):
            try:
                names = [obj.DataProviders.Item(k).Name
                         for k in range(obj.DataProviders.Count)]
                log.warning("_read_fom_grid: available %s DataProviders: %s", label, names)
            except Exception as exc:
                log.warning("_read_fom_grid: could not enumerate %s DataProviders: %s", label, exc)
        return []

    @staticmethod
    def _extract_area_target_polygon(czml_path: Path) -> list[tuple[float, float]] | None:
        """Return [(lon_deg, lat_deg), ...] for the first AreaTarget polygon in the CZML."""
        try:
            data = json.loads(czml_path.read_text(encoding='utf-8'))
            for p in data:
                rad = p.get('polygon', {}).get('positions', {}).get('cartographicRadians')
                if rad and len(rad) >= 6:
                    lons = rad[0::3]
                    lats = rad[1::3]
                    return [(math.degrees(lon), math.degrees(lat))
                            for lon, lat in zip(lons, lats)]
        except Exception as exc:
            log.warning("_extract_area_target_polygon: %s", exc)
        return None

    @staticmethod
    def _render_fom_heatmap(
        grid: list[tuple[float, float, float]],
        img_path: Path,
        polygon_deg: list[tuple[float, float]] | None = None,
    ) -> None:
        """
        Render FOM grid data as a smooth transparent RGBA PNG.
        Color scale: red (low) → yellow → green (high).

        Uses scipy.interpolate.griddata (linear) to interpolate scattered FOM
        points onto a regular 512×512 output grid whose geographic extent matches
        the AreaTarget polygon bounding box — the same bounds used for the CZML
        rectangle — so the PNG aligns pixel-perfectly with the overlay.
        The polygon is then applied as a hard mask so nothing bleeds outside it.
        """
        import numpy as np
        from PIL import Image, ImageDraw, ImageFilter
        from scipy.interpolate import griddata

        lats = np.array([r[0] for r in grid], dtype=np.float64)
        lons = np.array([r[1] for r in grid], dtype=np.float64)
        vals = np.array([r[2] for r in grid], dtype=np.float64)

        min_val, max_val = vals.min(), vals.max()
        norm_vals = (vals - min_val) / ((max_val - min_val) or 1.0)

        # Image geographic extent = polygon bounding box (= CZML rectangle bounds).
        # This guarantees pixel-to-geo alignment between the PNG and the overlay.
        if polygon_deg:
            p_lons = [p[0] for p in polygon_deg]
            p_lats = [p[1] for p in polygon_deg]
            lon_min, lon_max = min(p_lons), max(p_lons)
            lat_min, lat_max = min(p_lats), max(p_lats)
        else:
            lon_min, lon_max = lons.min(), lons.max()
            lat_min, lat_max = lats.min(), lats.max()

        lon_span = (lon_max - lon_min) or 1.0
        lat_span = (lat_max - lat_min) or 1.0
        out_w = out_h = 512

        # Regular output grid (N at top → lat decreasing with row index)
        xi = np.linspace(lon_min, lon_max, out_w)
        yi = np.linspace(lat_max, lat_min, out_h)
        xi_g, yi_g = np.meshgrid(xi, yi)

        # Smooth interpolation: linear fills the interior cleanly; NaN outside convex hull
        norm_interp = griddata(
            np.column_stack([lons, lats]),
            norm_vals,
            (xi_g, yi_g),
            method='linear',
        ).astype(np.float32)

        data_mask = ~np.isnan(norm_interp)
        norm_interp = np.where(data_mask, norm_interp, 0.0)

        # Red → yellow → green colour map
        r_ch = np.where(norm_interp >= 0.5, (255 * 2 * (1.0 - norm_interp)).clip(0, 255), 255).astype(np.uint8)
        g_ch = np.where(norm_interp <= 0.5, (255 * 2 * norm_interp).clip(0, 255),          255).astype(np.uint8)
        b_ch = np.zeros_like(r_ch)
        a_ch = np.where(data_mask, np.uint8(210), np.uint8(0))

        rgb_img   = Image.fromarray(np.stack([r_ch, g_ch, b_ch], axis=-1), 'RGB')
        alpha_img = Image.fromarray(a_ch, 'L')

        # Light blur on RGB only to smooth colour transitions; alpha stays hard
        rgb_img = rgb_img.filter(ImageFilter.GaussianBlur(radius=1.5))

        # Hard polygon clip: crisp full-opacity boundary, no feathering
        if polygon_deg:
            def _to_px(lon_d: float, lat_d: float) -> tuple[float, float]:
                return ((lon_d - lon_min) / lon_span * out_w,
                        (lat_max - lat_d) / lat_span * out_h)

            poly_px   = [_to_px(lon, lat) for lon, lat in polygon_deg]
            poly_mask = Image.new('L', (out_w, out_h), 0)
            ImageDraw.Draw(poly_mask).polygon(poly_px, fill=255)
            # Sub-pixel antialiasing only — no visible fade
            poly_mask = poly_mask.filter(ImageFilter.GaussianBlur(radius=0.5))

            combined  = (np.array(alpha_img, np.float32) * np.array(poly_mask, np.float32) / 255.0)
            alpha_img = Image.fromarray(combined.clip(0, 255).astype(np.uint8), 'L')

        Image.merge('RGBA', (*rgb_img.split(), alpha_img)).save(img_path, 'PNG')

    def shutdown(self) -> None:
        """
        Explicitly terminate the STK Engine/Desktop instance.
        Must be called before Python interpreter teardown to avoid the
        access-violation crash in STK's own atexit handler, which fires
        after sys.meta_path is already None.
        """
        try:
            self._stk.Terminate()
            log.info("StkComService: STK terminated cleanly")
        except Exception as exc:
            log.debug("StkComService: STK terminate raised (may already be shut down): %s", exc)


_service_instance: IStkService | None = None


def get_stk_service() -> IStkService:
    global _service_instance
    if _service_instance is not None:
        return _service_instance

    from stk_mock_service import MockStkService

    try:
        import agi.stk12  # noqa: F401
        _service_instance = StkComService()
        log.info("get_stk_service: using StkComService (live STK)")
    except Exception as exc:
        log.warning("get_stk_service: STK unavailable (%s), using MockStkService", exc)
        _service_instance = MockStkService()

    return _service_instance
