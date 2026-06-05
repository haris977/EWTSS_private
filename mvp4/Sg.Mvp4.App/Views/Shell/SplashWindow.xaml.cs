using System.Windows;

namespace Sg.Mvp4.App.Views.Shell;

public partial class SplashWindow : Window
{
    public SplashWindow() { InitializeComponent(); }
    public void SetStatus(string text) => StatusText.Text = text;
}
