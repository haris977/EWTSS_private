using System.Collections.ObjectModel;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using Sg.Domain.Contracts;
using Sg.Domain.Models;

namespace Sg.Domain.ViewModels;

public partial class AreaTargetViewModel : EntityViewModelBase
{
    private readonly IScenarioBackend _backend;
    private readonly string _path;
    private AreaTargetDto _committed;

    [ObservableProperty] private string _outlineColorHex;
    [ObservableProperty] private bool   _fillEnabled;
    [ObservableProperty] private string _fillColorHex;

    public ObservableCollection<VertexModel> Vertices { get; } = new();

    public IRelayCommand AddVertexCommand { get; }
    public IRelayCommand<VertexModel> RemoveVertexCommand { get; }

    public AreaTargetViewModel(IScenarioBackend backend, string path)
    {
        _backend   = backend;
        _path      = path;
        _committed = _backend.GetAreaTarget(path);

        _outlineColorHex = _committed.OutlineColorHex;
        _fillEnabled     = _committed.FillEnabled;
        _fillColorHex    = _committed.FillColorHex;
        foreach (var v in _committed.Vertices)
            AddVertex(v.LatitudeDeg, v.LongitudeDeg);

        Vertices.CollectionChanged += (_, _) => MarkDirty();

        AddVertexCommand = new RelayCommand(() =>
        {
            AddVertex(0, 0);
            MarkDirty();
        });

        RemoveVertexCommand = new RelayCommand<VertexModel>(v =>
        {
            if (v is not null) { Vertices.Remove(v); MarkDirty(); }
        });

        PropertyChanged += (_, e) =>
        {
            if (e.PropertyName is nameof(OutlineColorHex) or nameof(FillEnabled) or nameof(FillColorHex))
                MarkDirty();
        };
    }

    public override string EntityName => _committed.Name;

    private void AddVertex(double lat, double lon)
    {
        var v = new VertexModel { LatitudeDeg = lat, LongitudeDeg = lon };
        // Per-vertex property edits in the panel must mark the VM dirty so Apply enables.
        v.PropertyChanged += (_, _) => MarkDirty();
        Vertices.Add(v);
    }

    protected override void DoApply()
    {
        var dto = new AreaTargetDto(
            _committed.Name, OutlineColorHex, FillEnabled, FillColorHex,
            Vertices.Select(v => new VertexDto(v.LatitudeDeg, v.LongitudeDeg)).ToList());
        _backend.UpdateAreaTarget(_path, dto);
        _committed = dto;
    }

    protected override void DoReset()
    {
        OutlineColorHex = _committed.OutlineColorHex;
        FillEnabled     = _committed.FillEnabled;
        FillColorHex    = _committed.FillColorHex;
        Vertices.Clear();
        foreach (var v in _committed.Vertices)
            AddVertex(v.LatitudeDeg, v.LongitudeDeg);
    }

    public override void Refresh()
    {
        if (_backend.FindByPath(_path) is null) return;
        _committed = _backend.GetAreaTarget(_path);
        DoReset();
        IsDirty = false;
    }
}
