using System.Windows;
using WpfUserControl = System.Windows.Controls.UserControl;
using Microsoft.Extensions.DependencyInjection;
using Sg.Mvp4.Domain.Interaction;
using Sg.Mvp4.Domain.ViewModels;

namespace Sg.Mvp4.App.Views.Entities;

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
