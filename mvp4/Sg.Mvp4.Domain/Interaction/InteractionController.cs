using Sg.Mvp4.Domain.Contracts;
using Sg.Mvp4.Domain.Models;

namespace Sg.Mvp4.Domain.Interaction;

public sealed class InteractionController : IInteractionController
{
    private readonly IScenarioBackend _stk;

    private readonly List<(double lat, double lon, double alt)> _pendingPoints = new();

    public InteractionMode Mode { get; private set; } = InteractionMode.Idle;
    public string? EditingPath { get; private set; }

    public string StatusHint => Mode switch
    {
        InteractionMode.Idle              => "Ready.",
        InteractionMode.PlacingFacility   => "Click on the map to place a facility. Esc to cancel.",
        InteractionMode.PlacingAircraft   => "Left-click to add waypoints, press Enter to finish. Esc to cancel.",
        InteractionMode.PlacingAreaTarget => "Left-click to add polygon vertices, press Enter to close. Esc to cancel.",
        InteractionMode.EditingEntity     => $"Editing {EditingPath} — drag handles, Enter to finish, Esc to cancel.",
        _ => ""
    };

    public event EventHandler? ModeChanged;

    public InteractionController(IScenarioBackend stk) { _stk = stk; }

    public void BeginPlace(EntityKind kind)
    {
        Cancel();
        Mode = kind switch
        {
            EntityKind.Facility           => InteractionMode.PlacingFacility,
            EntityKind.Aircraft           => InteractionMode.PlacingAircraft,
            EntityKind.AreaTarget         => InteractionMode.PlacingAreaTarget,
            _ => throw new ArgumentException($"Placement not supported for {kind}.", nameof(kind))
        };
        RaiseModeChanged();
    }

    public void OnMapMouseDown(double lat, double lon, double alt, bool isValidPick)
    {
        if (!isValidPick) return;
        switch (Mode)
        {
            case InteractionMode.PlacingFacility:
                // Reset placement state BEFORE backend calls. AddEntity/UpdateXxx
                // pump the message loop (Application.DoEvents per perf pattern #2);
                // DoEvents can re-enter this controller via a queued MouseDown
                // and create a second phantom facility. Setting Mode=Idle first
                // turns the reentrant call into a no-op.
                var fName = UniqueName("Facility");
                var fDto  = new FacilityDto(fName, "#FF00FF", lat, lon, Math.Max(0, alt));
                ReturnToIdle();
                _stk.AddEntity(EntityKind.Facility, fName, null);
                _stk.UpdateFacility(fName, fDto);
                break;

            case InteractionMode.PlacingAircraft:
            case InteractionMode.PlacingAreaTarget:
                _pendingPoints.Add((lat, lon, alt));
                break;
        }
    }

    public void OnMapMouseMove(double lat, double lon, double alt, bool isValidPick)
    {
        // No state change on move; StkDisplayHost uses the latest coords to update preview primitives.
    }

    public void OnMapMouseDblClick(double lat, double lon, double alt, bool isValidPick)
    {
        // WinForms/ActiveX double-click sequence is MouseDown, MouseUp, MouseDown,
        // MouseUp, DblClick. Both MouseDowns already appended their copies of the
        // DC location to _pendingPoints. Pop those duplicates before the DC point
        // goes in, otherwise the route ends with 2-3 coincident waypoints (zero-
        // length final leg → degenerate propagation in STK).
        TrimTrailingDuplicates(lat, lon, isValidPick);

        switch (Mode)
        {
            case InteractionMode.PlacingAircraft when _pendingPoints.Count > 0:
                if (isValidPick) _pendingPoints.Add((lat, lon, alt));
                if (_pendingPoints.Count >= 2) FinalizeAircraft();
                break;
            case InteractionMode.PlacingAreaTarget when _pendingPoints.Count >= 2:
                if (isValidPick) _pendingPoints.Add((lat, lon, alt));
                if (_pendingPoints.Count >= 3) FinalizeAreaTarget();
                break;
        }
    }

