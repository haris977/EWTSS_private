using WpfUserControl = System.Windows.Controls.UserControl;
using Sg.Domain.ViewModels;

namespace Sg.App.Views.Shell;

public partial class PropertyPanelHostView : WpfUserControl
{
    public PropertyPanelHostView(PropertyPanelHostViewModel vm)
    {
        InitializeComponent();
        DataContext = vm;
    }
}
