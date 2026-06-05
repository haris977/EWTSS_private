using CommunityToolkit.Mvvm.ComponentModel;
using Sg.Mvp4.Domain.Contracts;
using Sg.Mvp4.Domain.Models;

namespace Sg.Mvp4.Domain.ViewModels;

public partial class FigureOfMeritViewModel : EntityViewModelBase
{
    private readonly IScenarioBackend _backend;
    private readonly string _path;
    private FigureOfMeritDto _committed;

    [ObservableProperty] private FomKind      _kind;
    [ObservableProperty] private FomStatistic _statistic;

    public FigureOfMeritViewModel(IScenarioBackend backend, string path)
    {
        _backend   = backend;
        _path      = path;
        _committed = _backend.GetFom(path);

        _kind      = _committed.Kind;
        _statistic = _committed.Statistic;

        PropertyChanged += (_, e) =>
        {
            if (e.PropertyName is nameof(Kind) or nameof(Statistic)) MarkDirty();
        };
    }

    public override string EntityName => _committed.Name;

    protected override void DoApply()
    {
        var dto = new FigureOfMeritDto(_committed.Name, _committed.ParentPath, Kind, Statistic);
        _backend.UpdateFom(_path, dto);
        _committed = dto;
    }

    protected override void DoReset()
    {
        Kind      = _committed.Kind;
        Statistic = _committed.Statistic;
    }
}
