using CommunityToolkit.Mvvm.ComponentModel;

namespace Sg.Mvp4.Domain.Models;

public partial class VertexModel : ObservableObject
{
    [ObservableProperty] private double _latitudeDeg;
    [ObservableProperty] private double _longitudeDeg;
}
