using CommunityToolkit.Mvvm.ComponentModel;

namespace Sg.Mvp4.Domain.Models;

public partial class WaypointModel : ObservableObject
{
    [ObservableProperty] private double _latitudeDeg;
    [ObservableProperty] private double _longitudeDeg;
    [ObservableProperty] private double _altitudeMeters;
    [ObservableProperty] private double _speedMps;
}
