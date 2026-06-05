using System.Windows;
using WpfUserControl = System.Windows.Controls.UserControl;
using Sg.Mvp4.Domain.Models;

namespace Sg.Mvp4.App.Views.Entities;

public partial class SensorPanel : WpfUserControl
{
    public SensorPanel()
    {
        InitializeComponent();

        // Populate pointing mode ComboBox from enum so XAML stays simple.
        PointingCombo.ItemsSource = Enum.GetValues<PointingMode>();
    }
}
