using Sg.Domain.Models;

namespace Sg.Domain.Contracts;

public sealed record FigureOfMeritDto(
    string Name,
    string ParentPath,
    FomKind Kind,
    FomStatistic Statistic);
