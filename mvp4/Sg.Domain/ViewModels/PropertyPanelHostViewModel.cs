using CommunityToolkit.Mvvm.ComponentModel;
using Sg.Domain.Contracts;
using Sg.Domain.Interaction;
using Sg.Domain.Models;

namespace Sg.Domain.ViewModels;

public partial class PropertyPanelHostViewModel : ObservableObject
{
    private readonly IScenarioBackend       _stk;
    private readonly ObjectTreeViewModel    _tree;
    private readonly IInteractionController _controller;

    [ObservableProperty] private object _current = new EmptySelectionViewModel();

    public PropertyPanelHostViewModel(
        IScenarioBackend stk,
        ObjectTreeViewModel tree,
        IInteractionController controller)
    {
        _stk         = stk;
        _tree        = tree;
        _controller  = controller;

        _tree.SelectionChanged += (_, path) => UpdateCurrent(path);
        _controller.ModeChanged += (_, _) => OnPropertyChanged(nameof(IsCommittingEdit));

        // When STK mutates an entity outside our DoApply (e.g. user dragged edit
        // handles on the map), the backend raises ScenarioChanged. Refresh the
        // active VM ONLY if the change is for that entity — otherwise an unrelated
        // map-edit on entity A would clobber unsaved panel edits the user made on
        // entity B's panel. EditingPath is non-null only during EditingEntity mode,
        // which is the only path that produces a ScenarioChanged the panel didn't
        // initiate itself; the panel's own Apply already updates _committed.
        _stk.ScenarioChanged += (_, _) =>
        {
            // Diagnostic — written via the same Desktop file as StkDisplayHost.
            _diag($"ScenarioChanged: Current={(Current as EntityViewModelBase)?.EntityName ?? Current?.GetType().Name ?? "null"}, EditingPath={_controller.EditingPath ?? "null"}, Mode={_controller.Mode}");

            if (Current is not EntityViewModelBase vm)               { _diag("  skip: Current is not an EntityViewModelBase"); return; }
            if (_controller.EditingPath is not string ep)             { _diag("  skip: EditingPath is null (not currently in edit mode)"); return; }
            if (vm.EntityName != LastSegment(ep))                     { _diag($"  skip: vm.EntityName='{vm.EntityName}' does not match EditingPath last segment '{LastSegment(ep)}'"); return; }
            vm.Refresh();
            vm.MarkDirty();
            _diag($"  → Refresh+MarkDirty applied (IsDirty now {vm.IsDirty})");
        };
    }

    // Same MVP4_DIAG env-gate as StkDisplayHost. Off by default; set the variable
    // to enable verbose ScenarioChanged-handler trace alongside the StkDisplayHost
    // log lines in Desktop\stk-debug.log.
    private static readonly bool _diagEnabled =
        string.Equals(Environment.GetEnvironmentVariable("MVP4_DIAG"), "1", StringComparison.OrdinalIgnoreCase) ||
        string.Equals(Environment.GetEnvironmentVariable("MVP4_DIAG"), "true", StringComparison.OrdinalIgnoreCase);
    private static readonly object _diagLock = new();
    private static readonly string _diagPath = System.IO.Path.Combine(
        Environment.GetFolderPath(Environment.SpecialFolder.Desktop), "stk-debug.log");
    private static void _diag(string message)
    {
        if (!_diagEnabled) return;
        try
        {
            lock (_diagLock)
                System.IO.File.AppendAllText(_diagPath,
                    $"{DateTime.Now:HH:mm:ss.fff} [PanelHost] {message}{Environment.NewLine}");
        }
        catch { }
    }

    /// <summary>
    /// True when the controller is in EditingEntity mode AND the editing path
    /// matches the entity in this panel. Used by the view to highlight Apply.
    /// </summary>
    public bool IsCommittingEdit
    {
        get
        {
            if (_controller.Mode != InteractionMode.EditingEntity) return false;
            if (_controller.EditingPath is null) return false;
            return Current is EntityViewModelBase vm && vm.EntityName == LastSegment(_controller.EditingPath);
        }
    }

    private void UpdateCurrent(string? path)
    {
        if (path is null) { Current = new EmptySelectionViewModel(); OnPropertyChanged(nameof(IsCommittingEdit)); return; }

        var node = _stk.FindByPath(path);
        Current = node is null
            ? new EmptySelectionViewModel()
            : PanelFactory.Create(node, _stk);

        OnPropertyChanged(nameof(IsCommittingEdit));
    }

    private static string LastSegment(string path) =>
        path.Split('/') is { } parts && parts.Length > 0 ? parts[^1] : path;
}

internal static class PanelFactory
{
    public static object Create(EntityNodeDto node, IScenarioBackend stk) =>
        node.Kind switch
        {
            EntityKind.Aircraft           => new AircraftViewModel          (stk, node.Path),
            EntityKind.Facility           => new FacilityViewModel          (stk, node.Path),
            EntityKind.AreaTarget         => new AreaTargetViewModel        (stk, node.Path),
            EntityKind.Sensor             => new SensorViewModel            (stk, node.Path),
            EntityKind.CoverageDefinition => new CoverageDefinitionViewModel(stk, node.Path),
            EntityKind.FigureOfMerit      => new FigureOfMeritViewModel     (stk, node.Path),
            _ => new EmptySelectionViewModel()
        };
}
