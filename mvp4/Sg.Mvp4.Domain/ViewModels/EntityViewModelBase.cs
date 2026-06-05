using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;

namespace Sg.Mvp4.Domain.ViewModels;

public abstract partial class EntityViewModelBase : ObservableObject
{
    [ObservableProperty] private bool _isDirty;

    public abstract string EntityName { get; }

    public IRelayCommand ApplyCommand { get; }
    public IRelayCommand ResetCommand { get; }

    protected EntityViewModelBase()
    {
        ApplyCommand = new RelayCommand(Apply, () => IsDirty);
        ResetCommand = new RelayCommand(Reset, () => IsDirty);
        PropertyChanged += (_, e) =>
        {
            if (e.PropertyName == nameof(IsDirty))
            {
                ApplyCommand.NotifyCanExecuteChanged();
                ResetCommand.NotifyCanExecuteChanged();
            }
        };
    }

    public void MarkDirty() => IsDirty = true;

    private void Apply() { DoApply(); IsDirty = false; }
    private void Reset() { DoReset(); IsDirty = false; }

    /// <summary>
    /// Reload state from the backend. Called after STK mutates the entity outside
    /// our own DoApply (e.g. the user drags edit handles on the map). Implementers
    /// should re-fetch the entity DTO, repopulate observable state, and clear IsDirty.
    /// </summary>
    public virtual void Refresh() { }

    protected abstract void DoApply();
    protected abstract void DoReset();
}
