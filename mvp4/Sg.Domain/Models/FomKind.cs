namespace Sg.Domain.Models;

/// <summary>
/// Subset of FOM definition types exposed in the MVP panel.
/// Maps to <c>AgEFmDefinitionType</c> in the STK 12 COM API.
/// </summary>
public enum FomKind
{
    /// <summary>Maps to <c>AgEFmDefinitionType.eFmAccessDuration</c>.</summary>
    AccessDuration,

    /// <summary>Maps to <c>AgEFmDefinitionType.eFmRevisitTime</c>.</summary>
    RevisitTime,

    /// <summary>Maps to <c>AgEFmDefinitionType.eFmCoverageTime</c>.</summary>
    Coverage,

    /// <summary>Maps to <c>AgEFmDefinitionType.eFmNumberOfAccesses</c>.</summary>
    SampleCount,
}

/// <summary>
/// Statistic computed across grid points for a FOM definition that implements
/// <c>IAgFmDefCompute</c>. Maps to <c>AgEFmCompute</c> in the STK 12 COM API.
/// </summary>
public enum FomStatistic
{
    /// <summary>Maps to <c>AgEFmCompute.eMaximum</c>.</summary>
    Maximum,

    /// <summary>Maps to <c>AgEFmCompute.eMinimum</c>.</summary>
    Minimum,

    /// <summary>Maps to <c>AgEFmCompute.eAverage</c>.</summary>
    Average,

    /// <summary>Maps to <c>AgEFmCompute.eTotal</c>.</summary>
    Total,
}

/// <summary>
/// Static helpers exposing enum values as arrays for XAML x:Static bindings.
/// </summary>
public static class FomKindValues
{
    public static readonly FomKind[] All =
        (FomKind[])System.Enum.GetValues(typeof(FomKind));
}

public static class FomStatisticValues
{
    public static readonly FomStatistic[] All =
        (FomStatistic[])System.Enum.GetValues(typeof(FomStatistic));
}
