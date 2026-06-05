using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using Sg.Mvp4.Domain.Contracts;
using Sg.Mvp4.Domain.Interaction;
using Sg.Mvp4.Domain.Services;

namespace Sg.Mvp4.Domain.ViewModels;

public partial class MainWindowViewModel : ObservableObject
{
    private readonly IScenarioBackend       _stk;
    private readonly IFileDialogService     _fileDialogs;
    private readonly IInteractionController _controller;

    [ObservableProperty] private bool   _isDirty;
    [ObservableProperty] private string _statusMessage = "Ready.";

    public IRelayCommand NewCommand     { get; }
    public IRelayCommand OpenCommand    { get; }
    public IRelayCommand SaveCommand    { get; }
    public IRelayCommand SaveAsCommand  { get; }
    public IRelayCommand ComputeCommand { get; }

    public MainWindowViewModel(
        IScenarioBackend stk,
        IFileDialogService dialogs,
        IInteractionController controller)
    {
        _stk         = stk;
        _fileDialogs = dialogs;
        _controller  = controller;

        _stk.ScenarioChanged    += (_, _) => { OnPropertyChanged(nameof(Title)); OnPropertyChanged(nameof(StatusPath)); };
        _controller.ModeChanged += (_, _) => OnPropertyChanged(nameof(StatusHint));

        NewCommand    = new RelayCommand(NewScenario);
        OpenCommand   = new RelayCommand(OpenScenario);
        SaveCommand   = new RelayCommand(Save, () => _stk.CurrentPath is not null);
        SaveAsCommand = new RelayCommand(SaveAs);

        ComputeCommand = new RelayCommand(Compute, HasCoverage);
        _stk.ScenarioChanged += (_, _) => ComputeCommand.NotifyCanExecuteChanged();
    }

    public string  Title      => _stk.ScenarioName is null ? "MVP4 — (no scenario)" : $"MVP4 — {_stk.ScenarioName}";
    public string? StatusPath => _stk.CurrentPath is null
        ? null
        : $"{_stk.CurrentPath}{(_stk.CurrentIsPackaged ? " (packaged)" : "")}";
    public string  StatusHint => _controller.StatusHint;

    private void NewScenario()
    {
        if (IsDirty && !_fileDialogs.ConfirmDiscardChanges()) return;
        var req = _fileDialogs.PromptNewScenario();
        if (req is null) return;
        _stk.NewScenario(req.Name, req.StartUtc, req.StopUtc);
        IsDirty = false;
    }

    private void OpenScenario()
    {
        if (IsDirty && !_fileDialogs.ConfirmDiscardChanges()) return;
        var res = _fileDialogs.PromptOpen();
        if (res is null) return;
        if (Path.GetExtension(res.Path).Equals(".vdf", StringComparison.OrdinalIgnoreCase))
            _stk.LoadVdf(res.Path, res.Password ?? "");
        else
            _stk.LoadSc(res.Path);
        IsDirty = false;
        SaveCommand.NotifyCanExecuteChanged();
    }

    private void Save()
    {
        if (_stk.CurrentPath is null) { SaveAs(); return; }
        if (_stk.CurrentIsPackaged) _stk.SaveAsVdf(_stk.CurrentPath, password: "");
        else                        _stk.SaveAsSc(_stk.CurrentPath);
        IsDirty = false;
    }

    private void SaveAs()
    {
        var name      = _stk.ScenarioName ?? "Untitled";
        var suggested = _stk.SuggestedPath(name);
        var res       = _fileDialogs.PromptSaveAs(suggested);
        if (res is null) return;
        if (res.Format == SaveFormat.Vdf) _stk.SaveAsVdf(res.Path, res.Password ?? "");
        else                              _stk.SaveAsSc(res.Path);
        IsDirty = false;
        SaveCommand.NotifyCanExecuteChanged();
    }

    private bool HasCoverage()
    {
        foreach (var c in _stk.Children)
            if (c.Kind == Models.EntityKind.CoverageDefinition) return true;
        return false;
    }

    private void Compute()
    {
        try
        {
            StatusMessage = "Compute running...";
            _ = _stk.ComputeAll();
            StatusMessage = "Compute complete.";
        }
        catch (Exception ex)
        {
            StatusMessage = $"Compute failed: {ex.Message}";
        }
    }
}
