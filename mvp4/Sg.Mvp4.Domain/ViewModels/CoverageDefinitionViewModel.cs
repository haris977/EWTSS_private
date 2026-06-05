using System.Collections.ObjectModel;
using CommunityToolkit.Mvvm.ComponentModel;
using Sg.Mvp4.Domain.Contracts;
using Sg.Mvp4.Domain.Models;

namespace Sg.Mvp4.Domain.ViewModels;

public partial class CoverageDefinitionViewModel : EntityViewModelBase
{
    private readonly IScenarioBackend _backend;
    private readonly string _path;
    private CoverageDefinitionDto _committed;

    // Suppresses dirty-marking during a reset / initial load.
    private bool _suppressDirty;

    // -----------------------------------------------------------------------
    // Grid bounds
    // -----------------------------------------------------------------------
    [ObservableProperty] private double _latMin;
    [ObservableProperty] private double _latMax;
    [ObservableProperty] private double _lonMin;
    [ObservableProperty] private double _lonMax;

    // -----------------------------------------------------------------------
    // Grid resolution
    // -----------------------------------------------------------------------
    [ObservableProperty] private double _latStep;
    [ObservableProperty] private double _lonStep;

    // -----------------------------------------------------------------------
    // Constraints
    // -----------------------------------------------------------------------
    [ObservableProperty] private double _minAltitude;
    [ObservableProperty] private double _minElevationAngle;

    // -----------------------------------------------------------------------
    // Asset paths — the VM owns a mutable copy; Apply pushes it back.
    // -----------------------------------------------------------------------

    /// <summary>
    /// Currently selected asset paths for this coverage definition.
    /// Bind a ListBox (or similar) to this collection.
    /// </summary>
    public ObservableCollection<string> AssetPaths { get; } = new();

    // -----------------------------------------------------------------------
    // Available assets — computed from IScenarioBackend.Children
    // -----------------------------------------------------------------------

    /// <summary>
    /// Scenario objects that can act as coverage providers: Aircraft, Facility, Sensor.
    /// Refreshed from <see cref="IScenarioBackend.Children"/> each time the VM is reset
    /// or constructed.
    /// </summary>
    public IReadOnlyList<string> AvailableAssetPaths { get; private set; } = Array.Empty<string>();

    // -----------------------------------------------------------------------
    // Construction
    // -----------------------------------------------------------------------

    public CoverageDefinitionViewModel(IScenarioBackend backend, string path)
    {
        _backend   = backend;
        _path      = path;
        _committed = _backend.GetCoverage(path);

        _suppressDirty = true;
        LoadFromCommitted();
        _suppressDirty = false;

        // Any scalar property change → dirty (unless suppressed during load/reset)
        PropertyChanged += (_, e) =>
        {
            if (_suppressDirty) return;
            if (e.PropertyName is nameof(LatMin)  or nameof(LatMax)
                               or nameof(LonMin)  or nameof(LonMax)
                               or nameof(LatStep) or nameof(LonStep)
                               or nameof(MinAltitude) or nameof(MinElevationAngle))
                MarkDirty();
        };

        // Collection change → dirty
        AssetPaths.CollectionChanged += (_, _) => { if (!_suppressDirty) MarkDirty(); };
    }

    public override string EntityName => _committed.Name;

    // -----------------------------------------------------------------------
    // Apply / Reset
    // -----------------------------------------------------------------------

    protected override void DoApply()
    {
        var dto = new CoverageDefinitionDto(
            _committed.Name, LatMin, LatMax, LonMin, LonMax,
            LatStep, LonStep, MinAltitude, MinElevationAngle,
            AssetPaths.ToList());
        _backend.UpdateCoverage(_path, dto);
        _committed = dto;
    }

    protected override void DoReset()
    {
        _suppressDirty = true;
        try   { LoadFromCommitted(); }
        finally { _suppressDirty = false; }
    }

    // -----------------------------------------------------------------------
    // Helpers
    // -----------------------------------------------------------------------

    private void LoadFromCommitted()
    {
        LatMin  = _committed.LatMin;
        LatMax  = _committed.LatMax;
        LonMin  = _committed.LonMin;
        LonMax  = _committed.LonMax;
        LatStep = _committed.LatStep;
        LonStep = _committed.LonStep;
        MinAltitude       = _committed.MinAltitude;
        MinElevationAngle = _committed.MinElevationAngle;

        // Reload asset paths without triggering dirty
        AssetPaths.Clear();
        foreach (var p in _committed.AssetPaths)
            AssetPaths.Add(p);

        // Rebuild available-asset list from scenario
        AvailableAssetPaths = BuildAvailableAssets(_backend);
        OnPropertyChanged(nameof(AvailableAssetPaths));
    }

    private static IReadOnlyList<string> BuildAvailableAssets(IScenarioBackend backend)
    {
        var results = new List<string>();
        foreach (var node in backend.Children)
        {
            if (node.Kind is EntityKind.Aircraft or EntityKind.Facility)
                results.Add(node.Path);

            // Sensors nested under aircraft/facility
            if (node.Kind is EntityKind.Aircraft or EntityKind.Facility)
                foreach (var child in node.Children)
                    if (child.Kind == EntityKind.Sensor)
                        results.Add(child.Path);
        }
        return results;
    }
}
