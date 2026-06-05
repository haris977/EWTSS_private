using FluentAssertions;
using NUnit.Framework;
using Sg.Mvp4.Domain.Services;
using Sg.Mvp4.Domain.ViewModels;
using Sg.Mvp4.Tests.Fakes;

namespace Sg.Mvp4.Tests.ViewModels;

[TestFixture, Category(TestCategories.Unit)]
public class MainWindowViewModelTests
{
    private static MainWindowViewModel MakeVm(
        FakeScenarioBackend?       svc        = null,
        FakeFileDialogService?     dialogs    = null,
        FakeInteractionController? controller = null)
        => new(
            svc        ?? new FakeScenarioBackend(),
            dialogs    ?? new FakeFileDialogService(),
            controller ?? new FakeInteractionController());

    [Test]
    public void Title_shows_no_scenario_when_name_null()
    {
        MakeVm().Title.Should().Be("MVP4 — (no scenario)");
    }

    [Test]
    public void Title_reflects_current_scenario_name()
    {
        var svc = new FakeScenarioBackend();
        var vm  = MakeVm(svc);

        svc.NewScenario("Scenario34", DateTime.UtcNow, DateTime.UtcNow.AddHours(1));

        vm.Title.Should().Be("MVP4 — Scenario34");
    }

    [Test]
    public void IsDirty_is_false_initially()
    {
        MakeVm().IsDirty.Should().BeFalse();
    }

    [Test]
    public void StatusHint_reflects_controller_hint()
    {
        var controller = new FakeInteractionController { StatusHint = "Test hint." };
        var vm = MakeVm(controller: controller);

        vm.StatusHint.Should().Be("Test hint.");
    }

    [Test]
    public void StatusHint_PropertyChanged_fires_when_ModeChanged_raised()
    {
        var controller = new FakeInteractionController();
        var vm = MakeVm(controller: controller);

        var changed = new List<string?>();
        vm.PropertyChanged += (_, e) => changed.Add(e.PropertyName);

        controller.RaiseModeChanged();

        changed.Should().Contain(nameof(vm.StatusHint));
    }

    [Test]
    public void SaveCommand_disabled_when_no_current_path()
    {
        var stk        = new FakeScenarioBackend();
        var dialogs    = new FakeFileDialogService();
        var controller = new FakeInteractionController();
        var vm = new MainWindowViewModel(stk, dialogs, controller);

        vm.SaveCommand.CanExecute(null).Should().BeFalse();
    }

    [Test]
    public void SaveCommand_enabled_after_SaveAs_via_dialog()
    {
        var stk     = new FakeScenarioBackend();
        stk.NewScenario("S", DateTime.UtcNow, DateTime.UtcNow.AddHours(1));
        var dialogs    = new FakeFileDialogService { SaveAsResponse = new SaveAsRequest(@"C:\tmp\T.sc", SaveFormat.Sc, null) };
        var controller = new FakeInteractionController();
        var vm = new MainWindowViewModel(stk, dialogs, controller);

        vm.SaveAsCommand.Execute(null);

        vm.SaveCommand.CanExecute(null).Should().BeTrue();
        stk.Saves.Should().ContainSingle(s => s.path == @"C:\tmp\T.sc" && !s.packaged);
    }

    [Test]
    public void Open_loads_vdf_when_extension_is_vdf()
    {
        var stk        = new FakeScenarioBackend();
        var dialogs    = new FakeFileDialogService { OpenResponse = new OpenRequest(@"C:\tmp\T.vdf", "") };
        var controller = new FakeInteractionController();
        var vm = new MainWindowViewModel(stk, dialogs, controller);

        vm.OpenCommand.Execute(null);

        stk.Loads.Should().ContainSingle(l => l.packaged && l.path.EndsWith(".vdf"));
    }
}
