"""
Builds the MVP STK scenario via agi.stk12 COM.
Called exclusively by StkComService.build_and_compute().

Scenario content:
  EmitterAC  — Aircraft flying west→east at 4.5 km alt across Kashmir Valley, wide 45° emission cone
  RadarAC    — Aircraft flying east→west at 9.0 km alt over Zanskar/Himalayan Range, narrow 25° detection cone

Access computations (gives LOS / RLOS intervals in CZML export):
  EmitterAC/EmissionCone  → RadarAC     (emitter cone illuminates radar platform)
  RadarAC/RadarCone       → EmitterAC   (radar cone detects emitter platform)

STK's ExportDataTo CZML captures:
  - Entity trajectories (time-dynamic positions)
  - Sensor cone geometry (EmissionCone + RadarCone)
  - Access interval overlays with shaded timeline segments
"""
from __future__ import annotations

import logging

log = logging.getLogger(__name__)


def _add_aircraft_great_arc(
    sc,
    AgESTKObjectType,
    name: str,
    start_time: str,
    stop_time: str,
    wp_start: tuple[float, float, float],   # (lat, lon, alt_km)
    wp_stop:  tuple[float, float, float],
    speed_ms: float,
):
    """Create an aircraft with a two-waypoint great-arc route."""
    aircraft = sc.Children.New(AgESTKObjectType.eAircraft, name)
    # In STK 12, aircraft.Route returns AgVePropagatorGreatArc directly —
    # it is already the propagator; SetPropagatorType / .Propagator are not needed.
    prop = aircraft.Route
    prop.EphemerisInterval.SetExplicitInterval(start_time, stop_time)

    # STK GreatArc Waypoints.Speed uses km/s (STK's native distance unit per second).
    # Convert the caller's m/s value before assignment.
    speed_km_s = speed_ms / 1000.0

    wp0 = prop.Waypoints.Add()
    wp0.Latitude  = wp_start[0]
    wp0.Longitude = wp_start[1]
    wp0.Altitude  = wp_start[2]
    wp0.Speed     = speed_km_s

    wp1 = prop.Waypoints.Add()
    wp1.Latitude  = wp_stop[0]
    wp1.Longitude = wp_stop[1]
    wp1.Altitude  = wp_stop[2]
    wp1.Speed     = speed_km_s

    prop.Propagate()
    log.info("scenario_builder: added aircraft %s", name)
    return aircraft


def _add_conic_sensor(aircraft, AgESTKObjectType, name: str, half_angle_deg: float):
    """Attach a nadir-pointing SimpleConic sensor to an aircraft."""
    sensor = aircraft.Children.New(AgESTKObjectType.eSensor, name)
    sensor.SetPatternType(1)                              # 1 = eSensorPatternSimpleConic
    sensor.CommonTasks.SetPatternSimpleConic(half_angle_deg, 1)
    log.info("scenario_builder: added sensor %s (half-angle %.1f°)", name, half_angle_deg)
    return sensor


def build_stk_scenario(
    root,
    exercise_id: str,
    start_time: str,
    stop_time: str,
) -> None:
    """
    Create or replace an STK scenario with Emitter + Radar aircraft.

    Args:
        root:        agi.stk12 IAgStkObjectRoot — from StkComService._root
        exercise_id: used as the scenario name
        start_time:  STK-format string, e.g. "1 Jan 2025 00:00:00.000"
        stop_time:   STK-format string, e.g. "1 Jan 2025 01:00:00.000"
    """
    from agi.stk12.stkobjects import AgESTKObjectType  # type: ignore[import]

    scenario_name = f"MVP_{exercise_id.replace('-', '_')}"
    log.info("scenario_builder: creating scenario %s", scenario_name)

    # STK allows only one open scenario at a time — close any existing one first.
    try:
        root.CloseScenario()
        log.info("scenario_builder: closed existing scenario")
    except Exception:
        pass  # no scenario was open — ignore

    root.NewScenario(scenario_name)
    sc = root.CurrentScenario
    sc.StartTime = start_time
    sc.StopTime  = stop_time

    # ── Emitter aircraft ─────────────────────────────────────────────
    # Flies west→east across Kashmir Valley at 4.5 km (above valley floor ~1.6 km).
    # Route: Baramulla area → Pahalgam area — entirely within the valley.
    emitter = _add_aircraft_great_arc(
        sc, AgESTKObjectType,
        name       = "EmitterAC",
        start_time = start_time,
        stop_time  = stop_time,
        wp_start   = (34.2, 73.9, 1.9),   # (lat, lon, alt_km) — west of Baramulla, ~300 m above valley floor
        wp_stop    = (34.0, 75.4, 1.9),   # east towards Pahalgam
        speed_ms   = 150.0,               # 150 m/s = 0.15 km/s ≈ 540 km/h
    )

    emission_cone = _add_conic_sensor(
        emitter, AgESTKObjectType,
        name            = "EmissionCone",
        half_angle_deg  = 45.0,   # wide RF emission pattern
    )

    # ── Radar aircraft ───────────────────────────────────────────────
    # Flies east→west over Zanskar Range at 9 km — above the Himalayan peaks.
    # Counter-direction creates crossing geometry with the emitter.
    radar = _add_aircraft_great_arc(
        sc, AgESTKObjectType,
        name       = "RadarAC",
        start_time = start_time,
        stop_time  = stop_time,
        wp_start   = (34.6, 75.9, 4.675),   # east of radar orbit centre (34.4°N, 75.6°E), Zanskar foothills
        wp_stop    = (34.2, 75.2, 4.675),  # west — 4.675 km (−15% from 5.5 km)
        speed_ms   = 180.0,               # 180 m/s = 0.18 km/s ≈ 648 km/h
    )

    radar_cone = _add_conic_sensor(
        radar, AgESTKObjectType,
        name            = "RadarCone",
        half_angle_deg  = 25.0,   # focused detection pattern
    )

    # ── Access computations ──────────────────────────────────────────
    # Platform-to-platform geometric LOS (not sensor-to-platform).
    # Sensor-to-platform access requires the target to fall inside the
    # sensor's FOV; a nadir-pointing cone on EmitterAC (1.9 km) can never
    # illuminate RadarAC which sits above it (4.675 km).  Using platform
    # objects gives pure ellipsoid-based line-of-sight instead.
    #
    # NOTE: load a Kashmir DEM (Scenario → Basic → Terrain) to get
    # terrain-occlusion intervals rather than ellipsoid-only LOS.
    access_emit_to_radar = emitter.GetAccessToObject(radar)
    access_emit_to_radar.ComputeAccess()
    log.info("scenario_builder: computed EmitterAC → RadarAC access (LOS)")

    access_radar_to_emit = radar.GetAccessToObject(emitter)
    access_radar_to_emit.ComputeAccess()
    log.info("scenario_builder: computed RadarAC → EmitterAC access (RLOS)")
