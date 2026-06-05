using FluentAssertions;
using Sg.App.Services;
using Sg.App.Models;
using Sg.App.ViewModels;

namespace Sg.App.Tests.ViewModels;

[TestFixture]
public class MainWindowViewModelTests
{
    private static MainWindowViewModel BuildVm()
    {
        var exercise = new ExerciseStateService();
        var banner   = new SyncBannerService();
        // ITimeSyncClient is null — no test in this file exercises the polling loop
        return new MainWindowViewModel(exercise, banner, null);
    }

    // -----------------------------------------------------------------------
    // Test 1: Start is disabled by default (status not healthy)
    // -----------------------------------------------------------------------
    [Test, Category(TestCategories.Unit)]
    public void StartExercise_disabled_when_status_is_not_healthy()
    {
        var vm = BuildVm();

        vm.StartExerciseCommand.CanExecute(null).Should().BeFalse(
            "initial _lastKnownStatus is 'warming', which is not 'healthy'");
    }

    // -----------------------------------------------------------------------
    // Test 2: Start enabled after ApplyTimeSyncStatus("healthy")
    // -----------------------------------------------------------------------
    [Test, Category(TestCategories.Unit)]
    public void StartExercise_enabled_after_apply_healthy()
    {
        var vm = BuildVm();

        vm.ApplyTimeSyncStatus("healthy");

        vm.StartExerciseCommand.CanExecute(null).Should().BeTrue(
            "status is now healthy and state is Stopped");
    }

    // -----------------------------------------------------------------------
    // Test 3: Start disabled once exercise is Running, even if status healthy
    // -----------------------------------------------------------------------
    [Test, Category(TestCategories.Unit)]
    public void StartExercise_disabled_when_state_already_running()
    {
        var exercise = new ExerciseStateService();
        var banner   = new SyncBannerService();
        var vm       = new MainWindowViewModel(exercise, banner, null);

        vm.ApplyTimeSyncStatus("healthy");
        exercise.Transition(ExerciseState.Running); // drives StateChanged → VM updates

        vm.StartExerciseCommand.CanExecute(null).Should().BeFalse(
            "exercise is Running so CanStartExercise must be false regardless of sync status");
    }

    // -----------------------------------------------------------------------
    // Test 4: Banner level updated by ApplyTimeSyncStatus
    // -----------------------------------------------------------------------
    [Test, Category(TestCategories.Unit)]
    public void Apply_time_sync_status_updates_banner()
    {
        var vm = BuildVm();

        vm.ApplyTimeSyncStatus("drift_warn");

        vm.Banner.CurrentBanner.Should().Be(BannerLevel.Warn,
            "drift_warn maps to BannerLevel.Warn");
    }

    // -----------------------------------------------------------------------
    // Test 5: sync_lost disables start and sets banner to Lost
    // -----------------------------------------------------------------------
    [Test, Category(TestCategories.Unit)]
    public void Apply_sync_lost_disables_start_and_sets_banner_lost()
    {
        var vm = BuildVm();
        vm.ApplyTimeSyncStatus("healthy"); // enable first so we can see it go back false
        vm.StartExerciseCommand.CanExecute(null).Should().BeTrue("precondition");

        vm.ApplyTimeSyncStatus("sync_lost");

        vm.StartExerciseCommand.CanExecute(null).Should().BeFalse(
            "sync_lost status must gate Start");
        vm.Banner.CurrentBanner.Should().Be(BannerLevel.Lost,
            "sync_lost maps to BannerLevel.Lost");
    }

    // -----------------------------------------------------------------------
    // Test 6: sync_lost during running exercise auto-pauses (Task 19)
    // -----------------------------------------------------------------------
    [Test, Category(TestCategories.Unit)]
    public void Sync_lost_during_running_exercise_auto_pauses()
    {
        var exercise = new ExerciseStateService();
        var vm = new MainWindowViewModel(exercise, new SyncBannerService());

        vm.ApplyTimeSyncStatus("healthy");
        vm.StartExerciseCommand.Execute(null);
        vm.CurrentState.Should().Be(ExerciseState.Running);

        vm.ApplyTimeSyncStatus("sync_lost");

        vm.CurrentState.Should().Be(ExerciseState.Paused);
        vm.PauseReason.Should().Be("Time sync lost");
        vm.Banner.CurrentBanner.Should().Be(BannerLevel.Lost);
    }

    // -----------------------------------------------------------------------
    // Test 7: sync_lost when not running does not change state (Task 19)
    // -----------------------------------------------------------------------
    [Test, Category(TestCategories.Unit)]
    public void Sync_lost_when_not_running_does_not_change_state()
    {
        var exercise = new ExerciseStateService();
        var vm = new MainWindowViewModel(exercise, new SyncBannerService());

        // Stays in Stopped (no exercise running).
        vm.ApplyTimeSyncStatus("sync_lost");

        vm.CurrentState.Should().Be(ExerciseState.Stopped);
        vm.PauseReason.Should().BeNull();
    }

    // -----------------------------------------------------------------------
    // Test 8: drift_warn does not pause running exercise (Task 19)
    // -----------------------------------------------------------------------
    [Test, Category(TestCategories.Unit)]
    public void Drift_warn_does_not_pause_running_exercise()
    {
        var exercise = new ExerciseStateService();
        var vm = new MainWindowViewModel(exercise, new SyncBannerService());

        vm.ApplyTimeSyncStatus("healthy");
        vm.StartExerciseCommand.Execute(null);
        vm.ApplyTimeSyncStatus("drift_warn");

        vm.CurrentState.Should().Be(ExerciseState.Running);
        vm.PauseReason.Should().BeNull();
    }

    // -----------------------------------------------------------------------
    // Test 9: Resume clears PauseReason (Task 19)
    // -----------------------------------------------------------------------
    [Test, Category(TestCategories.Unit)]
    public void Resume_clears_pause_reason()
    {
        var exercise = new ExerciseStateService();
        var vm = new MainWindowViewModel(exercise, new SyncBannerService());

        vm.ApplyTimeSyncStatus("healthy");
        vm.StartExerciseCommand.Execute(null);
        vm.ApplyTimeSyncStatus("sync_lost");
        vm.PauseReason.Should().Be("Time sync lost");

        // Operator acknowledges by clicking Resume.
        vm.ResumeExerciseCommand.Execute(null);

        vm.CurrentState.Should().Be(ExerciseState.Running);
        vm.PauseReason.Should().BeNull();
    }
}
