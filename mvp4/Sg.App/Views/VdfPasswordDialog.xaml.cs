using System.Windows;

namespace Sg.App.Views;

public partial class VdfPasswordDialog : Window
{
    public string Password { get; private set; } = "";
    public VdfPasswordDialog() { InitializeComponent(); }
    private void Ok_Click(object sender, RoutedEventArgs e)
    {
        Password = PwdBox.Password;
        DialogResult = true;
    }
}
