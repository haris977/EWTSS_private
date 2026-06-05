namespace Sg.Domain.Contracts;

public sealed record CoverageDefinitionDto(
    string Name,
    double LatMin,
    double LatMax,
    double LonMin,
    double LonMax,
    double LatStep,
    double LonStep,
    double MinAltitude,
    double MinElevationAngle,
    IReadOnlyList<string> AssetPaths);
