using System;
using System.Threading.Tasks;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using Sg.App.Services;

namespace Sg.App.ViewModels;

/// <summary>
/// VM for the Admin → Time Sync view. Reads /time/status on demand and
/// exposes the parsed fields for display. Polling is not done here (the
/// banner host's poll loop already covers continuous updates); this VM
/// is for an explicit Refresh-on-demand inspection surface.
/// </summary>
public sealed partial class TimeSyncViewModel : ObservableObject
{
    private readonly ITimeSyncClient _client;

    [ObservableProperty] private double _currentOffsetMs;
    [ObservableProperty] private double _currentJitterMs;
    [ObservableProperty] private string? _ntpPeer;
    [ObservableProperty] private string _status = "warming";
    [ObservableProperty] private DateTime _lastSync;

    public TimeSyncViewModel(ITimeSyncClient client)
    {
        _client = client;
    }

    [RelayCommand]
    public async Task RefreshAsync()
    {
        var dto = await _client.GetStatusAsync();
        CurrentOffsetMs = dto.NtpOffsetMs;
        CurrentJitterMs = dto.NtpJitterMs;
        NtpPeer = dto.NtpPeer;
        Status = dto.Status;
        LastSync = dto.LastSync;
    }
}
