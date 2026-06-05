using System.Windows;
using System.Windows.Controls;
using WpfUserControl = System.Windows.Controls.UserControl;
using Sg.Domain.Interaction;
using Sg.Domain.Models;
using Sg.Domain.ViewModels;

namespace Sg.App.Views.Shell;

public partial class ObjectTreeView : WpfUserControl
{
    private readonly IInteractionController _controller;

    public ObjectTreeView(ObjectTreeViewModel vm, IInteractionController controller)
    {
        _controller = controller;
        InitializeComponent();
        DataContext = vm;
    }

    private ObjectTreeViewModel Vm => (ObjectTreeViewModel)DataContext;

    private void Tree_SelectedItemChanged(object sender, RoutedPropertyChangedEventArgs<object> e)
    {
        Vm.SelectedNode = e.NewValue as ObjectTreeNodeViewModel;
    }

    private void Tree_MouseDoubleClick(object sender, System.Windows.Input.MouseButtonEventArgs e)
    {
        if (Vm.SelectedPath is string path)
            _controller.StartEditingOnMap(path);
    }

    private void AddAircraft_Click(object sender, RoutedEventArgs e)      => _controller.BeginPlace(EntityKind.Aircraft);
    private void AddFacility_Click(object sender, RoutedEventArgs e)      => _controller.BeginPlace(EntityKind.Facility);
    private void AddAreaTarget_Click(object sender, RoutedEventArgs e)    => _controller.BeginPlace(EntityKind.AreaTarget);
    private void AddCoverage_Click(object sender, RoutedEventArgs e)
    {
        // CoverageDefinition placement is not yet map-driven; fall back to immediate creation.
        var name = UniqueName("Coverage", Vm.Nodes.Select(n => n.Name));
        Vm.AddChildCommand.Execute((EntityKind.CoverageDefinition, null, name));
    }

    private void Delete_Click(object sender, RoutedEventArgs e)
    {
        if (Vm.SelectedPath is string path)
            Vm.RemoveCommand.Execute(path);
    }

    private static string UniqueName(string baseName, IEnumerable<string> existing)
    {
        var taken = new HashSet<string>(existing);
        for (var i = 1; i <= 10_000; i++)
        {
            var candidate = $"{baseName}{i}";
            if (!taken.Contains(candidate)) return candidate;
        }
        throw new InvalidOperationException(
            $"Could not generate a unique name for base '{baseName}' within 10 000 attempts.");
    }
}
