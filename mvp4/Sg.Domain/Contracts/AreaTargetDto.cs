namespace Sg.Domain.Contracts;

public sealed record AreaTargetDto(
    string Name,
    string OutlineColorHex,
    bool FillEnabled,
    string FillColorHex,
    IReadOnlyList<VertexDto> Vertices);

public sealed record VertexDto(double LatitudeDeg, double LongitudeDeg);