    private void TrimTrailingDuplicates(double lat, double lon, bool isValidPick)
    {
        if (!isValidPick) return;
        // Strict equality: PickInfo at the same screen pixel is deterministic, so the
        // duplicate MouseDowns produced during a DC return bit-identical lat/lon.
        // A legitimate prior click at a slightly different position will not match.
        while (_pendingPoints.Count > 0
               && _pendingPoints[^1].lat == lat
               && _pendingPoints[^1].lon == lon)
            _pendingPoints.RemoveAt(_pendingPoints.Count - 1);
    }

    private void FinalizeAircraft()
    {
        // Snapshot the DTO and reset placement state BEFORE any backend call.
        // _stk.AddEntity / UpdateAircraft both pump the message loop internally
        // (Application.DoEvents — perf pattern #2). DoEvents can re-enter this
        // controller via queued MouseDown/DblClick events while finalize is
        // mid-flight: a reentrant DblClick (Mode still PlacingAircraft, pending
        // still populated) recurses into FinalizeAircraft and clears pending —
        // by the time the outer call constructs its DTO, pending is empty and
        // the outer aircraft ends up with no waypoints. Each reentry also
        // creates a phantom Aircraft2/3/... entity. Setting Mode=Idle first
        // turns reentrant events into no-ops.
        var name = UniqueName("Aircraft");
        var dto  = new AircraftDto(name, "#00FFFF", true,
            _pendingPoints.Select(p => new WaypointDto(p.lat, p.lon, Math.Max(1500, p.alt), 100)).ToList());
        ReturnToIdle();
        _stk.AddEntity(EntityKind.Aircraft, name, null);
        _stk.UpdateAircraft(name, dto);
    }

    private void FinalizeAreaTarget()
    {
        // Same reentrancy guard as FinalizeAircraft above.
        var name = UniqueName("AreaTarget");
        var dto  = new AreaTargetDto(name, "#FF0000", false, "#FF0000",
            _pendingPoints.Select(p => new VertexDto(p.lat, p.lon)).ToList());
        ReturnToIdle();
        _stk.AddEntity(EntityKind.AreaTarget, name, null);
        _stk.UpdateAreaTarget(name, dto);
    }

    public void FinalizePlacement()
    {
        switch (Mode)
        {
            case InteractionMode.PlacingAircraft   when _pendingPoints.Count >= 2: FinalizeAircraft();   break;
            case InteractionMode.PlacingAreaTarget when _pendingPoints.Count >= 3: FinalizeAreaTarget(); break;
            // Otherwise no-op: not in a placement mode, or below minimum point count.
        }
    }

    public void StartEditingOnMap(string entityPath)
    {
        if (Mode != InteractionMode.Idle) Cancel();
        EditingPath = entityPath;
        Mode = InteractionMode.EditingEntity;
        RaiseModeChanged();
    }

    public void ApplyEdit()
    {
        if (Mode != InteractionMode.EditingEntity) return;
        EditingPath = null;
        ReturnToIdle();
    }

    public void CancelEdit()
    {
        if (Mode != InteractionMode.EditingEntity) return;
        EditingPath = null;
        ReturnToIdle();
    }

    public void Cancel()
    {
        if (Mode == InteractionMode.Idle) return;
        // Pending placement entities haven't been added to STK yet (we only add on finalize),
        // so there's nothing to remove here.
        _pendingPoints.Clear();
        EditingPath = null;
        ReturnToIdle();
    }

    private void ReturnToIdle()
    {
        _pendingPoints.Clear();
        Mode = InteractionMode.Idle;
        RaiseModeChanged();
    }

    private void RaiseModeChanged() => ModeChanged?.Invoke(this, EventArgs.Empty);

    private string UniqueName(string baseName)
    {
        var taken = new HashSet<string>(_stk.Children.Select(c => c.Name));
        for (var i = 1; i <= 10_000; i++)
        {
            var candidate = $"{baseName}{i}";
            if (!taken.Contains(candidate)) return candidate;
        }
        throw new InvalidOperationException("Could not generate unique name.");
    }
}
