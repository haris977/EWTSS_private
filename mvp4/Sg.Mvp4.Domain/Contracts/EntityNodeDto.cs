using Sg.Mvp4.Domain.Models;

namespace Sg.Mvp4.Domain.Contracts;

public sealed record EntityNodeDto(
    EntityKind Kind,
    string Name,
    string Path,
    IReadOnlyList<EntityNodeDto> Children);
