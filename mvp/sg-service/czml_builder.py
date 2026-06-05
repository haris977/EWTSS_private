"""
Generates a synthetic CZML document with two moving platforms:
  - Emitter aircraft: wide-angle RF emission cone (orange), low altitude in Kashmir Valley
  - Radar aircraft:   narrow detection cone (cyan), high altitude over Zanskar Range

Visual elements:
  - Per-platform: path trail, point marker, label, downward-pointing cone (cylinder)
  - LOS line:  green polyline — visible only during computed LOS-clear intervals
  - RLOS line: amber glowing polyline — same availability as LOS line

LOS availability is computed by sampling terrain height along the emitter→radar path
using a Gaussian terrain model calibrated to Kashmir's Pir Panjal and Great Himalayan
ridges.  The emitter at 2 200 m (just above the valley floor) is frequently occluded
by ridges that top 4 000–5 500 m, producing realistic intermittent LOS loss.

Used by MockStkService. When real STK is available, StkComService replaces this with
an authentic CZML export driven by STK's ComputedAccessIntervals.
"""
from __future__ import annotations

import math
from datetime import datetime, timedelta, timezone


def _stk_time_to_iso(stk_time: str) -> str:
    """Convert STK time string to ISO-8601 UTC.

    Handles millisecond ('1 Jan 2025 00:00:00.000'),
    microsecond ('…00:00:00.000000'),
    and nanosecond ('…00:00:00.000000000') variants.
    """
    import re as _re
    s = stk_time.strip()
    # Truncate fractional seconds to at most 6 digits so %f always matches.
    s = _re.sub(r'(\.\d{6})\d+', r'\1', s)
    for fmt in ("%d %b %Y %H:%M:%S.%f", "%d %b %Y %H:%M:%S"):
        try:
            dt = datetime.strptime(s, fmt)
            return dt.replace(tzinfo=timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")
        except ValueError:
            continue
    raise ValueError(f"Cannot parse STK time string: {stk_time!r}")


# ── Simplified Kashmir terrain model ────────────────────────────────────────

def _terrain_height_m(lat: float, lon: float) -> float:
    """
    Approximate terrain height (m ASL) for the Kashmir / Himalayan AoI.

    Three Gaussian ridge functions calibrated to the main ranges:
      Pir Panjal Range       ~33.35°N  4 000–4 800 m  south wall of Kashmir Valley
      Great Himalayan Range  ~35.0°N   4 500–6 000 m  north wall
      Zanskar Range          ~34.8°N   3 800–5 000 m  northeast (radar orbit area)

    Valley floor ≈ 1 600 m ASL.
    """
    h = 1600.0
    h += 3200.0 * math.exp(-((lat - 33.35) ** 2) / 0.12  - ((lon - 74.5) ** 2) / 2.0)
    h += 4500.0 * math.exp(-((lat - 35.00) ** 2) / 0.15  - ((lon - 75.0) ** 2) / 3.0)
    h += 3500.0 * math.exp(-((lat - 34.80) ** 2) / 0.08  - ((lon - 76.0) ** 2) / 0.6)
    return h


def _has_los(
    e_lat: float, e_lon: float, e_alt_m: float,
    r_lat: float, r_lon: float, r_alt_m: float,
    n: int = 24,
) -> bool:
    """
    Return True if the straight emitter→radar path clears all terrain sample points.
    Checks n−1 interpolated points (skips the endpoints themselves).
    """
    for i in range(1, n):
        f   = i / n
        lat = e_lat + f * (r_lat - e_lat)
        lon = e_lon + f * (r_lon - e_lon)
        alt = e_alt_m + f * (r_alt_m - e_alt_m)
        if alt < _terrain_height_m(lat, lon):
            return False
    return True


def _los_availability(
    emitter_samples: list[tuple[float, float, float, float]],
    radar_samples:   list[tuple[float, float, float, float]],
    start_dt:        datetime,
    stop_iso:        str,
) -> list[str]:
    """
    Return a list of CZML interval strings during which emitter has LOS to radar.

    emitter_samples / radar_samples: [(t_sec_from_epoch, lon, lat, alt_m), ...]
    Falls back to a single full-duration interval if no LOS is ever blocked.
    """
    intervals: list[str] = []
    in_los    = False
    seg_start = 0.0

    for (et, e_lon, e_lat, e_alt), (_, r_lon, r_lat, r_alt) in zip(
        emitter_samples, radar_samples
    ):
        visible = _has_los(e_lat, e_lon, e_alt, r_lat, r_lon, r_alt)
        if visible and not in_los:
            in_los    = True
            seg_start = et
        elif not visible and in_los:
            in_los = False
            t0 = (start_dt + timedelta(seconds=seg_start)).strftime("%Y-%m-%dT%H:%M:%SZ")
            t1 = (start_dt + timedelta(seconds=et)).strftime("%Y-%m-%dT%H:%M:%SZ")
            intervals.append(f"{t0}/{t1}")

    if in_los:
        t0 = (start_dt + timedelta(seconds=seg_start)).strftime("%Y-%m-%dT%H:%M:%SZ")
        intervals.append(f"{t0}/{stop_iso}")

    # Safety: if nothing was ever blocked return full availability
    full = f"{start_dt.strftime('%Y-%m-%dT%H:%M:%SZ')}/{stop_iso}"
    return intervals if intervals else [full]


# ── Circular track generator ─────────────────────────────────────────────────

def _circle_track(
    center_lat: float,
    center_lon: float,
    radius_deg: float,
    alt_m: float,
    duration: int,
    step: int,
    clockwise: bool = True,
    orbits: int = 1,
) -> list[float]:
    """
    Return cartographicDegrees samples for a circular orbit.
    Format: [t0, lon0, lat0, alt0,  t1, lon1, lat1, alt1,  ...]

    orbits — how many full circles to complete within duration.
    """
    direction = 1.0 if clockwise else -1.0
    coords: list[float] = []
    t = 0
    while t <= duration:
        angle = math.radians(direction * t * orbits * 360.0 / duration)
        lon = center_lon + radius_deg * math.cos(angle)
        lat = center_lat + radius_deg * math.sin(angle)
        coords.extend([float(t), lon, lat, float(alt_m)])
        t += step
    return coords


def _platform_packets(
    entity_id: str,
    name: str,
    availability: str,
    epoch: str,
    positions: list[float],
    point_color: list[int],
    path_color: list[int],
    cone_color: list[int],
    cone_radius_m: float,
    cone_length_m: float,
    horizontal_disc: bool = False,
    trail_time: int | None = None,
    model_uri: str | None = None,
    orientation: dict | None = None,
) -> dict:
    """Return a single CZML entity dict for a moving platform with a cone or disc.

    horizontal_disc=True renders a flat ellipsoid (horizontal RF disc) instead of a
    downward-pointing cylinder, representing a horizontal emission pattern for RLOS.
    trail_time: seconds of path history to show. None shows the full path.
    model_uri: optional glTF URI; when provided a 3-D model is shown instead of
               the billboard icon.
    """
    cone_color_dim = cone_color[:3] + [70]
    cone_outline   = cone_color[:3] + [200]

    path_packet: dict = {
        "width":      2,
        "resolution": 60,
        "material":   {"solidColor": {"color": {"rgba": path_color}}},
    }
    if trail_time is not None:
        # Both lead and trail equal to full duration → complete track always visible.
        path_packet["leadTime"]  = trail_time
        path_packet["trailTime"] = trail_time

    # Small aircraft silhouette encoded as a 8×8 cyan PNG (same icon STK uses).
    _AIRCRAFT_ICON = (
        "data:image/png;base64,"
        "iVBORw0KGgoAAAANSUhEUgAAAAgAAAAICAYAAADED76LAAAAAXNSR0IArs4c6QAAAA"
        "RnQU1BAACxjwv8YQUAAAAJcEhZcwAADsMAAA7DAcdvqGQAAAAaSURBVChTY2AgFvxH"
        "A3glYYCeCrApQpHEBwDC13+BxzsuqQAAAABJRU5ErkJggg=="
    )

    packet: dict = {
        "id":           entity_id,
        "name":         name,
        "availability": availability,
        "position": {
            "epoch":                     epoch,
            "interpolationAlgorithm":    "LAGRANGE",
            "interpolationDegree":       1,
            "forwardExtrapolationType":  "HOLD",
            "backwardExtrapolationType": "HOLD",
            "cartographicDegrees":       positions,
        },
        "path": path_packet,
        "billboard": {
            "show":             True,
            "image":            _AIRCRAFT_ICON,
            "scale":            1,
            "pixelOffset":      {"cartesian2": [0, 0]},
            "eyeOffset":        {"cartesian": [0, 0, 0]},
            "horizontalOrigin": "CENTER",
            "verticalOrigin":   "CENTER",
            "color":            {"rgba": point_color},
        },
        "label": {
            "show":             True,
            "text":             name,
            "font":             "bold 18pt Consolas",
            "style":            "FILL_AND_OUTLINE",
            "scale":            0.5,
            "fillColor":        {"rgba": point_color},
            "outlineColor":     {"rgba": [0, 0, 0, 255]},
            "outlineWidth":     2,
            "pixelOffset":      {"cartesian2": [5, -4]},
            "horizontalOrigin": "LEFT",
            "verticalOrigin":   "CENTER",
        },
    }

    if model_uri:
        packet["model"] = {
            "show":          True,
            "gltf":          [{"interval": f"{availability.split('/')[0]}/9999-12-31T23:59:59Z",
                               "uri": model_uri}],
            "scale":         1,
            "runAnimations": False,
        }
        # Hide the billboard when a 3-D model is present to avoid overlap.
        packet["billboard"]["show"] = False

    if orientation:
        packet["orientation"] = orientation

    if horizontal_disc:
        # Flat oblate ellipsoid: large equatorial radius, small polar radius.
        # Represents omnidirectional horizontal RF emission (RLOS pattern).
        packet["ellipsoid"] = {
            "radii": {"cartesian": [cone_radius_m, cone_radius_m, cone_length_m * 0.08]},
            "material": {"solidColor": {"color": {"rgba": cone_color_dim}}},
            "outline": True,
            "outlineColor": {"rgba": cone_outline},
        }
    else:
        # Downward-pointing nadir cone (downward cylinder) — default for radar.
        packet["cylinder"] = {
            "length":              cone_length_m,
            "topRadius":           0.0,
            "bottomRadius":        cone_radius_m,
            "material":            {"solidColor": {"color": {"rgba": cone_color_dim}}},
            "outline":             True,
            "outlineColor":        {"rgba": cone_outline},
            "numberOfVerticalLines": 8,
        }

    return packet


def build_synthetic_czml(
    exercise_id: str,
    start_time: str,
    stop_time: str,
) -> list[dict]:
    """
    Build a synthetic CZML document for two platforms over Kashmir.

    Scenario (Kashmir AoI — Himalayan hilly terrain):
      Emitter  — clockwise circle, centre (34.1°N, 74.8°E) near Srinagar
                 alt = 1 900 m  (valley floor + 300 m, well below surrounding ridges)
      Radar    — counterclockwise, centre (34.4°N, 75.6°E) over Zanskar foothills
                 alt = 5 000 m  (lowered to increase terrain occlusion frequency)

    LOS availability is computed via _los_availability(), which samples terrain
    height along the emitter→radar path using a Gaussian terrain model.  The
    emitter at 1 900 m is frequently occluded by the Pir Panjal and Great
    Himalayan ridges, producing ~73% blockage for realistic intermittent LOS loss.
    """
    start_iso = _stk_time_to_iso(start_time)
    stop_iso  = _stk_time_to_iso(stop_time)
    avail     = f"{start_iso}/{stop_iso}"

    start_dt  = datetime.strptime(start_iso, "%Y-%m-%dT%H:%M:%SZ")
    stop_dt   = datetime.strptime(stop_iso,  "%Y-%m-%dT%H:%M:%SZ")
    duration  = int((stop_dt - start_dt).total_seconds())
    STEP      = 30

    emitter_id = f"emitter/{exercise_id}"
    radar_id   = f"radar/{exercise_id}"

    emitter_pos = _circle_track(34.1, 74.8, 0.7, 1900.0, duration, STEP, clockwise=True,  orbits=1)
    radar_pos   = _circle_track(34.4, 75.6, 0.4, 4250.0, duration, STEP, clockwise=False, orbits=3)

    # ── Terrain-based LOS availability ──────────────────────────────────────
    def _samples(track: list[float]) -> list[tuple[float, float, float, float]]:
        return [(track[i], track[i+1], track[i+2], track[i+3])
                for i in range(0, len(track), 4)]

    los_avail = _los_availability(
        _samples(emitter_pos), _samples(radar_pos), start_dt, stop_iso
    )

    return [
        # ── Document ────────────────────────────────────────────────────
        {
            "id":      "document",
            "name":    f"Exercise {exercise_id} — Kashmir LOS Demo",
            "version": "1.0",
            "clock": {
                "interval":    avail,
                "currentTime": start_iso,
                "multiplier":  30,
                "step":        "SYSTEM_CLOCK_MULTIPLIER",
                "range":       "LOOP_STOP",
            },
        },

        # ── Emitter (orange) — low altitude, Kashmir Valley ──────────────
        _platform_packets(
            entity_id    = emitter_id,
            name         = "Emitter",
            availability = avail,
            epoch        = start_iso,
            positions    = emitter_pos,
            point_color  = [255, 120, 0, 255],
            path_color   = [255, 120, 0, 180],
            cone_color   = [255, 120, 0, 255],
            cone_radius_m  = 7000.0,
            cone_length_m  = 5000.0,
            horizontal_disc = True,
            trail_time   = 300,
        ),

        # ── Radar (cyan) — high altitude, Zanskar Range ──────────────────
        _platform_packets(
            entity_id    = radar_id,
            name         = "Radar",
            availability = avail,
            epoch        = start_iso,
            positions    = radar_pos,
            point_color  = [0, 200, 255, 255],
            path_color   = [0, 200, 255, 180],
            cone_color   = [0, 200, 255, 255],
            cone_radius_m  = 4000.0,
            cone_length_m  = 6000.0,
            trail_time   = 300,
        ),

        # ── LOS line — visible only during terrain-clear intervals ───────
        {
            "id":           f"los/{exercise_id}",
            "name":         "LOS",
            "availability": los_avail,
            "polyline": {
                "positions": {
                    "references": [
                        f"{emitter_id}#position",
                        f"{radar_id}#position",
                    ]
                },
                "width":    2,
                "material": {"solidColor": {"color": {"rgba": [86, 211, 100, 220]}}},
                "arcType":  "NONE",
            },
        },

        # ── RLOS line — same availability as LOS ─────────────────────────
        {
            "id":           f"rlos/{exercise_id}",
            "name":         "RLOS",
            "availability": los_avail,
            "polyline": {
                "positions": {
                    "references": [
                        f"{emitter_id}#position",
                        f"{radar_id}#position",
                    ]
                },
                "width":    5,
                "material": {
                    "polylineGlow": {
                        "color":      {"rgba": [255, 200, 0, 200]},
                        "glowPower":  0.3,
                        "taperPower": 1.0,
                    }
                },
                "arcType":  "NONE",
            },
        },
    ]
