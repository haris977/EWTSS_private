using Sg.Domain.Services;

namespace Sg.Tests.Fakes;

public sealed class FakeFileDialogService : IFileDialogService
{
    public NewScenarioRequest? NewResponse         { get; set; }
    public OpenRequest?        OpenResponse        { get; set; }
    public SaveAsRequest?      SaveAsResponse      { get; set; }
    public string?             VdfPasswordResponse { get; set; }
    public bool                DiscardConfirmation { get; set; } = true;

    public NewScenarioRequest? PromptNewScenario()      => NewResponse;
    public OpenRequest?        PromptOpen()             => OpenResponse;
    public SaveAsRequest?      PromptSaveAs(string s)   => SaveAsResponse;
    public string?             PromptVdfPassword()      => VdfPasswordResponse;
    public bool                ConfirmDiscardChanges()  => DiscardConfirmation;
}
