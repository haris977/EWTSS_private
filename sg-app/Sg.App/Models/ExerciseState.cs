namespace Sg.App.Models;

/// <summary>
/// Lifecycle state of an exercise (a SG-DRS run).
/// Per the v2 design: an exercise is what the operator starts after time-sync
/// is healthy; the state machine is observed by the banner UI, persistence
/// audit log, and the SYNC_LOST auto-pause flow.
/// </summary>
public enum ExerciseState
{
    /// <summary>No exercise loaded or active.</summary>
    Stopped,
    /// <summary>Exercise armed but not yet running; participants joining.</summary>
    Armed,
    /// <summary>Exercise running.</summary>
    Running,
    /// <summary>Exercise paused (auto- or operator-initiated).</summary>
    Paused,
}
