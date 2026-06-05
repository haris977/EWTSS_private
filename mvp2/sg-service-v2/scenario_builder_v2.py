"""
Builds an STK scenario from PlanningResult entities via agi.stk13 COM.
Targets STK Engine 13.x / STK_ODTK 13.

agi.stk13 mirrors agi.stk12's COM API — PascalCase methods, same object
model, same enum prefixes (``AgESTKObjectType``, ``AgECvBoundsType``, etc.).
PySTK (``ansys.stk.core``) is NOT imported here; use it only on a
future migration once its API stabilises.
"""
from __future__ import annotations
import logging

log = logging.getLogger(__name__)


def _import_stk_types():
    """Centralise agi.stk13 enum imports so tests can patch this one function.

    Names verified against agi_stk13-13.1.0 (introspected from the installed
    wheel) — note these differ from agi.stk12:
      * ``AgECvBounds`` replaces ``AgECvBoundsType``.
      * ``AgESnPattern.eSnSimpleConic = 5``  (was 1 in stk12).
      * ``AgEFmDefinitionType.eFmSimpleCoverage = 11``  (renamed from
        ``eFmSimpleAccessCoverage`` = 1 in stk12).
    """
    from agi.stk13.stkobjects import (  # type: ignore[import]
        AgESTKObjectType,
        AgESnPattern,
        AgECvBounds,
        AgEFmDefinitionType,
    )

    class _T:
        pass

    t = _T()
    t.AgESTKObjectType    = AgESTKObjectType
    t.AgESnPattern        = AgESnPattern
    t.AgECvBounds         = AgECvBounds
    t.AgEFmDefinitionType = AgEFmDefinitionType
    return t


def _add_area_target(root, scenario, name: str, polygon_lonlat: list, t):
    at = scenario.Children.New(t.AgESTKObjectType.eAreaTarget, name)
    coords = " ".join(f"Lat {lat:.6f} Lon {lon:.6f}" for lon, lat in polygon_lonlat)
    root.ExecuteCommand(f"SetState */AreaTarget/{name} PatternOld {coords}")
    log.info("scenario_builder_v2: AreaTarget %s (%d vertices)", name, len(polygon_lonlat))
    return at


def _add_aircraft(scenario, name: str, waypoints_lonlat_altm: list, speed_ms: float,
                  start_time: str, stop_time: str, t):
    aircraft = scenario.Children.New(t.AgESTKObjectType.eAircraft, name)
    # STK 13's aircraft.Route is the GreatArc propagator directly — same as STK 12.
    prop = aircraft.Route
    prop.EphemerisInterval.SetExplicitInterval(start_time, stop_time)

    # STK's native distance unit is km and speed is km/s; convert from the
    # m/s / metres the frontend sends.
    speed_km_s = speed_ms / 1000.0

    for wp in waypoints_lonlat_altm:
        lon, lat, alt_m = wp[0], wp[1], wp[2] if len(wp) > 2 else 0.0
        w          = prop.Waypoints.Add()
        w.Latitude  = lat
        w.Longitude = lon
        w.Altitude  = alt_m / 1000.0   # km
        w.Speed     = speed_km_s

    prop.Propagate()
    log.info("scenario_builder_v2: aircraft %s (%d waypoints)", name, len(waypoints_lonlat_altm))
    return aircraft


def _add_facility(scenario, name: str, lat: float, lon: float, alt_m: float, t):
    facility = scenario.Children.New(t.AgESTKObjectType.eFacility, name)
    # AssignPlanetodetic uses STK's current distance unit (km by default).
    facility.Position.AssignPlanetodetic(lat, lon, alt_m / 1000.0)
    log.info("scenario_builder_v2: facility %s (%.4f, %.4f)", name, lat, lon)
    return facility


def _add_sensor(parent, name: str, half_angle_deg: float, t):
    sensor = parent.Children.New(t.AgESTKObjectType.eSensor, name)
    sensor.SetPatternType(t.AgESnPattern.eSnSimpleConic)
    sensor.CommonTasks.SetPatternSimpleConic(half_angle_deg, 1)
    log.info("scenario_builder_v2: sensor %s (half-angle %.1f°)", name, half_angle_deg)
    return sensor


