using System.Collections.ObjectModel;
using CommunityToolkit.Mvvm.ComponentModel;
using Sg.Domain.Contracts;
using Sg.Domain.Models;

namespace Sg.Domain.ViewModels;

public partial class ObjectTreeNodeViewModel : ObservableObject
{
    [ObservableProperty] private bool _isExpanded = true;

    public ObjectTreeNodeViewModel(EntityNodeDto source)
    {
        Kind = source.Kind;
        Name = source.Name;
        Path = source.Path;
        Children = new ObservableCollection<ObjectTreeNodeViewModel>(
            source.Children.Select(c => new ObjectTreeNodeViewModel(c)));
    }

    public EntityKind Kind { get; }
    public string     Name { get; }
    public string     Path { get; }
    public ObservableCollection<ObjectTreeNodeViewModel> Children { get; }

    public string DisplayLabel => $"{Kind}: {Name}";
}
