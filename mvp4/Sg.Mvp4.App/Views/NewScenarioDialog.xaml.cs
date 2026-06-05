using System;
using System.Globalization;
using System.Windows;

namespace Sg.Mvp4.App.Views;

public partial class NewScenarioDialog : Window
{
    public string ScenarioName { get; private set; } = "Untitled";
    public DateTime StartUtc { get; private set; }
    public DateTime StopUtc  { get; private set; }

    public NewScenarioDialog()
    {
        InitializeComponent();
        var now = DateTime.UtcNow;
        StartBox.Text = now.ToString("yyyy-MM-ddTHH:mm:ssZ");
        StopBox.Text  = now.AddHours(1).ToString("yyyy-MM-ddTHH:mm:ssZ");
    }

    private void Ok_Click(object sender, RoutedEventArgs e)
    {
        if (!DateTime.TryParse(StartBox.Text, CultureInfo.InvariantCulture,
                               DateTimeStyles.AssumeUniversal | DateTimeStyles.AdjustToUniversal, out var s)
         || !DateTime.TryParse(StopBox.Text, CultureInfo.InvariantCulture,
                               DateTimeStyles.AssumeUniversal | DateTimeStyles.AdjustToUniversal, out var e2))
        {
            System.Windows.MessageBox.Show("Invalid date/time format.");
            return;
        }
        ScenarioName = NameBox.Text.Trim();
        StartUtc     = s;
        StopUtc      = e2;
        DialogResult = true;
    }
}
