using FluentAssertions;
using NUnit.Framework;
using Sg.Mvp4.Domain.Contracts;
using Sg.Mvp4.Domain.Models;
using Sg.Mvp4.Domain.ViewModels;
using Sg.Mvp4.Tests.Fakes;

namespace Sg.Mvp4.Tests.ViewModels;

[TestFixture, Category(TestCategories.Unit)]
public class AircraftViewModelTests
{
    private static (FakeScenarioBackend, AircraftViewModel) Setup(AircraftDto seed)
    {
        var back = new FakeScenarioBackend();
        back.Aircraft[seed.Name] = seed;
        var vm = new AircraftViewModel(back, seed.Name);
        return (back, vm);
    }

    [Test]
    public void EntityName_returns_dto_name()
    {
        var (_, vm) = Setup(new AircraftDto("A1", "#00FFFF", true, Array.Empty<WaypointDto>()));
        vm.EntityName.Should().Be("A1");
    }

    [Test]
    public void Adding_waypoint_marks_dirty()
    {
        var (_, vm) = Setup(new AircraftDto("A1", "#00FFFF", true, Array.Empty<WaypointDto>()));
        vm.AddWaypointCommand.Execute(null);
        vm.IsDirty.Should().BeTrue();
    }

    [Test]
    public void ColorHex_change_marks_dirty()
    {
        var (_, vm) = Setup(new AircraftDto("A1", "#00FFFF", true, Array.Empty<WaypointDto>()));
        vm.ColorHex = "#FF00FF";
        vm.IsDirty.Should().BeTrue();
    }

    [Test]
    public void Apply_pushes_full_DTO_to_backend()
    {
        var (back, vm) = Setup(new AircraftDto("A1", "#00FFFF", true, Array.Empty<WaypointDto>()));
        vm.Waypoints.Add(new WaypointModel { LatitudeDeg = 34.2, LongitudeDeg = 74.5, AltitudeMeters = 1500, SpeedMps = 100 });
        vm.ColorHex = "#FF0000";
        vm.MarkDirty();

        vm.ApplyCommand.Execute(null);

        back.Aircraft["A1"].ColorHex.Should().Be("#FF0000");
        back.Aircraft["A1"].Waypoints.Should().HaveCount(1);
        back.Aircraft["A1"].Waypoints[0].LatitudeDeg.Should().Be(34.2);
    }

    [Test]
    public void Reset_reverts_to_committed()
    {
        var (_, vm) = Setup(new AircraftDto("A1", "#00FFFF", true, Array.Empty<WaypointDto>()));
        vm.ColorHex = "#FF00FF";
        vm.ResetCommand.Execute(null);
        vm.ColorHex.Should().Be("#00FFFF");
        vm.IsDirty.Should().BeFalse();
    }
}
