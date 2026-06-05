using System.Net.Http;
using FluentAssertions;
using Sg.App.Contracts;
using Sg.App.Services;
using Sg.App.ViewModels;

namespace Sg.App.Tests.ViewModels;

[TestFixture]
public class PollingLoopTests
{
    private sealed class FakeTimeSyncClient : ITimeSyncClient
    {
        public string NextStatus { get; set; } = "healthy";
        public int CallCount { get; private set; }

        public Task<TimeSyncStatusDto> GetStatusAsync(CancellationToken ct = default)
        {
            CallCount++;
            return Task.FromResult(new TimeSyncStatusDto(
                CurrentTime: DateTime.UtcNow,
                NtpOffsetMs: 0.4,
                NtpJitterMs: 0.2,
                NtpPeer: "WS1-SG.local",
                LastSync: DateTime.UtcNow,
                Status: NextStatus));
        }
    }

    [Test, Category(TestCategories.Unit), CancelAfter(2000)]
    public async Task StartPolling_applies_status_and_exits_on_cancel()
    {
        var fake = new FakeTimeSyncClient { NextStatus = "healthy" };
        var vm = new MainWindowViewModel(new ExerciseStateService(), new SyncBannerService(), fake);

        using var cts = new CancellationTokenSource();
        var pollTask = vm.StartPolling(TimeSpan.FromMilliseconds(10), cts.Token);

        // Wait briefly for at least one poll to land, then cancel.
        for (var i = 0; i < 20 && fake.CallCount == 0; i++)
            await Task.Delay(10);

        cts.Cancel();
        await pollTask; // must not throw

        fake.CallCount.Should().BeGreaterOrEqualTo(1);
        vm.StartExerciseCommand.CanExecute(null).Should().BeTrue(); // healthy was applied
    }

    [Test, Category(TestCategories.Unit), CancelAfter(2000)]
    public async Task StartPolling_marks_sync_lost_on_client_exception()
    {
        var failing = new ThrowingClient();
        var vm = new MainWindowViewModel(new ExerciseStateService(), new SyncBannerService(), failing);

        using var cts = new CancellationTokenSource();
        var pollTask = vm.StartPolling(TimeSpan.FromMilliseconds(10), cts.Token);

        for (var i = 0; i < 20 && failing.CallCount == 0; i++)
            await Task.Delay(10);

        cts.Cancel();
        await pollTask;

        vm.Banner.CurrentBanner.Should().Be(BannerLevel.Lost);
        vm.StartExerciseCommand.CanExecute(null).Should().BeFalse();
    }

    private sealed class ThrowingClient : ITimeSyncClient
    {
        public int CallCount { get; private set; }
        public Task<TimeSyncStatusDto> GetStatusAsync(CancellationToken ct = default)
        {
            CallCount++;
            throw new HttpRequestException("drs-server unreachable");
        }
    }
}
