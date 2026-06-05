namespace Sg.Domain.Contracts;

public sealed record AircraftDto(
    string Name,
    string ColorHex,
    bool PathVisible,
    IReadOnlyList<WaypointDto> Waypoints);

public sealed record WaypointDto(
    double LatitudeDeg,
    double LongitudeDeg,
    double AltitudeMeters,
    double SpeedMps);
