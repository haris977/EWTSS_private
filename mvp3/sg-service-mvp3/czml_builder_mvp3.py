"""Build a CZML document from live STK DataProviders.

Replaces MVP1's ``ExportCZML`` Connect Command + ``_patch_czml_sensors`` chain.
Uses plain dict packets (czml3's Pydantic builders are planned as a follow-up
refactor; the primary goal of MVP3 is to eliminate the plugin dependency).

Public entry point: ``build_czml(root, scenario_name, start_time, stop_time,
model_base_url, step_seconds=10.0) -> str`` — returns a JSON document string
ready to write to disk.
"""
from __future__ import annotations
import json
import math
from typing import Any

from cone_geometry import cone_samples, nadir_quaternion_samples, aim_quaternion_samples


def build_czml(
    root,
    scenario_name: str,
    start_time: str,
    stop_time: str,
    model_base_url: str,
    step_seconds: float = 10.0,
) -> str:
    """Return a CZML JSON document for the current STK scenario."""
    packets: list[dict[str, Any]] = []

    # ---- 1. Document packet ----
    packets.append({
        "id": "document",
        "name": scenario_name,
        "version": "1.0",
        "clock": {
            "interval": f"{_iso(start_time)}/{_iso(stop_time)}",
            "currentTime": _iso(start_time),
            "multiplier": 60,
            "range": "LOOP_STOP",
            "step": "SYSTEM_CLOCK_MULTIPLIER",
        },
    })

    # ---- 2a. Collect aircraft positions + emit their own packets ----
    sc = root.CurrentScenario
    aircraft_positions: dict[str, list[float]] = {}
    for i in range(sc.Children.Count):
        obj = sc.Children.Item(i)
        if getattr(obj, "ClassName", "").lower() != "aircraft":
            continue
        positions = _read_cartesian_positions(obj, start_time, stop_time, step_seconds)
        aircraft_positions[obj.InstanceName] = positions
        packets.append(_aircraft_packet(obj.InstanceName, positions,
                                        start_time, stop_time, model_base_url))

    # ---- 2b. Sensor packets with explicit orientation targets ----
    # Pointing rules:
    #   RadarCone     → nadir (local +Z → earth's centre from RadarAC).
    #   EmissionCone  → aim at RadarAC (local +Z → RadarAC - EmitterAC unit vec).
    # Everything else falls back to a nadir pointing on its own parent.
    for i in range(sc.Children.Count):
        obj = sc.Children.Item(i)
        if getattr(obj, "ClassName", "").lower() != "aircraft":
            continue
        parent_name = obj.InstanceName
        positions   = aircraft_positions[parent_name]
        for j in range(obj.Children.Count):
            child = obj.Children.Item(j)
            if child is None or getattr(child, "ClassName", "").lower() != "sensor":
                continue
            sensor_name = child.InstanceName
            if sensor_name == "EmissionCone":
                target_positions = aircraft_positions.get("RadarAC", positions)
                quat_samples = aim_quaternion_samples(positions, target_positions)
                packets.extend(_sensor_gaussian_lobe_packets(
                    child, parent_name, positions, start_time, quat_samples))
            else:
                quat_samples = nadir_quaternion_samples(positions)
                packets.append(_sensor_cone_packet(
                    child, parent_name, positions, start_time, quat_samples))

    # ---- 3. Access pair packets ----
    # Pair every distinct aircraft with every other exactly once.
    aircraft_objs = [sc.Children.Item(i) for i in range(sc.Children.Count)
                     if getattr(sc.Children.Item(i), "ClassName", "").lower() == "aircraft"]
    for i, a_obj in enumerate(aircraft_objs):
        for b_obj in aircraft_objs[i + 1:]:
            access = a_obj.GetAccessToObject(b_obj)
            intervals = _read_access_intervals(access, start_time, stop_time)
            if intervals:
                packets.append(_access_polyline_packet(
                    a_obj.InstanceName, b_obj.InstanceName, intervals))

    return json.dumps(packets, indent=2)


# ---------------- DataProvider readers ----------------

