namespace Sg.Mvp4.Domain.Services;

public enum SaveFormat { Sc, Vdf }

public sealed record NewScenarioRequest(string Name, DateTime StartUtc, DateTime StopUtc);
public sealed record OpenRequest       (string Path, string? Password);
public sealed record SaveAsRequest     (string Path, SaveFormat Format, string? Password);

public interface IFileDialogService
{
    NewScenarioRequest? PromptNewScenario();
    OpenRequest?        PromptOpen();
    SaveAsRequest?      PromptSaveAs(string suggestedPath);
    string?             PromptVdfPassword();
    bool                ConfirmDiscardChanges();
}
