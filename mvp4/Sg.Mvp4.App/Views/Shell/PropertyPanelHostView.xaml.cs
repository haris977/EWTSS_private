using WpfUserControl = System.Windows.Controls.UserControl;
using Sg.Mvp4.Domain.ViewModels;

namespace Sg.Mvp4.App.Views.Shell;

public partial class PropertyPanelHostView : WpfUserControl
{
    public PropertyPanelHostView(PropertyPanelHostViewModel vm)
    {
        InitializeComponent();
        DataContext = vm;
    }
}
