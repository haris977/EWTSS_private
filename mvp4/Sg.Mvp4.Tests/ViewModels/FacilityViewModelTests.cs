using FluentAssertions;
using NUnit.Framework;
using Sg.Mvp4.Domain.Contracts;
using Sg.Mvp4.Domain.ViewModels;
using Sg.Mvp4.Tests.Fakes;

namespace Sg.Mvp4.Tests.ViewModels;

[TestFixture, Category(TestCategories.Unit)]
public class FacilityViewModelTests
{
    private static (FakeScenarioBackend, FacilityViewModel) Setup(FacilityDto seed)
    {
        var back = new FakeScenarioBackend();
        back.Facilities[seed.Name] = seed;
        return (back, new FacilityViewModel(back, seed.Name));
    }

    [Test]
    public void EntityName_returns_dto_name()
    {
        var (_, vm) = Setup(new FacilityDto("F1", "#FF00FF", 0, 0, 0));
        vm.EntityName.Should().Be("F1");
    }

    [Test]
    public void Setting_lat_marks_dirty()
    {
        var (_, vm) = Setup(new FacilityDto("F1", "#FF00FF", 0, 0, 0));
        vm.LatitudeDeg = 10;
        vm.IsDirty.Should().BeTrue();
    }

    [Test]
    public void Apply_writes_full_dto()
    {
        var (back, vm) = Setup(new FacilityDto("F1", "#FF00FF", 0, 0, 0));
        vm.LatitudeDeg = 34.2; vm.LongitudeDeg = 74.5; vm.AltitudeMeters = 1200;
        vm.MarkDirty();
        vm.ApplyCommand.Execute(null);
        back.Facilities["F1"].LatitudeDeg.Should().Be(34.2);
        back.Facilities["F1"].LongitudeDeg.Should().Be(74.5);
        back.Facilities["F1"].AltitudeMeters.Should().Be(1200);
    }

    [Test]
    public void ColorHex_change_marks_dirty()
    {
        var (_, vm) = Setup(new FacilityDto("F1", "#FF00FF", 0, 0, 0));
        vm.ColorHex = "#00FF00";
        vm.IsDirty.Should().BeTrue();
    }

    [Test]
    public void Reset_reverts_to_committed()
    {
        var (_, vm) = Setup(new FacilityDto("F1", "#FF00FF", 1, 0, 0));
        vm.LatitudeDeg = 99;
        vm.ResetCommand.Execute(null);
        vm.LatitudeDeg.Should().Be(1);
        vm.IsDirty.Should().BeFalse();
    }
}
