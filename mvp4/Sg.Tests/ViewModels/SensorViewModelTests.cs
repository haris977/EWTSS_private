using FluentAssertions;
using NUnit.Framework;
using Sg.Domain.Contracts;
using Sg.Domain.Models;
using Sg.Domain.ViewModels;
using Sg.Tests.Fakes;

namespace Sg.Tests.ViewModels;

[TestFixture, Category(TestCategories.Unit)]
public class SensorViewModelTests
{
    private static SensorDto DefaultSeed(string name = "S1") => new(
        Name:              name,
        ParentPath:        "Aircraft1",
        ColorHex:          "#FFFFFF",
        ShowIntersection:  true,
        OuterHalfAngleDeg: 0,
        InnerHalfAngleDeg: 0,
        Pointing:          PointingMode.Fixed,
        FixedAzimuthDeg:   0,
        FixedElevationDeg: 0,
        TargetPath:        null);

    private static (FakeScenarioBackend, SensorViewModel) Setup(SensorDto seed)
    {
        var back = new FakeScenarioBackend();
        var path = $"{seed.ParentPath}/{seed.Name}";
        back.Sensors[path] = seed;
        return (back, new SensorViewModel(back, path));
    }

    [Test]
    public void EntityName_returns_dto_name()
    {
        var (_, vm) = Setup(DefaultSeed("Sensor1"));
        vm.EntityName.Should().Be("Sensor1");
    }

    [Test]
    public void OuterHalfAngle_change_marks_dirty()
    {
        var (_, vm) = Setup(DefaultSeed());
        vm.OuterHalfAngleDeg = 30.0;
        vm.IsDirty.Should().BeTrue();
    }

    [Test]
    public void Apply_writes_outer_half_angle_back()
    {
        var (back, vm) = Setup(DefaultSeed() with { OuterHalfAngleDeg = 15.0 });
        vm.OuterHalfAngleDeg = 45.0;
        vm.ApplyCommand.Execute(null);
        back.Sensors["Aircraft1/S1"].OuterHalfAngleDeg.Should().Be(45.0);
        vm.IsDirty.Should().BeFalse();
    }

    [Test]
    public void Pointing_mode_change_marks_dirty()
    {
        var (_, vm) = Setup(DefaultSeed() with { Pointing = PointingMode.Fixed });
        vm.Pointing = PointingMode.Targeted;
        vm.IsDirty.Should().BeTrue();
    }

    [Test]
    public void Reset_discards_unapplied_changes()
    {
        var (_, vm) = Setup(DefaultSeed() with { OuterHalfAngleDeg = 15.0, ColorHex = "#00FFFF" });
        vm.OuterHalfAngleDeg = 90.0;
        vm.ColorHex          = "#FF0000";
        vm.ResetCommand.Execute(null);
        vm.OuterHalfAngleDeg.Should().Be(15.0);
        vm.ColorHex.Should().Be("#00FFFF");
        vm.IsDirty.Should().BeFalse();
    }

    [Test]
    public void TargetPath_change_marks_dirty()
    {
        var (_, vm) = Setup(DefaultSeed());
        vm.TargetPath = "Aircraft/Bird1";
        vm.IsDirty.Should().BeTrue();
    }

    [Test]
    public void Apply_writes_all_fields_back_to_backend()
    {
        var (back, vm) = Setup(DefaultSeed());
        vm.OuterHalfAngleDeg = 20.0;
        vm.InnerHalfAngleDeg = 5.0;
        vm.ColorHex          = "#FF8800";
        vm.ShowIntersection  = false;
        vm.Pointing          = PointingMode.Targeted;
        vm.TargetPath        = "Aircraft/Target1";
        vm.ApplyCommand.Execute(null);

        var saved = back.Sensors["Aircraft1/S1"];
        saved.OuterHalfAngleDeg.Should().Be(20.0);
        saved.InnerHalfAngleDeg.Should().Be(5.0);
        saved.ColorHex.Should().Be("#FF8800");
        saved.ShowIntersection.Should().BeFalse();
        saved.Pointing.Should().Be(PointingMode.Targeted);
        saved.TargetPath.Should().Be("Aircraft/Target1");
        vm.IsDirty.Should().BeFalse();
    }
}
