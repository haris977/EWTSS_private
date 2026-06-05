using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using Sg.App.Models;
using Sg.App.Services;

namespace Sg.App.ViewModels;

/// <summary>
/// Top-level VM for the production v2 Sg.App.
///
/// Wires exercise-control commands against <see cref="IExerciseStateService"/>
/// and surfaces the banner state from <see cref="SyncBannerService"/>.
/// B1.3 Phase 5 Tasks 17 and 19 extend this with time-sync gating and
/// SYNC_LOST auto-pause respectively.
/// </summary>
public sealed partial class MainWindowViewModel : ObservableObject
{
    private readonly IExerciseStateService _exercise;
    private readonly ITimeSyncClient? _timeSync;

    private string _lastKnownStatus = "warming";

    public SyncBannerService Banner { get; }

    [ObservableProperty]
    private ExerciseState _currentState = ExerciseState.Stopped;

    [ObservableProperty]
    private string? _pauseReason;

    public IRelayCommand StartExerciseCommand { get; }
    public IRelayCommand PauseExerciseCommand { get; }
    public IRelayCommand ResumeExerciseCommand { get; }
    public IRelayCommand StopExerciseCommand { get; }

    /// <param name="exercise">Exercise state machine.</param>
    /// <param name="banner">Banner service for sync-status display.</param>
    /// <param name="timeSync">
    ///   Time-sync HTTP client. Pass <c>null</c> in unit tests that do not
    ///   exercise the polling loop — <see cref="StartPolling"/> will be a
    ///   no-op when this is null.
    /// </param>
    public MainWindowViewModel(
        IExerciseStateService exercise,
        SyncBannerService banner,
        ITimeSyncClient? timeSync = null)
    {
        _exercise  = exercise;
        Banner     = banner;
        _timeSync  = timeSync;

        CurrentState = _exercise.CurrentState;
        _exercise.StateChanged += (_, s) =>
        {
            CurrentState = s;
            RaiseCommandsCanExecuteChanged();
        };

        StartExerciseCommand  = new RelayCommand(StartExercise,  CanStartExercise);
        PauseExerciseCommand  = new RelayCommand(PauseExercise,  () => CurrentState == ExerciseState.Running);
        ResumeExerciseCommand = new RelayCommand(ResumeExercise, () => CurrentState == ExerciseState.Paused);
        StopExerciseCommand   = new RelayCommand(StopExercise,   () => CurrentState != ExerciseState.Stopped);
    }

    private void StartExercise()  => _exercise.Transition(ExerciseState.Running);
    private void PauseExercise()  => _exercise.Transition(ExerciseState.Paused);
    private void ResumeExercise()
    {
        PauseReason = null;
        _exercise.Transition(ExerciseState.Running);
    }

    private void StopExercise()
    {
        PauseReason = null;
        _exercise.Transition(ExerciseState.Stopped);
    }

    /// <summary>
    /// Gating predicate for Start. Requires both <c>Stopped</c> state and a
    /// <c>healthy</c> time-sync status (B1.3 Task 17).
    /// </summary>
    private bool CanStartExercise() =>
        CurrentState == ExerciseState.Stopped && _lastKnownStatus == "healthy";

    /// <summary>
    /// Applies a /time/status status string: updates the internal gate field,
    /// refreshes the banner, and re-evaluates all command availability.
    /// This is the primary seam used by tests and by the polling loop.
    /// </summary>
    public void ApplyTimeSyncStatus(string status)
    {
        _lastKnownStatus = status;
        Banner.UpdateStatus(status);
        if (status == "sync_lost" && CurrentState == ExerciseState.Running)
        {
            PauseReason = "Time sync lost";
            _exercise.Transition(ExerciseState.Paused);
        }
        RaiseCommandsCanExecuteChanged();
    }

    /// <summary>
    /// Polls <see cref="ITimeSyncClient.GetStatusAsync"/> on the given interval
    /// and feeds the result into <see cref="ApplyTimeSyncStatus"/>.
    /// On any exception, applies <c>"sync_lost"</c> so the banner and exercise
    /// gate both reflect the network failure.
    /// Exits cleanly when <paramref name="ct"/> is cancelled.
    /// Do not call from the constructor — keep construction synchronous.
    /// Task 18 starts this from the composition root.
    /// </summary>
    public async Task StartPolling(TimeSpan interval, CancellationToken ct)
    {
        if (_timeSync is null)
            return;

        while (!ct.IsCancellationRequested)
        {
            try
            {
                var dto = await _timeSync.GetStatusAsync(ct);
                ApplyTimeSyncStatus(dto.Status);
            }
            catch (OperationCanceledException) when (ct.IsCancellationRequested)
            {
                break;
            }
            catch
            {
                ApplyTimeSyncStatus("sync_lost");
            }

            try
            {
                await Task.Delay(interval, ct);
            }
            catch (OperationCanceledException)
            {
                break;
            }
        }
    }

    private void RaiseCommandsCanExecuteChanged()
    {
        StartExerciseCommand.NotifyCanExecuteChanged();
        PauseExerciseCommand.NotifyCanExecuteChanged();
        ResumeExerciseCommand.NotifyCanExecuteChanged();
        StopExerciseCommand.NotifyCanExecuteChanged();
    }
}
