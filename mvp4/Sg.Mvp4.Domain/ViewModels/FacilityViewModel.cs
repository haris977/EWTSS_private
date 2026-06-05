using CommunityToolkit.Mvvm.ComponentModel;
using Sg.Mvp4.Domain.Contracts;

namespace Sg.Mvp4.Domain.ViewModels;

public partial class FacilityViewModel : EntityViewModelBase
{
    private readonly IScenarioBackend _backend;
    private readonly string _path;
    private FacilityDto _committed;

    [ObservableProperty] private string _colorHex;
    [ObservableProperty] private double _latitudeDeg;
    [ObservableProperty] private double _longitudeDeg;
    [ObservableProperty] private double _altitudeMeters;

    public FacilityViewModel(IScenarioBackend backend, string path)
    {
        _backend   = backend;
        _path      = path;
        _committed = _backend.GetFacility(path);

        _colorHex       = _committed.ColorHex;
        _latitudeDeg    = _committed.LatitudeDeg;
        _longitudeDeg   = _committed.LongitudeDeg;
        _altitudeMeters = _committed.AltitudeMeters;

        PropertyChanged += (_, e) =>
        {
            if (e.PropertyName is nameof(ColorHex) or nameof(LatitudeDeg)
                                                   or nameof(LongitudeDeg)
                                                   or nameof(AltitudeMeters))
                MarkDirty();
        };
    }

    public override string EntityName => _committed.Name;

    protected override void DoApply()
    {
        var dto = new FacilityDto(_committed.Name, ColorHex, LatitudeDeg, LongitudeDeg, AltitudeMeters);
        _backend.UpdateFacility(_path, dto);
        _committed = dto;
    }

    protected override void DoReset()
    {
        ColorHex       = _committed.ColorHex;
        LatitudeDeg    = _committed.LatitudeDeg;
        LongitudeDeg   = _committed.LongitudeDeg;
        AltitudeMeters = _committed.AltitudeMeters;
    }

    public override void Refresh()
    {
        if (_backend.FindByPath(_path) is null) return;
        _committed = _backend.GetFacility(_path);
        DoReset();
        IsDirty = false;
    }
}
