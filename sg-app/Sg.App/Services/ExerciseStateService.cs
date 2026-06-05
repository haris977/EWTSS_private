using Sg.App.Models;

namespace Sg.App.Services;

public sealed class ExerciseStateService : IExerciseStateService
{
    private readonly object _gate = new();
    private ExerciseState _state = ExerciseState.Stopped;

    public ExerciseState CurrentState
    {
        get { lock (_gate) { return _state; } }
    }

    public event EventHandler<ExerciseState>? StateChanged;

    public void Transition(ExerciseState newState)
    {
        ExerciseState old;
        lock (_gate)
        {
            if (_state == newState) return;
            old = _state;
            _state = newState;
        }
        StateChanged?.Invoke(this, newState);
    }
}
