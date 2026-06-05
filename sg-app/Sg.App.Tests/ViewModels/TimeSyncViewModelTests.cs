using FluentAssertions;
using Sg.App.Contracts;
using Sg.App.Services;
using Sg.App.ViewModels;

namespace Sg.App.Tests.ViewModels;

[TestFixture]
public class TimeSyncViewModelTests
{
    private sealed class FixedClient : ITimeSyncClient
    {
        public required TimeSyncStatusDto Dto { get; init; }
        public Task<TimeSyncStatusDto> GetStatusAsync(CancellationToken ct = default)
            => Task.FromResult(Dto);
    }

    [Test, Category(TestCategories.Unit)]
    public async Task RefreshAsync_populates_all_observable_fields()
    {
        var stamp = new DateTime(2026, 5, 14, 12, 34, 56, DateTimeKind.Utc);
        var client = new FixedClient
        {
            Dto = new TimeSyncStatusDto(
                CurrentTime: stamp,
                NtpOffsetMs: 0.42,
                NtpJitterMs: 0.18,
                NtpPeer: "WS1-SG.local",
                LastSync: stamp.AddSeconds(-2),
                Status: "healthy"),
        };
        var vm = new TimeSyncViewModel(client);

        await vm.RefreshAsync();

        vm.CurrentOffsetMs.Should().BeApproximately(0.42, 0.001);
        vm.CurrentJitterMs.Should().BeApproximately(0.18, 0.001);
        vm.NtpPeer.Should().Be("WS1-SG.local");
        vm.Status.Should().Be("healthy");
        vm.LastSync.Should().Be(stamp.AddSeconds(-2));
    }

    [Test, Category(TestCategories.Unit)]
    public void Default_status_is_warming_before_first_refresh()
    {
        var vm = new TimeSyncViewModel(new FixedClient
        {
            Dto = new TimeSyncStatusDto(
                DateTime.UtcNow, 0.0, 0.0, null, DateTime.UtcNow, "healthy"),
        });

        vm.Status.Should().Be("warming");
        vm.NtpPeer.Should().BeNull();
    }
}
