using FluentAssertions;
using NUnit.Framework;
using Sg.Mvp4.Domain.Models;
using Sg.Mvp4.Domain.ViewModels;
using Sg.Mvp4.Tests.Fakes;

namespace Sg.Mvp4.Tests.ViewModels;

[TestFixture, Category(TestCategories.Unit)]
public class ComputeTests
{
    private static MainWindowViewModel CreateVm(FakeScenarioBackend svc) =>
        new(svc, new FakeFileDialogService(), new FakeInteractionController());

    [Test]
    public void ComputeCommand_disabled_when_no_coverage_definition()
    {
        var svc = new FakeScenarioBackend();
        svc.NewScenario("S", DateTime.UtcNow, DateTime.UtcNow.AddHours(1));
        var vm = CreateVm(svc);

        vm.ComputeCommand.CanExecute(null).Should().BeFalse();
    }

    [Test]
    public void ComputeCommand_enabled_when_coverage_definition_exists()
    {
        var svc = new FakeScenarioBackend();
        svc.NewScenario("S", DateTime.UtcNow, DateTime.UtcNow.AddHours(1));
        svc.AddEntity(EntityKind.CoverageDefinition, "Cov1", null);
        var vm = CreateVm(svc);

        vm.ComputeCommand.CanExecute(null).Should().BeTrue();
    }

    [Test]
    public void ComputeCommand_invokes_ComputeAll_and_sets_status()
    {
        var svc = new FakeScenarioBackend();
        svc.NewScenario("S", DateTime.UtcNow, DateTime.UtcNow.AddHours(1));
        svc.AddEntity(EntityKind.CoverageDefinition, "Cov1", null);
        var vm = CreateVm(svc);

        vm.ComputeCommand.Execute(null);

        svc.ComputeCallCount.Should().Be(1);
        vm.StatusMessage.Should().Contain("Compute");
    }
}
