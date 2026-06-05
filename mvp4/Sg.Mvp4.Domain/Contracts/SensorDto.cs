using Sg.Mvp4.Domain.Models;

namespace Sg.Mvp4.Domain.Contracts;

public sealed record SensorDto(
    string Name,
    string ParentPath,
    string ColorHex,
    bool ShowIntersection,
    double OuterHalfAngleDeg,
    double InnerHalfAngleDeg,
    PointingMode Pointing,
    double FixedAzimuthDeg,
    double FixedElevationDeg,
    string? TargetPath);
