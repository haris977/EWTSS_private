"""FOM rendering pipeline — read grid via DataProviders, render a transparent
PNG heatmap, inject a CZML rectangle overlay pointing at the PNG.

Each function is pure: no module state, no STK globals. Ports MVP1's FOM
logic verbatim but extracted into free functions so the unit tests can drive
them with fixture data.
"""
from __future__ import annotations
import logging
from pathlib import Path

log = logging.getLogger(__name__)


def read_fom_grid(coverage, fom, scenario) -> list[tuple[float, float, float]]:
    """Return ``[(lat_deg, lon_deg, value), ...]`` or an empty list if every
    DataProvider attempt fails. Tries provider names in order of likelihood."""

    start, stop = scenario.StartTime, scenario.StopTime

    def _col(result, *candidates):
        for name in candidates:
            try:
                return list(result.DataSets.GetDataSetByName(name).GetValues())
            except Exception:
                continue
        return []

    def _try(obj, dp_name: str, val_cols: tuple, exec_args=()) -> list | None:
        try:
            result = obj.DataProviders.GetItemByName(dp_name).Exec(*exec_args)
        except Exception as exc:
            log.warning("read_fom_grid: '%s' not available: %s", dp_name, exc)
            return None
        lats = _col(result, "Latitude", "Lat", "lat")
        lons = _col(result, "Longitude", "Lon", "lon")
        if not lats or not lons:
            return None
        for col in val_cols:
            vals = _col(result, col)
            if vals:
                n = min(len(lats), len(lons), len(vals))
                log.info("read_fom_grid: %d pts via '%s'/'%s'", n, dp_name, col)
                return list(zip(lats[:n], lons[:n], vals[:n]))
        return None

    val_fom = ("Value", "FOM Value", "Percent Coverage", "Coverage")
    val_cov = ("Percent Coverage", "Coverage", "Value")

    for dp_name in ("Value By Point", "Static Satisfaction"):
        r = _try(fom, dp_name, val_fom)
        if r:
            return r
    r = _try(coverage, "Percent Coverage", val_cov, exec_args=(start, stop, 3600.0))
    if r:
        return r
    return []


def render_heatmap_png(grid, polygon_deg, out_path: Path) -> None:
    """Render an RGBA PNG from ``grid = [(lat, lon, val), ...]``.

    If ``polygon_deg`` is provided (``[(lon, lat), ...]``), the image extent
    matches the polygon bounding box and the polygon acts as a hard clip mask.
    """
    import numpy as np
    from PIL import Image, ImageDraw, ImageFilter
    from scipy.interpolate import griddata

    lats = np.array([r[0] for r in grid], dtype=np.float64)
    lons = np.array([r[1] for r in grid], dtype=np.float64)
    vals = np.array([r[2] for r in grid], dtype=np.float64)

    v_min, v_max = vals.min(), vals.max()
    norm = (vals - v_min) / ((v_max - v_min) or 1.0)

    if polygon_deg:
        p_lons = [p[0] for p in polygon_deg]
        p_lats = [p[1] for p in polygon_deg]
        lon_min, lon_max = min(p_lons), max(p_lons)
        lat_min, lat_max = min(p_lats), max(p_lats)
    else:
        lon_min, lon_max = float(lons.min()), float(lons.max())
        lat_min, lat_max = float(lats.min()), float(lats.max())

    w = h = 512
    xi = np.linspace(lon_min, lon_max, w)
    yi = np.linspace(lat_max, lat_min, h)
    gx, gy = np.meshgrid(xi, yi)

    points = np.column_stack([lons, lats])
    try:
        interp = griddata(points, norm, (gx, gy), method="linear").astype(np.float32)
        # If linear produces all NaN (e.g. collinear points), fall back to nearest
        if np.all(np.isnan(interp)):
            raise ValueError("linear produced all-NaN; falling back to nearest")
    except Exception:
        log.warning("render_heatmap_png: linear interpolation failed, using nearest")
        interp = griddata(points, norm, (gx, gy), method="nearest").astype(np.float32)
    mask = ~np.isnan(interp)
    interp = np.where(mask, interp, 0.0)

    r_ch = np.where(interp >= 0.5, (255 * 2 * (1.0 - interp)).clip(0, 255), 255).astype(np.uint8)
    g_ch = np.where(interp <= 0.5, (255 * 2 * interp).clip(0, 255),         255).astype(np.uint8)
    b_ch = np.zeros_like(r_ch)
    a_ch = np.where(mask, np.uint8(210), np.uint8(0))

    rgb   = Image.fromarray(np.stack([r_ch, g_ch, b_ch], axis=-1), "RGB")
    alpha = Image.fromarray(a_ch, "L")
    rgb   = rgb.filter(ImageFilter.GaussianBlur(radius=1.5))

    if polygon_deg:
        def _to_px(lon_d, lat_d):
            return ((lon_d - lon_min) / max(lon_max - lon_min, 1e-9) * w,
                    (lat_max - lat_d) / max(lat_max - lat_min, 1e-9) * h)
        poly_px   = [_to_px(lo, la) for lo, la in polygon_deg]
        poly_mask = Image.new("L", (w, h), 0)
        ImageDraw.Draw(poly_mask).polygon(poly_px, fill=255)
        poly_mask = poly_mask.filter(ImageFilter.GaussianBlur(radius=0.5))
        import numpy as _np
        combined  = (_np.array(alpha, _np.float32) * _np.array(poly_mask, _np.float32) / 255.0)
        alpha     = Image.fromarray(combined.clip(0, 255).astype("uint8"), "L")

    Path(out_path).parent.mkdir(parents=True, exist_ok=True)
    Image.merge("RGBA", (*rgb.split(), alpha)).save(str(out_path), "PNG")


def inject_rectangle_overlay(document, image_url, polygon_bounds_rad):
    """Return a new document with an extra CZML packet for the FOM overlay."""
    if polygon_bounds_rad is None:
        return document
    w, s, e, n = polygon_bounds_rad
    overlay = {
        "id":   "fom-overlay",
        "name": "FigureOfMerit overlay",
        "rectangle": {
            "coordinates": {"wsen": [w, s, e, n]},
            "material": {
                "image": {
                    "image":       image_url,
                    "transparent": True,
                    "repeat":      {"cartesian2": [1, 1]},
                },
            },
            "height":             0.0,
            "classificationType": "TERRAIN",
            "zIndex":             1,
        },
    }
    return [p for p in document if p.get("id") != "fom-overlay"] + [overlay]
