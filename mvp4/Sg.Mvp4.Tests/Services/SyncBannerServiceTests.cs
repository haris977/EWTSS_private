using NUnit.Framework;
using FluentAssertions;
using Sg.Mvp4.App.Services;

namespace Sg.Mvp4.Tests.Services;

[TestFixture]
public class SyncBannerServiceTests
{
    [Test, Category(TestCategories.Unit)]
    public void Transition_to_warn_raises_warn_banner()
    {
        var svc = new SyncBannerService();
        svc.UpdateStatus("healthy");
        svc.UpdateStatus("drift_warn");
        svc.CurrentBanner.Should().Be(BannerLevel.Warn);
    }

    [Test, Category(TestCategories.Unit)]
    public void Transition_to_alert_raises_alert_banner()
    {
        var svc = new SyncBannerService();
        svc.UpdateStatus("healthy");
        svc.UpdateStatus("drift_alert");
        svc.CurrentBanner.Should().Be(BannerLevel.Alert);
    }

    [Test, Category(TestCategories.Unit)]
    public void Transition_to_sync_lost_raises_lost_banner()
    {
        var svc = new SyncBannerService();
        svc.UpdateStatus("drift_alert");
        svc.UpdateStatus("sync_lost");
        svc.CurrentBanner.Should().Be(BannerLevel.Lost);
    }

    [Test, Category(TestCategories.Unit)]
    public void Return_to_healthy_clears_banner()
    {
        var svc = new SyncBannerService();
        svc.UpdateStatus("drift_warn");
        svc.UpdateStatus("healthy");
        svc.CurrentBanner.Should().Be(BannerLevel.None);
    }
}