def _read_cartesian_positions(aircraft, start_time, stop_time, step_seconds) -> list[float]:
    """Return ``[t0, x0, y0, z0, t1, x1, y1, z1, ...]`` in the CZML epoch-relative layout.

    STK's ``Cartesian Position`` on an Aircraft is a *grouped* data provider —
    ``DataProviders.GetItemByName("Cartesian Position")`` returns a group whose
    ``Group.Item("Fixed")`` is the runnable ECEF provider. If that grouped
    accessor raises (e.g. in the mock fixture where the name resolves directly
    to a runnable provider), fall back to the direct call.

    STK's default distance unit is kilometres; Cesium CZML positions are in
    metres. We convert with a heuristic: if the first sample's radial distance
    from the earth's centre is < 10,000 (i.e. clearly not metres from ECEF
    origin), multiply by 1000.
    """
    group = aircraft.DataProviders.GetItemByName("Cartesian Position")
    try:
        dp = group.Group.Item("Fixed")
    except Exception:
        dp = group
    result = dp.Exec(start_time, stop_time, step_seconds)
    times = list(result.DataSets.GetDataSetByName("Time").GetValues())
    xs    = list(result.DataSets.GetDataSetByName("x").GetValues())
    ys    = list(result.DataSets.GetDataSetByName("y").GetValues())
    zs    = list(result.DataSets.GetDataSetByName("z").GetValues())

    scale = 1.0
    if xs:
        r = math.sqrt(float(xs[0])**2 + float(ys[0])**2 + float(zs[0])**2)
        if r < 10_000:              # clearly not metres from the ECEF origin
            scale = 1000.0

    out: list[float] = []
    for t, x, y, z in zip(times, xs, ys, zs):
        t_rel = _seconds_from_start(t, start_time, step_seconds, len(out) // 4)
        out.extend([t_rel,
                    float(x) * scale,
                    float(y) * scale,
                    float(z) * scale])
    return out


def _read_access_intervals(access, start_time: str, stop_time: str) -> list[tuple[str, str]]:
    """Return ``[(start_iso, stop_iso), ...]`` for the access intervals.

    STK's ``Access Data`` DataProvider requires the scenario interval as
    ``Exec`` arguments to report computed intervals; omitting them returns an
    empty result set on live STK.
    """
    try:
        dp = access.DataProviders.GetItemByName("Access Data")
    except Exception:
        return []
    try:
        result = dp.Exec(start_time, stop_time)
    except Exception:
        # Fall back to a no-arg Exec for STK versions where the provider
        # returns cached intervals from the previous ComputeAccess call.
        try:
            result = dp.Exec()
        except Exception:
            return []
    try:
        starts = list(result.DataSets.GetDataSetByName("Start Time").GetValues())
        stops  = list(result.DataSets.GetDataSetByName("Stop Time").GetValues())
    except Exception:
        return []
    return [(_iso(s), _iso(e)) for s, e in zip(starts, stops)]


# ---------------- Packet builders ----------------

def _aircraft_packet(name, positions, start_time, stop_time, model_base_url):
    """Aircraft packet — point marker + label + trailing path.

    The frontend forces sane GL caps on the Cesium context after construction
    (see cesium-viewer.ts), so point outlines and label glyph atlases render
    correctly even on drivers that report degenerate aliasedLineWidthRange.
    """
    del model_base_url  # reserved for a future 'model' path when .glb is served
    return {
        "id": name,
        "name": name,
        "availability": f"{_iso(start_time)}/{_iso(stop_time)}",
        "position": {
            "epoch": _iso(start_time),
            "interpolationAlgorithm": "LAGRANGE",
            "interpolationDegree": 5,
            "referenceFrame": "FIXED",
            "cartesian": positions,
        },
        "point": {
            "pixelSize":    10,
            "color":        {"rgba": [255, 180, 0, 255]},
            "outlineColor": {"rgba": [0, 0, 0, 255]},
            "outlineWidth": 1,
        },
        "path": {
            "show":      True,
            "leadTime":  0,
            "trailTime": 300,
            "width":     2,
            "material":  {"solidColor": {"color": {"rgba": [255, 180, 0, 200]}}},
        },
        "label": {
            "text":         name,
            "font":         "bold 12pt sans-serif",
            "fillColor":    {"rgba": [255, 255, 255, 255]},
            "outlineColor": {"rgba": [0, 0, 0, 255]},
            "outlineWidth": 2,
            "pixelOffset":  {"cartesian2": [10, 0]},
        },
    }


def _sensor_cone_packet(sensor, parent_name, parent_positions, start_time, quat_samples):
    """Emit a native STK ``agi_conicSensor`` CZML packet.

    Consumed by Ion SDK's ``processConicSensor`` in the frontend.

    Position references the parent aircraft (so the sensor rides along).
    ``quat_samples`` is a time-dynamic quaternion list in CZML epoch-relative
    layout ``[t0, qx, qy, qz, qw, t1, qx, qy, qz, qw, ...]`` — we use it
    rather than a parent-orientation reference so we can aim the sensor
    independently of the aircraft's (uncomputed) heading.
    """
    half_angle_deg = 25.0
    pattern = getattr(sensor, "Pattern", None)
    for attr in ("ConeAngle", "OuterConeAngle", "ConeHalfAngle"):
        try:
            half_angle_deg = float(getattr(pattern, attr))
            break
        except (AttributeError, TypeError, ValueError):
            continue

    radius_m       = 20_000.0
    half_angle_rad = math.radians(half_angle_deg)

    _ = parent_positions
    return {
        "id":     f"{parent_name}/{sensor.InstanceName}",
        "name":   sensor.InstanceName,
        "parent": parent_name,
        "position":    {"reference": f"{parent_name}#position"},
        "orientation": {
            "epoch":                  _iso(start_time),
            "interpolationAlgorithm": "LINEAR",
            "unitQuaternion":         quat_samples,
        },
        "agi_conicSensor": {
            "show":             True,
            "radius":           radius_m,
            "innerHalfAngle":   0.0,
            "outerHalfAngle":   half_angle_rad,
            "minimumClockAngle": 0.0,
            "maximumClockAngle": math.tau,
            "portionToDisplay": "COMPLETE",
            "lateralSurfaceMaterial": {
                "solidColor": {"color": {"rgba": [0, 255, 255, 80]}},
            },
            "showLateralSurfaces":           True,
            "showEllipsoidHorizonSurfaces":  False,
            "showEllipsoidSurfaces":         False,
            "showDomeSurfaces":              False,
            "showIntersection":              True,
            "intersectionColor":             {"rgba": [0, 255, 255, 220]},
            "intersectionWidth":             1.0,
        },
    }


def _gaussian_beam_unit_spherical(
    sigma_azimuth_rad: float,
    sigma_elevation_rad: float,
    threshold_factor: float,
    n_points: int = 64,
) -> list[float]:
    """Flat ``[clock_0, cone_0, clock_1, cone_1, ...]`` array tracing an
    iso-gain contour of an elliptical 2-D Gaussian antenna pattern.

    Gain is modeled as ``G(θ_az, θ_el) = exp(-(θ_az²/(2σ_az²) + θ_el²/(2σ_el²)))``.
    For a given threshold ``t = G(θ)`` the contour is an ellipse whose
    semi-axes are ``σ · √(2·ln(1/t))``. ``threshold_factor`` is that ``t``
    (e.g. 0.5 for the −3 dB half-power contour, 0.01 for −20 dB).

    Returns ``unitSpherical`` pairs at ``n_points`` equally-spaced clock angles.
    """
    k = math.sqrt(2.0 * math.log(1.0 / threshold_factor))
    a = k * sigma_azimuth_rad     # major semi-axis in azimuth
    b = k * sigma_elevation_rad   # minor semi-axis in elevation
    out: list[float] = []
    for i in range(n_points):
        clock = math.tau * i / n_points
        cos_c = math.cos(clock)
        sin_c = math.sin(clock)
        cone  = (a * b) / math.sqrt((b * cos_c) ** 2 + (a * sin_c) ** 2)
        out.extend([clock, cone])
    return out


def _sensor_gaussian_lobe_packets(sensor, parent_name, parent_positions,
                                  start_time, quat_samples):
    """Emit a stack of nested ``agi_customPatternSensor`` packets representing
    successive iso-gain contours of a Gaussian antenna lobe.

    Visualisation: each shell is the 2-D Gaussian contour at a different
    power threshold (−3, −6, −10, −20 dB from peak). Colors grade from hot
    red-orange at the peak through orange to faint yellow at the −20 dB
    skirt — mimicking the classic antenna radiation pattern heatmap.
    Innermost shell is most opaque; outer shells are translucent so the
    main lobe dominates visually.

    ``quat_samples`` is the shared orientation array (time-dynamic) that
    aims local +Z at the target — all four contours share it so they
    remain co-axial.
    """
    del parent_positions  # referenced from the parent

    # Gaussian parameters (σ in radians). Asymmetric so the lobe reads as
    # directional: wider in azimuth, narrower in elevation.
    sigma_az = math.radians(25.0)
    sigma_el = math.radians(10.0)
    radius_m = 30_000.0

    # (db_label, threshold_factor = 10^(dB/10), rgba)
    #   -3  dB  ≈ 0.501  (half power)   — bright red-orange, opaque main lobe
    #   -6  dB  ≈ 0.251                 — orange
    #   -10 dB  ≈ 0.100                 — gold
    #   -20 dB  ≈ 0.010                 — pale yellow, ghostly outer halo
    contours = [
        ("-3dB",   0.501, [255,  40,   0, 140]),
        ("-6dB",   0.251, [255, 120,   0, 100]),
        ("-10dB",  0.100, [255, 180,  30,  70]),
        ("-20dB",  0.010, [255, 230, 120,  40]),
    ]

    packets: list[dict] = []
    # Draw from the outermost (largest, faintest) to innermost so the
    # opaque inner shell renders last and dominates the final pixel.
    for label, threshold, rgba in reversed(contours):
        directions = _gaussian_beam_unit_spherical(
            sigma_az, sigma_el, threshold_factor=threshold, n_points=64,
        )
        # The -3 dB peak also renders its ellipsoid-surface intersection —
        # that's the "beam footprint" on the ground: effectively a poor-
        # man's viewshed (no actual Ion Viewshed SDK is shipped in this
        # bundle; we piggy-back on the sensor primitive's own intersection
        # math).
        is_peak = label == "-3dB"
        ellipsoid_rgba = rgba[:3] + [130]   # more opaque than lateral surface
        packets.append({
            "id":     f"{parent_name}/{sensor.InstanceName}/{label}",
            "name":   f"{sensor.InstanceName} {label}",
            "parent": parent_name,
            "position":    {"reference": f"{parent_name}#position"},
            "orientation": {
                "epoch":                  _iso(start_time),
                "interpolationAlgorithm": "LINEAR",
                "unitQuaternion":         quat_samples,
            },
            "agi_customPatternSensor": {
                "show":             True,
                "radius":           radius_m,
                "directions":       {"unitSpherical": directions},
                "portionToDisplay": "COMPLETE",
                "lateralSurfaceMaterial": {"solidColor": {"color": {"rgba": rgba}}},
                "showLateralSurfaces":           True,
                "showEllipsoidHorizonSurfaces":  False,
                "showEllipsoidSurfaces":         is_peak,
                "ellipsoidSurfaceMaterial": {"solidColor": {"color": {"rgba": ellipsoid_rgba}}},
                "showDomeSurfaces":              False,
                "showIntersection":              is_peak,
                "intersectionColor":             {"rgba": rgba[:3] + [220]},
                "intersectionWidth":             1.0,
            },
        })
    return packets


def _sensor_cylinder_packet_deprecated(sensor, parent_name, parent_positions, start_time):
    """Legacy cylinder-with-quaternion packet builder — kept for reference.

    Replaced by ``_sensor_cone_packet`` which emits the STK-native
    ``agi_conicSensor`` shape. Ion SDK's sensor visualizers render it with
    proper ellipsoid-horizon and lateral-surface shading.
    """
    half_angle_deg = 25.0
    pattern = getattr(sensor, "Pattern", None)
    for attr in ("ConeAngle", "OuterConeAngle", "ConeHalfAngle"):
        try:
            half_angle_deg = float(getattr(pattern, attr))
            break
        except (AttributeError, TypeError, ValueError):
            continue

    length        = 20_000.0
    bottom_radius = length * math.tan(math.radians(half_angle_deg))

    cartesian, quaternion = cone_samples(parent_positions, length)
    return {
        "id":     sensor.InstanceName,
        "name":   sensor.InstanceName,
        "parent": parent_name,
        "position": {
            "epoch":                  _iso(start_time),
            "interpolationAlgorithm": "LAGRANGE",
            "interpolationDegree":    1,
            "referenceFrame":         "FIXED",
            "cartesian":              cartesian,
        },
        "orientation": {
            "epoch":                  _iso(start_time),
            "interpolationAlgorithm": "LINEAR",
            "unitQuaternion":         quaternion,
        },
        "cylinder": {
            "length":       length,
            "topRadius":    0.0,
            "bottomRadius": bottom_radius,
            "material":     {"solidColor": {"color": {"rgba": [0, 255, 255, 80]}}},
            "outline":      True,
            "outlineColor": {"rgba": [0, 255, 255, 220]},
        },
    }


def _access_polyline_packet(a_name, b_name, intervals):
    availability = [f"{start}/{stop}" for start, stop in intervals]
    return {
        "id":   f"Access/{a_name}-{b_name}",
        "name": f"Access {a_name} <-> {b_name}",
        "availability": availability if len(availability) > 1 else availability[0],
        "polyline": {
            "positions": {
                "references": [f"{a_name}#position", f"{b_name}#position"],
            },
            "width":       2,
            "material":    {"solidColor": {"color": {"rgba": [0, 255, 0, 220]}}},
            "arcType":     "NONE",
            "followSurface": False,
        },
    }


# ---------------- Utilities ----------------

def _iso(t: Any) -> str:
    """Convert STK-format time strings (``"1 Jan 2025 00:00:00.000"``) to
    CZML-compatible ``"YYYY-MM-DDTHH:MM:SSZ"``. Pass through if already ISO.

    STK can emit sub-microsecond precision (up to 9 fractional digits). Python's
    ``%f`` directive accepts only 1–6 digits, so we truncate before parsing.
    """
    s = str(t).strip()
    if s.endswith("Z") or ("T" in s and "-" in s[:10]):
        return s
    # Truncate over-long fractional seconds (STK emits up to 9 digits).
    if "." in s:
        head, _, tail = s.rpartition(".")
        tail = tail[:6]  # drop nanoseconds / sub-microsecond
        s = f"{head}.{tail}" if tail else head
    from datetime import datetime
    for fmt in ("%d %b %Y %H:%M:%S.%f", "%d %b %Y %H:%M:%S"):
        try:
            d = datetime.strptime(s, fmt)
            return d.strftime("%Y-%m-%dT%H:%M:%SZ")
        except ValueError:
            continue
    return s


def _seconds_from_start(t, start_time, step_seconds, sample_index):
    """Return seconds elapsed from start_time for a DataProvider 'Time' sample.

    Accepts either a numeric value (already elapsed seconds) or a date string
    in STK or ISO format. Parses strings by subtracting from start_time. Only
    as a last resort does it fall back to uniform sample_index × step_seconds,
    which would distort non-uniform DataProvider output.
    """
    try:
        return float(t)
    except (TypeError, ValueError):
        pass
    from datetime import datetime
    parsed = _parse_time(str(t))
    base   = _parse_time(str(start_time))
    if parsed is not None and base is not None:
        return (parsed - base).total_seconds()
    return sample_index * step_seconds


def _parse_time(s: str):
    """Parse STK-format or ISO time string to a naive datetime, or None."""
    from datetime import datetime
    s = s.strip()
    fmts = (
        "%d %b %Y %H:%M:%S.%f",
        "%d %b %Y %H:%M:%S",
        "%Y-%m-%dT%H:%M:%S.%fZ",
        "%Y-%m-%dT%H:%M:%SZ",
        "%Y-%m-%dT%H:%M:%S",
    )
    for fmt in fmts:
        try:
            return datetime.strptime(s, fmt)
        except ValueError:
            continue
    return None
