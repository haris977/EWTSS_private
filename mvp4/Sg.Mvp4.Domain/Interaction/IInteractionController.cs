using Sg.Mvp4.Domain.Models;

namespace Sg.Mvp4.Domain.Interaction;

public interface IInteractionController
{
    InteractionMode Mode { get; }
    string?         EditingPath { get; }
    string          StatusHint { get; }

    event EventHandler? ModeChanged;

    void BeginPlace(EntityKind kind);
    void OnMapMouseDown (double latDeg, double lonDeg, double altM, bool isValidPick);
    void OnMapMouseMove (double latDeg, double lonDeg, double altM, bool isValidPick);
    void OnMapMouseDblClick(double latDeg, double lonDeg, double altM, bool isValidPick);
    /// <summary>
    /// Finalises an in-progress aircraft / area-target placement using the
    /// already-collected waypoints (no extra point added). Wired to Enter key.
    /// No-op outside placement modes or when point count is below threshold.
    /// </summary>
    void FinalizePlacement();
    void StartEditingOnMap(string entityPath);
    void ApplyEdit();
    void CancelEdit();
    void Cancel();
}
