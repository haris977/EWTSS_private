"""
Builds synthetic CZML from a PlanningResult entity list — no STK required.
Used by MockStkService for testing and development.
"""
from __future__ import annotations
import math
from datetime import datetime, timezone


def _parse_stk_time(s: str) -> datetime:
    s = s.strip()
    fmt = "%d %b %Y %H:%M:%S.%f" if "." in s else "%d %b %Y %H:%M:%S"
    return datetime.strptime(s, fmt).replace(tzinfo=timezone.utc)


def _geodetic_to_ecef(lon_deg: float, lat_deg: float, alt_m: float) -> tuple[float, float, float]:
    a  = 6_378_137.0
    e2 = 0.006_694_379_990_14
    lat = math.radians(lat_deg)
    lon = math.radians(lon_deg)
    N = a / math.sqrt(1 - e2 * math.sin(lat) ** 2)
    x = (N + alt_m) * math.cos(lat) * math.cos(lon)
    y = (N + alt_m) * math.cos(lat) * math.sin(lon)
    z = (N * (1 - e2) + alt_m) * math.sin(lat)
    return x, y, z


def build_synthetic_czml(
    exercise_id: str,
    start_time_str: str,
    stop_time_str: str,
    entities: list[dict],
) -> list[dict]:
    t0 = _parse_stk_time(start_time_str)
    t1 = _parse_stk_time(stop_time_str)
    iso0 = t0.isoformat().replace("+00:00", "Z")
    iso1 = t1.isoformat().replace("+00:00", "Z")
    interval = f"{iso0}/{iso1}"
    duration = (t1 - t0).total_seconds()

    packets: list[dict] = [{
        "id": "document", "name": f"MVP2 {exercise_id}", "version": "1.0",
        "clock": {"interval": interval, "currentTime": iso0,
                  "multiplier": 30, "range": "LOOP_STOP",
                  "step": "SYSTEM_CLOCK_MULTIPLIER"},
    }]

    for ent in entities:
        etype = ent["properties"]["entityType"]
        name  = ent["properties"]["name"]

        if etype == "AreaTarget":
            ring = ent["geometry"]["coordinates"][0]
            flat: list[float] = []
            for lon, lat in ring:
                flat += [math.radians(lon), math.radians(lat), 0.0]
            packets.append({
                "id": f"AreaTarget/{name}", "name": name,
                "polygon": {
                    "positions": {"cartographicRadians": flat},
                    "material": {"solidColor": {"color": {"rgba": [255, 165, 0, 80]}}},
                    "outline": True,
                    "outlineColor": {"rgba": [255, 165, 0, 255]},
                    "height": 0.0,
                },
            })

        elif etype == "Aircraft":
            coords = ent["geometry"]["coordinates"]   # [[lon, lat, alt_m], ...]
            n = len(coords)
            cartesian: list[float] = []
            if n == 1:
                lon, lat = coords[0][0], coords[0][1]
                alt_m = coords[0][2] if len(coords[0]) > 2 else 0.0
                x, y, z = _geodetic_to_ecef(lon, lat, alt_m)
                cartesian = [0.0, x, y, z, duration, x, y, z]
            else:
                for i, wp in enumerate(coords):
                    lon, lat, alt_m = wp[0], wp[1], wp[2] if len(wp) > 2 else 0.0
                    t_s = duration * i / (n - 1)
                    x, y, z = _geodetic_to_ecef(lon, lat, alt_m)
                    cartesian += [t_s, x, y, z]
            packets.append({
                "id": f"Aircraft/{name}", "name": name,
                "availability": interval,
                "position": {
                    "epoch": iso0,
                    "interpolationAlgorithm": "LAGRANGE",
                    "interpolationDegree": 1,
                    "cartesian": cartesian,
                },
                "point": {"pixelSize": 8, "color": {"rgba": [0, 200, 255, 255]}},
                "label": {"text": name, "font": "bold 12pt sans-serif",
                          "fillColor": {"rgba": [255, 255, 255, 255]},
                          "style": "FILL_AND_OUTLINE",
                          "outlineColor": {"rgba": [0, 0, 0, 255]}, "outlineWidth": 2},
                "path": {"show": True, "leadTime": 0, "trailTime": 7200, "width": 2,
                         "material": {"solidColor": {"color": {"rgba": [0, 200, 255, 180]}}}},
            })

        elif etype == "Facility":
            coords = ent["geometry"]["coordinates"]
            lon, lat = coords[0], coords[1]
            alt_m = coords[2] if len(coords) > 2 else 0.0
            packets.append({
                "id": f"Facility/{name}", "name": name,
                "position": {"cartographicDegrees": [lon, lat, alt_m]},
                "point": {"pixelSize": 10, "color": {"rgba": [255, 255, 0, 255]}},
                "label": {"text": name, "font": "bold 11pt sans-serif",
                          "fillColor": {"rgba": [255, 255, 0, 255]},
                          "style": "FILL_AND_OUTLINE",
                          "outlineColor": {"rgba": [0, 0, 0, 255]}, "outlineWidth": 2,
                          "pixelOffset": {"cartesian2": [0, -16]}},
            })
        # Sensor: no independent position — skip in synthetic output

    return packets