def _add_coverage_fom(scenario, area_target_name: str, asset_objects: list, t):
    cov = scenario.Children.New(t.AgESTKObjectType.eCoverageDefinition,
                                f"Cov_{area_target_name}")
    cov.Grid.BoundsType = t.AgECvBounds.eBoundsCustomRegions
    cov.Grid.Bounds.AreaTargets.Add(f"AreaTarget/{area_target_name}")
    for obj in asset_objects:
        # Asset paths use class/name form (e.g. "Aircraft/AC1"); STK's AssetList
        # does not accept bare instance names.
        cov.AssetList.Add(f"{obj.ClassName}/{obj.InstanceName}")
    cov.ComputeAccesses()

    fom = cov.Children.New(t.AgESTKObjectType.eFigureOfMerit, f"FOM_{area_target_name}")
    fom.SetDefinitionType(t.AgEFmDefinitionType.eFmSimpleCoverage)
    # STK 13 refactored FOM compute: Compute() lives on IAgFmDefCompute, which
    # the Definition property implements. Older stk12 code called
    # fom.DataDefinition.Compute(); the new path is fom.Definition.Compute().
    # For simple coverage the values are also derived from the CoverageDefinition
    # access computation above, so Compute may be a no-op or raise — either way
    # DataProvider queries below still return usable data.
    definition = getattr(fom, 'Definition', None)
    compute = getattr(definition, 'Compute', None) if definition is not None else None
    if callable(compute):
        try:
            compute()
        except Exception as exc:
            log.debug("FigureOfMerit.Definition.Compute raised (continuing): %s", exc)
    log.info("scenario_builder_v2: CoverageDefinition + FOM created for %s", area_target_name)
    return cov, fom


def build_stk_scenario_v2(root, exercise_id, start_time, stop_time, entities):
    t = _import_stk_types()  # single entry point — passed to all helpers

    try:
        root.CloseScenario()
    except Exception:
        pass
    root.NewScenario(f"MVP2_{exercise_id.replace('-', '_')}")
    sc = root.CurrentScenario
    sc.StartTime = start_time
    sc.StopTime  = stop_time

    stk_objects: dict[str, object] = {}

    for ent in entities:
        etype = ent["properties"]["entityType"]
        name  = ent["properties"]["name"]

        if etype == "AreaTarget":
            ring = ent["geometry"]["coordinates"][0]
            stk_objects[name] = _add_area_target(root, sc, name, ring, t)

        elif etype == "Aircraft":
            coords   = ent["geometry"]["coordinates"]
            speed_ms = ent["properties"].get("speedMs", 150.0)
            stk_objects[name] = _add_aircraft(sc, name, coords, speed_ms,
                                              start_time, stop_time, t)

        elif etype == "Facility":
            coords = ent["geometry"]["coordinates"]
            lon, lat = coords[0], coords[1]
            alt_m = coords[2] if len(coords) > 2 else 0.0
            stk_objects[name] = _add_facility(sc, name, lat, lon, alt_m, t)

        elif etype == "Sensor":
            parent_name = ent["properties"]["parentEntity"]
            half_angle  = ent["properties"].get("halfAngleDeg", 25.0)
            parent_obj  = stk_objects.get(parent_name)
            if parent_obj is None:
                raise ValueError(
                    f"Sensor '{name}': parent '{parent_name}' not yet created. "
                    "Reorder entities so parents appear before sensors."
                )
            stk_objects[name] = _add_sensor(parent_obj, name, half_angle, t)

    # Auto CoverageDefinition + FOM (only for the first AreaTarget).
    at_names      = [e["properties"]["name"] for e in entities
                     if e["properties"]["entityType"] == "AreaTarget"]
    aircraft_objs = [stk_objects[e["properties"]["name"]] for e in entities
                     if e["properties"]["entityType"] == "Aircraft"]
    if at_names and aircraft_objs:
        if len(at_names) > 1:
            log.warning("scenario_builder_v2: %d AreaTargets found; coverage built only for '%s'",
                        len(at_names), at_names[0])
        _add_coverage_fom(sc, at_names[0], aircraft_objs, t)

    # Access computation — all aircraft pairs.
    for i, a in enumerate(aircraft_objs):
        for b in aircraft_objs[i + 1:]:
            a.GetAccessToObject(b).ComputeAccess()
            log.info("scenario_builder_v2: access %s <-> %s",
                     a.InstanceName, b.InstanceName)
