using System.IO;
using Sg.App.Views;
using Sg.Domain.Services;

namespace Sg.App.Services;

public sealed class FileDialogService : IFileDialogService
{
    public NewScenarioRequest? PromptNewScenario()
    {
        var dlg = new NewScenarioDialog();
        return dlg.ShowDialog() == true
            ? new NewScenarioRequest(dlg.ScenarioName, dlg.StartUtc, dlg.StopUtc)
            : null;
    }

    public OpenRequest? PromptOpen()
    {
        var dlg = new Microsoft.Win32.OpenFileDialog
        {
            Filter = "STK scenario (*.sc)|*.sc|Packaged scenario (*.vdf)|*.vdf|All STK files|*.sc;*.vdf",
            FilterIndex = 3,
        };
        if (dlg.ShowDialog() != true) return null;
        return new OpenRequest(dlg.FileName, Password: null);
    }

    public SaveAsRequest? PromptSaveAs(string suggestedPath)
    {
        var dlg = new Microsoft.Win32.SaveFileDialog
        {
            Filter = "STK scenario (*.sc)|*.sc|Packaged scenario (*.vdf)|*.vdf",
            FileName = Path.GetFileName(suggestedPath),
            InitialDirectory = Path.GetDirectoryName(suggestedPath) ?? "",
        };
        if (dlg.ShowDialog() != true) return null;
        var fmt = dlg.FilterIndex == 2 ? SaveFormat.Vdf : SaveFormat.Sc;
        string? pwd = null;
        if (fmt == SaveFormat.Vdf) pwd = PromptVdfPassword();
        return new SaveAsRequest(dlg.FileName, fmt, pwd);
    }

    public string? PromptVdfPassword()
    {
        var dlg = new VdfPasswordDialog();
        return dlg.ShowDialog() == true ? dlg.Password : null;
    }

    public bool ConfirmDiscardChanges()
    {
        var r = System.Windows.MessageBox.Show("There are unsaved changes. Discard and continue?",
                                               "Unsaved changes",
                                               System.Windows.MessageBoxButton.YesNo,
                                               System.Windows.MessageBoxImage.Warning);
        return r == System.Windows.MessageBoxResult.Yes;
    }
}
