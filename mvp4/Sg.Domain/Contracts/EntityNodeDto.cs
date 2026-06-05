using Sg.Domain.Models;

namespace Sg.Domain.Contracts;

public sealed record EntityNodeDto(
    EntityKind Kind,
    string Name,
    string Path,
    IReadOnlyList<EntityNodeDto> Children);
