namespace Sg.Domain.Contracts;

public sealed record ComputeResultDto(
    bool Success,
    string? Message,
    DateTime ComputedAtUtc);
