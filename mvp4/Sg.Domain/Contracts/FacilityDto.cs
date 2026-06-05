namespace Sg.Domain.Contracts;

public sealed record FacilityDto(
    string Name,
    string ColorHex,
    double LatitudeDeg,
    double LongitudeDeg,
    double AltitudeMeters);
