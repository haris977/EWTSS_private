using System.Collections.ObjectModel;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using Sg.Domain.Contracts;
using Sg.Domain.Models;

namespace Sg.Domain.ViewModels;

public partial class ObjectTreeViewModel : ObservableObject
{
    private readonly IScenarioBackend _stk;

    [ObservableProperty] private ObjectTreeNodeViewModel? _selectedNode;

    public ObservableCollection<ObjectTreeNodeViewModel> Nodes { get; } = new();

    public string? SelectedPath => SelectedNode?.Path;

    public IRelayCommand<(EntityKind kind, string? parentPath, string name)> AddChildCommand { get; }
    public IRelayCommand<string> RemoveCommand { get; }

    public event EventHandler<string?>? SelectionChanged;

    public ObjectTreeViewModel(IScenarioBackend stk)
    {
        _stk = stk;
        _stk.ScenarioChanged += (_, _) => RebuildFromService();

        AddChildCommand = new RelayCommand<(EntityKind, string?, string)>(args =>
        {
            _stk.AddEntity(args.Item1, args.Item3, args.Item2);
        });

        RemoveCommand = new RelayCommand<string>(path =>
        {
            if (path is not null) _stk.RemoveEntity(path);
        });

        RebuildFromService();
    }

    partial void OnSelectedNodeChanged(ObjectTreeNodeViewModel? value)
    {
        OnPropertyChanged(nameof(SelectedPath));
        SelectionChanged?.Invoke(this, value?.Path);
    }

    private void RebuildFromService()
    {
        // Skip the rebuild when the tree's STRUCTURE hasn't changed (only entity
        // CONTENT did — e.g. a waypoint dragged on the map). A full rebuild during
        // an edit would clear SelectedNode, which cascades into PropertyPanelHost
        // setting Current = EmptySelectionViewModel — visibly hiding the active
        // entity panel and breaking the post-edit Refresh+MarkDirty chain.
        var newPaths = new HashSet<string>();
        foreach (var c in _stk.Children) AddPathsRecursive(c, newPaths);

        var currentPaths = new HashSet<string>();
        foreach (var n in Nodes) AddPathsRecursive(n, currentPaths);

        if (currentPaths.SetEquals(newPaths)) return;

        var prevPath = SelectedNode?.Path;
        Nodes.Clear();
        foreach (var child in _stk.Children)
            Nodes.Add(new ObjectTreeNodeViewModel(child));
        SelectedNode = prevPath is not null ? FindNodeByPath(prevPath) : null;
    }

    private static void AddPathsRecursive(EntityNodeDto node, HashSet<string> sink)
    {
        sink.Add(node.Path);
        foreach (var c in node.Children) AddPathsRecursive(c, sink);
    }

    private static void AddPathsRecursive(ObjectTreeNodeViewModel node, HashSet<string> sink)
    {
        sink.Add(node.Path);
        foreach (var c in node.Children) AddPathsRecursive(c, sink);
    }

    private ObjectTreeNodeViewModel? FindNodeByPath(string path)
    {
        foreach (var n in Nodes)
        {
            var found = FindNodeByPath(n, path);
            if (found is not null) return found;
        }
        return null;
    }

    private static ObjectTreeNodeViewModel? FindNodeByPath(ObjectTreeNodeViewModel node, string path)
    {
        if (node.Path == path) return node;
        foreach (var c in node.Children)
        {
            var found = FindNodeByPath(c, path);
            if (found is not null) return found;
        }
        return null;
    }
}
