using System.Windows;
using WpfUserControl = System.Windows.Controls.UserControl;
using Microsoft.Extensions.DependencyInjection;
using Sg.Domain.Interaction;
using Sg.Domain.ViewModels;

namespace Sg.App.Views.Entities;

public partial class AreaTargetPanel : WpfUserControl
{
    public AreaTargetPanel() { InitializeComponent(); }

    private void EditOnMap_Click(object sender, RoutedEventArgs e)
    {
        if (DataContext is not AreaTargetViewModel vm) return;
        var controller = ((App)System.Windows.Application.Current).Services.GetRequiredService<IInteractionController>();
        controller.StartEditingOnMap(vm.EntityName);
    }
}
