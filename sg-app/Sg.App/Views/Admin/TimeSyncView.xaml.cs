using System.Windows.Controls;
using Sg.App.ViewModels;

namespace Sg.App.Views.Admin;

public partial class TimeSyncView : UserControl
{
    public TimeSyncView(TimeSyncViewModel viewModel)
    {
        InitializeComponent();
        DataContext = viewModel;
    }
}
