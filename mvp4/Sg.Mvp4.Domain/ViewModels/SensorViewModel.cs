using CommunityToolkit.Mvvm.ComponentModel;
using Sg.Mvp4.Domain.Contracts;
using Sg.Mvp4.Domain.Models;

namespace Sg.Mvp4.Domain.ViewModels;

public partial class SensorViewModel : EntityViewModelBase
{
    private readonly IScenarioBackend _backend;
    private readonly string _path;
    private SensorDto _committed;

    [ObservableProperty] private string       _colorHex;
    [ObservableProperty] private bool         _showIntersection;
    [ObservableProperty] private double       _outerHalfAngleDeg;
    [ObservableProperty] private double       _innerHalfAngleDeg;
    [ObservableProperty] private PointingMode _pointing;
    [ObservableProperty] private double       _fixedAzimuthDeg;
    [ObservableProperty] private double       _fixedElevationDeg;
    [ObservableProperty] private string?      _targetPath;

    public SensorViewModel(IScenarioBackend backend, string path)
    {
        _backend   = backend;
        _path      = path;
        _committed = _backend.GetSensor(path);

        _colorHex          = _committed.ColorHex;
        _showIntersection  = _committed.ShowIntersection;
        _outerHalfAngleDeg = _committed.OuterHalfAngleDeg;
        _innerHalfAngleDeg = _committed.InnerHalfAngleDeg;
        _pointing          = _committed.Pointing;
        _fixedAzimuthDeg   = _committed.FixedAzimuthDeg;
        _fixedElevationDeg = _committed.FixedElevationDeg;
        _targetPath        = _committed.TargetPath;

        PropertyChanged += (_, e) =>
        {
            if (e.PropertyName is not nameof(IsDirty)) MarkDirty();
        };
    }

    public override string EntityName => _committed.Name;

    protected override void DoApply()
    {
        var dto = new SensorDto(
            _committed.Name, _committed.ParentPath, ColorHex, ShowIntersection,
            OuterHalfAngleDeg, InnerHalfAngleDeg,
            Pointing, FixedAzimuthDeg, FixedElevationDeg, TargetPath);
        _backend.UpdateSensor(_path, dto);
        _committed = dto;
    }

    protected override void DoReset()
    {
        ColorHex          = _committed.ColorHex;
        ShowIntersection  = _committed.ShowIntersection;
        OuterHalfAngleDeg = _committed.OuterHalfAngleDeg;
        InnerHalfAngleDeg = _committed.InnerHalfAngleDeg;
        Pointing          = _committed.Pointing;
        FixedAzimuthDeg   = _committed.FixedAzimuthDeg;
        FixedElevationDeg = _committed.FixedElevationDeg;
        TargetPath        = _committed.TargetPath;
    }
}
