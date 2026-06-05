using Sg.Domain.Interaction;
using Sg.Domain.Models;

namespace Sg.Tests.Fakes;

/// <summary>
/// No-op fake of IInteractionController for use in unit tests that need a
/// MainWindowViewModel (or any other consumer) but do not exercise interaction logic.
/// </summary>
public sealed class FakeInteractionController : IInteractionController
{
    public InteractionMode Mode        { get; private set; } = InteractionMode.Idle;
    public string?         EditingPath { get; private set; }
    public string          StatusHint  { get; set; } = "Ready.";

    public event EventHandler? ModeChanged;

    public void BeginPlace(EntityKind kind)
    {
        Mode = kind switch
        {
            EntityKind.Facility   => InteractionMode.PlacingFacility,
            EntityKind.Aircraft   => InteractionMode.PlacingAircraft,
            EntityKind.AreaTarget => InteractionMode.PlacingAreaTarget,
            _ => InteractionMode.Idle
        };
        ModeChanged?.Invoke(this, EventArgs.Empty);
    }

    public void OnMapMouseDown(double latDeg, double lonDeg, double altM, bool isValidPick) { }
    public void OnMapMouseMove(double latDeg, double lonDeg, double altM, bool isValidPick) { }
    public void OnMapMouseDblClick(double latDeg, double lonDeg, double altM, bool isValidPick) { }
    public void FinalizePlacement()
    {
        Mode = InteractionMode.Idle;
        ModeChanged?.Invoke(this, EventArgs.Empty);
    }

    public void StartEditingOnMap(string entityPath)
    {
        EditingPath = entityPath;
        Mode        = InteractionMode.EditingEntity;
        ModeChanged?.Invoke(this, EventArgs.Empty);
    }

    public void ApplyEdit()
    {
        EditingPath = null;
        Mode        = InteractionMode.Idle;
        ModeChanged?.Invoke(this, EventArgs.Empty);
    }

    public void CancelEdit()
    {
        EditingPath = null;
        Mode        = InteractionMode.Idle;
        ModeChanged?.Invoke(this, EventArgs.Empty);
    }

    public void Cancel()
    {
        EditingPath = null;
        Mode        = InteractionMode.Idle;
        ModeChanged?.Invoke(this, EventArgs.Empty);
    }

    /// <summary>Fire ModeChanged externally for test purposes.</summary>
    public void RaiseModeChanged() => ModeChanged?.Invoke(this, EventArgs.Empty);
}
