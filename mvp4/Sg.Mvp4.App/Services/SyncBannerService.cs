using System;
using CommunityToolkit.Mvvm.ComponentModel;

namespace Sg.Mvp4.App.Services;

public enum BannerLevel { None, Warn, Alert, Lost }

public sealed partial class SyncBannerService : ObservableObject
{
    [ObservableProperty]
    private BannerLevel _currentBanner = BannerLevel.None;

    [ObservableProperty]
    private string? _currentMessage;

    /// <summary>
    /// Updates the banner state from a /time/status status string or a
    /// system.timesync Kafka event. Idempotent.
    /// </summary>
    public void UpdateStatus(string status)
    {
        (CurrentBanner, CurrentMessage) = status switch
        {
            "healthy" or "warming" => (BannerLevel.None, null),
            "drift_warn"  => (BannerLevel.Warn,
                              "Time sync drift detected (offset > 10 ms). Exercise continues."),
            "drift_alert" => (BannerLevel.Alert,
                              "Time sync alert (offset > 50 ms). Outbound IRS frames marked sync-degraded."),
            "sync_lost"   => (BannerLevel.Lost,
                              "Time sync lost. Exercise auto-paused. Acknowledge before resume."),
            _             => (BannerLevel.None, null),
        };
    }
}
