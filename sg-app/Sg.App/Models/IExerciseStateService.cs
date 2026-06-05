namespace Sg.App.Models;

/// <summary>
/// Tracks the current exercise lifecycle state. Wraps the underlying state
/// (currently in-process; eventually backed by drs-server's persistence).
/// Banner / VM consumers should observe state changes via <see cref="StateChanged"/>.
/// </summary>
public interface IExerciseStateService
{
    ExerciseState CurrentState { get; }
    event EventHandler<ExerciseState>? StateChanged;
    void Transition(ExerciseState newState);
}
