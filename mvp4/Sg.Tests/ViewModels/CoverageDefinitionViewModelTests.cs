using FluentAssertions;
using NUnit.Framework;
using Sg.Domain.Contracts;
using Sg.Domain.Models;
using Sg.Domain.ViewModels;
using Sg.Tests.Fakes;

namespace Sg.Tests.ViewModels;

[TestFixture, Category(TestCategories.Unit)]
public class CoverageDefinitionViewModelTests
{
    private static CoverageDefinitionDto DefaultSeed(string name = "Cv1") =>
        new(name, 0, 0, 0, 0, 1, 1, 0, 0, Array.Empty<string>());

    private static (FakeScenarioBackend, CoverageDefinitionViewModel) Setup(CoverageDefinitionDto seed)
    {
        var back = new FakeScenarioBackend();
        back.Coverages[seed.Name] = seed;
        return (back, new CoverageDefinitionViewModel(back, seed.Name));
    }

    // -----------------------------------------------------------------------
    // Test 1 – EntityName returns the backend name
    // -----------------------------------------------------------------------
    [Test]
    public void EntityName_returns_backend_name()
    {
        var (_, vm) = Setup(DefaultSeed("Coverage1"));
        vm.EntityName.Should().Be("Coverage1");
    }

    // -----------------------------------------------------------------------
    // Test 2 – LatMin change marks dirty
    // -----------------------------------------------------------------------
    [Test]
    public void LatMin_change_marks_dirty()
    {
        var (_, vm) = Setup(DefaultSeed());
        vm.LatMin = 10.0;
        vm.IsDirty.Should().BeTrue();
    }

    // -----------------------------------------------------------------------
    // Test 3 – Apply writes all grid bounds back to the backend
    // -----------------------------------------------------------------------
    [Test]
    public void Apply_writes_grid_bounds_back()
    {
        var (back, vm) = Setup(DefaultSeed());

        vm.LatMin = -45.0;
        vm.LatMax =  45.0;
        vm.LonMin = -90.0;
        vm.LonMax =  90.0;
        vm.LatStep = 5.0;
        vm.LonStep = 5.0;
        vm.ApplyCommand.Execute(null);

        var saved = back.Coverages["Cv1"];
        saved.LatMin.Should().Be(-45.0);
        saved.LatMax.Should().Be( 45.0);
        saved.LonMin.Should().Be(-90.0);
        saved.LonMax.Should().Be( 90.0);
        saved.LatStep.Should().Be(5.0);
        saved.LonStep.Should().Be(5.0);
        vm.IsDirty.Should().BeFalse();
    }

    // -----------------------------------------------------------------------
    // Test 4 – Apply writes asset paths back to the backend
    // -----------------------------------------------------------------------
    [Test]
    public void Apply_writes_asset_paths_back()
    {
        var (back, vm) = Setup(DefaultSeed());

        vm.AssetPaths.Add("Aircraft1");
        vm.AssetPaths.Add("Facility1");
        vm.IsDirty.Should().BeTrue();

        vm.ApplyCommand.Execute(null);

        back.Coverages["Cv1"].AssetPaths.Should().BeEquivalentTo(new[] { "Aircraft1", "Facility1" });
        vm.IsDirty.Should().BeFalse();
    }

    // -----------------------------------------------------------------------
    // Test 5 – Reset discards unapplied changes
    // -----------------------------------------------------------------------
    [Test]
    public void Reset_discards_unapplied_changes()
    {
        var (_, vm) = Setup(DefaultSeed() with { LatMin = -30.0, LatMax = 30.0 });

        vm.LatMin = -99.0;
        vm.LatMax =  99.0;
        vm.ResetCommand.Execute(null);

        vm.LatMin.Should().Be(-30.0);
        vm.LatMax.Should().Be( 30.0);
        vm.IsDirty.Should().BeFalse();
    }

    // -----------------------------------------------------------------------
    // Test 6 – AssetPaths collection change marks dirty
    // -----------------------------------------------------------------------
    [Test]
    public void AssetPaths_change_via_collection_marks_dirty()
    {
        var (_, vm) = Setup(DefaultSeed());
        vm.IsDirty.Should().BeFalse();
        vm.AssetPaths.Add("Aircraft1");
        vm.IsDirty.Should().BeTrue();
    }

    // -----------------------------------------------------------------------
    // Test 7 – AvailableAssetPaths reflects scenario Aircraft and Facility nodes
    // -----------------------------------------------------------------------
    [Test]
    public void AvailableAssetPaths_includes_aircraft_and_facility()
    {
        var back = new FakeScenarioBackend();
        back.NewScenario("S", DateTime.UtcNow, DateTime.UtcNow.AddHours(1));
        back.AddEntity(EntityKind.Aircraft,   "Bird1", null);
        back.AddEntity(EntityKind.Facility,   "Site1", null);
        back.AddEntity(EntityKind.AreaTarget, "Zone1", null);   // should NOT appear

        back.Coverages["Cv1"] = DefaultSeed();
        var vm = new CoverageDefinitionViewModel(back, "Cv1");

        vm.AvailableAssetPaths.Should().Contain("Bird1");
        vm.AvailableAssetPaths.Should().Contain("Site1");
        vm.AvailableAssetPaths.Should().NotContain("Zone1");
    }

    // -----------------------------------------------------------------------
    // Test 8 – Constraints are applied and reset correctly
    // -----------------------------------------------------------------------
    [Test]
    public void Apply_writes_constraints_back()
    {
        var (back, vm) = Setup(DefaultSeed());

        vm.MinAltitude       = 500.0;
        vm.MinElevationAngle = 10.0;
        vm.ApplyCommand.Execute(null);

        var saved = back.Coverages["Cv1"];
        saved.MinAltitude.Should().Be(500.0);
        saved.MinElevationAngle.Should().Be(10.0);
        vm.IsDirty.Should().BeFalse();
    }
}
