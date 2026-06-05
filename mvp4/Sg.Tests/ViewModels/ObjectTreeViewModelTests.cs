using FluentAssertions;
using NUnit.Framework;
using Sg.Domain.Models;
using Sg.Domain.ViewModels;
using Sg.Tests.Fakes;

namespace Sg.Tests.ViewModels;

[TestFixture, Category(TestCategories.Unit)]
public class ObjectTreeViewModelTests
{
    [Test]
    public void Tree_mirrors_scenario_children_after_NewScenario()
    {
        var svc = new FakeScenarioBackend();
        var vm  = new ObjectTreeViewModel(svc);

        svc.NewScenario("S", DateTime.UtcNow, DateTime.UtcNow.AddHours(1));
        svc.AddEntity(EntityKind.Aircraft, "A1", null);
        svc.AddEntity(EntityKind.Facility, "F1", null);

        vm.Nodes.Should().HaveCount(2);
        vm.Nodes.Select(n => n.Name).Should().Contain(new[] { "A1", "F1" });
    }

    [Test]
    public void Nested_sensor_appears_under_aircraft_node()
    {
        var svc = new FakeScenarioBackend();
        var vm  = new ObjectTreeViewModel(svc);
        svc.NewScenario("S", DateTime.UtcNow, DateTime.UtcNow.AddHours(1));
        var ac = svc.AddEntity(EntityKind.Aircraft, "A1", null);
        svc.AddEntity(EntityKind.Sensor, "Sen1", ac.Path);

        var a = vm.Nodes.Single(n => n.Name == "A1");
        a.Children.Should().ContainSingle(n => n.Name == "Sen1" && n.Kind == EntityKind.Sensor);
    }

    [Test]
    public void AddChild_on_root_appends_new_entity()
    {
        var svc = new FakeScenarioBackend();
        var vm  = new ObjectTreeViewModel(svc);
        svc.NewScenario("S", DateTime.UtcNow, DateTime.UtcNow.AddHours(1));

        vm.AddChildCommand.Execute((EntityKind.Aircraft, (string?)null, "NewA"));

        vm.Nodes.Should().ContainSingle(n => n.Name == "NewA");
    }

    [Test]
    public void RemoveNode_removes_from_service()
    {
        var svc = new FakeScenarioBackend();
        var vm  = new ObjectTreeViewModel(svc);
        svc.NewScenario("S", DateTime.UtcNow, DateTime.UtcNow.AddHours(1));
        svc.AddEntity(EntityKind.Facility, "F1", null);

        vm.RemoveCommand.Execute("F1");

        vm.Nodes.Should().BeEmpty();
    }

    [Test]
    public void Selecting_node_updates_SelectedPath()
    {
        var svc = new FakeScenarioBackend();
        var vm  = new ObjectTreeViewModel(svc);
        svc.NewScenario("S", DateTime.UtcNow, DateTime.UtcNow.AddHours(1));
        svc.AddEntity(EntityKind.Aircraft, "A1", null);

        vm.SelectedNode = vm.Nodes.Single();

        vm.SelectedPath.Should().Be("A1");
    }
}
