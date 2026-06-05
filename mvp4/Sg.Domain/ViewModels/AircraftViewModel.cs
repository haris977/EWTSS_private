using System.Collections.ObjectModel;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using Sg.Domain.Contracts;
using Sg.Domain.Models;

namespace Sg.Domain.ViewModels;

public partial class AircraftViewModel : EntityViewModelBase
{
    private readonly IScenarioBackend _backend;
    private readonly string _path;
    private AircraftDto _committed;

    [ObservableProperty] private string _colorHex;
    [ObservableProperty] private bool   _pathVisible;

    public ObservableCollection<WaypointModel> Waypoints { get; } = new();

    public IRelayCommand AddWaypointCommand { get; }
    public IRelayCommand<WaypointModel> RemoveWaypointCommand { get; }

    public AircraftViewModel(IScenarioBackend backend, string path)
    {
        _backend   = backend;
        _path      = path;
        _committed = _backend.GetAircraft(path);

        _colorHex    = _committed.ColorHex;
        _pathVisible = _committed.PathVisible;
        foreach (var wp in _committed.Waypoints)
            AddWaypoint(wp.LatitudeDeg, wp.LongitudeDeg, wp.AltitudeMeters, wp.SpeedMps);
        Waypoints.CollectionChanged += (_, _) => MarkDirty();

        AddWaypointCommand = new RelayCommand(() =>
        {
            AddWaypoint(0, 0, 1500, 100);
            MarkDirty();
        });

        RemoveWaypointCommand = new RelayCommand<WaypointModel>(wp =>
        {
            if (wp is not null) { Waypoints.Remove(wp); MarkDirty(); }
        });

        PropertyChanged += (_, e) =>
        {
            if (e.PropertyName is nameof(ColorHex) or nameof(PathVisible)) MarkDirty();
        };
    }

    public override string EntityName => _committed.Name;

    private void AddWaypoint(double lat, double lon, double alt, double speed)
    {
        var wp = new WaypointModel
        {
            LatitudeDeg    = lat,
            LongitudeDeg   = lon,
            AltitudeMeters = alt,
            SpeedMps       = speed,
        };
        // Per-waypoint property edits in the panel must mark the VM dirty so Apply enables.
        wp.PropertyChanged += (_, _) => MarkDirty();
        Waypoints.Add(wp);
    }

    protected override void DoApply()
    {
        var dto = new AircraftDto(
            _committed.Name, ColorHex, PathVisible,
            Waypoints.Select(w => new WaypointDto(
                w.LatitudeDeg, w.LongitudeDeg, w.AltitudeMeters, w.SpeedMps)).ToList());
        _backend.UpdateAircraft(_path, dto);
        _committed = dto;
    }

    protected override void DoReset()
    {
        ColorHex    = _committed.ColorHex;
        PathVisible = _committed.PathVisible;
        Waypoints.Clear();
        foreach (var wp in _committed.Waypoints)
            AddWaypoint(wp.LatitudeDeg, wp.LongitudeDeg, wp.AltitudeMeters, wp.SpeedMps);
    }

    public override void Refresh()
    {
        if (_backend.FindByPath(_path) is null) return;
        _committed = _backend.GetAircraft(_path);
        DoReset();
        IsDirty = false;
    }
}
