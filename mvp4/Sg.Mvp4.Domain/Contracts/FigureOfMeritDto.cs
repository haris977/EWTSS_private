using Sg.Mvp4.Domain.Models;

namespace Sg.Mvp4.Domain.Contracts;

public sealed record FigureOfMeritDto(
    string Name,
    string ParentPath,
    FomKind Kind,
    FomStatistic Statistic);
