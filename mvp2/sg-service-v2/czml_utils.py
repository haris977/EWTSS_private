"""
Pure-Python CZML post-processing utilities.
No STK API calls — safe to import without STK installed.
Copied logic from mvp/sg-service/stk_com_service.py static methods.
"""
from __future__ import annotations
import json
import logging
import math
from pathlib import Path

log = logging.getLogger(__name__)


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
        lon, lat = _ecef_to_lonlat_rad(x, y, z)
        # Zenith unit vector in ECEF
        zx = math.cos(lat) * math.cos(lon)
        zy = math.cos(lat) * math.sin(lon)
        zz = math.sin(lat)
        # Shift center down so the apex (top of cylinder) is at the aircraft
        cart.extend([t, x - half_shift * zx, y - half_shift * zy, z - half_shift * zz])
        qx, qy, qz, qw = _enu_quaternion(lon, lat)
        quat.extend([t, qx, qy, qz, qw])
    return {'cartesian': cart, 'unitQuaternion': quat}


def inject_fom_rectangle(czml_path: Path, image_url: str) -> None:
    """
    Patch an existing CZML file to add a georeferenced image overlay for the
    FigureOfMerit result.

    Bounds are computed automatically from the first AreaTarget polygon found
    in the CZML (cartographicRadians format: [lon, lat, 0, ...]).
    The overlay is inserted only when FOM entities are present in the file.
    Idempotent: re-uploading a new image replaces the previous overlay.
    The wsen array is stored in radians as required by the CZML rectangle.coordinates specification.
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


def patch_czml_sensors(path: Path) -> None:
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
                sampled = _cone_position_and_orientation(raw, length / 2.0)
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
            "patch_czml_sensors: sensor '%s' → cylinder(length=%.0fm, bottomR=%.0fm)",
            pkt.get('id', '?'), length, b_rad,
        )

    if patched:
        path.write_text(json.dumps(out, indent=2), encoding='utf-8')


def extract_area_target_polygon(czml_path: Path) -> list[tuple[float, float]] | None:
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
        log.warning("extract_area_target_polygon: %s", exc)
    return None


def render_fom_heatmap(
    grid: list[tuple[float, float, float]],
    img_path: Path,
    polygon_deg: list[tuple[float, float]] | None = None,
) -> None:
    """
    Render FOM grid data as a smooth transparent RGBA PNG.
    Color scale: red (low) → yellow → green (high).

    grid: list of (lat_deg, lon_deg, value) tuples

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
