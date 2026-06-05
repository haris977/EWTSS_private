using FluentAssertions;
using NUnit.Framework;
using Sg.Domain.Interaction;
using Sg.Domain.Models;
using Sg.Tests.Fakes;

namespace Sg.Tests.Interaction;

[TestFixture, Category(TestCategories.Unit)]
public class InteractionControllerTests
{
    private FakeScenarioBackend _svc = null!;
    private InteractionController _c = null!;

    [SetUp]
    public void SetUp()
    {
        _svc = new FakeScenarioBackend();
        _svc.NewScenario("T", DateTime.UtcNow, DateTime.UtcNow.AddHours(1));
        _c   = new InteractionController(_svc);
    }

    [Test]
    public void Starts_in_Idle()
    {
        _c.Mode.Should().Be(InteractionMode.Idle);
        _c.EditingPath.Should().BeNull();
    }

    [Test]
    public void BeginPlace_Facility_enters_PlacingFacility_with_helpful_hint()
    {
        _c.BeginPlace(EntityKind.Facility);
        _c.Mode.Should().Be(InteractionMode.PlacingFacility);
        _c.StatusHint.Should().Contain("facility");
    }

    [Test]
    public void Facility_placement_creates_entity_at_click_location_then_returns_to_Idle()
    {
        _c.BeginPlace(EntityKind.Facility);
        _c.OnMapMouseDown(34.2, 74.5, 0.0, isValidPick: true);

        _svc.Children.Should().ContainSingle(n => n.Kind == EntityKind.Facility);
        _c.Mode.Should().Be(InteractionMode.Idle);
    }

    [Test]
    public void Facility_placement_with_invalid_pick_stays_in_placement_mode()
    {
        _c.BeginPlace(EntityKind.Facility);
        _c.OnMapMouseDown(0, 0, 0, isValidPick: false);
        _c.Mode.Should().Be(InteractionMode.PlacingFacility);
        _svc.Children.Should().BeEmpty();
    }

    [Test]
    public void Aircraft_placement_collects_waypoints_until_dblclick_finalizes()
    {
        _c.BeginPlace(EntityKind.Aircraft);
        _c.OnMapMouseDown(34.2, 74.5, 1500, isValidPick: true);
        _c.OnMapMouseDown(36.7, 77.1, 1500, isValidPick: true);
        _c.OnMapMouseDblClick(38.0, 78.5, 1500, isValidPick: true);

        _svc.Children.Should().ContainSingle(n => n.Kind == EntityKind.Aircraft);
        _c.Mode.Should().Be(InteractionMode.Idle);
    }

    [Test]
    public void AreaTarget_placement_collects_vertices_until_dblclick_closes_polygon()
    {
        _c.BeginPlace(EntityKind.AreaTarget);
        _c.OnMapMouseDown(34.0, 74.0, 0, isValidPick: true);
        _c.OnMapMouseDown(36.0, 74.0, 0, isValidPick: true);
        _c.OnMapMouseDown(36.0, 76.0, 0, isValidPick: true);
        _c.OnMapMouseDblClick(34.0, 76.0, 0, isValidPick: true);

        _svc.Children.Should().ContainSingle(n => n.Kind == EntityKind.AreaTarget);
        _c.Mode.Should().Be(InteractionMode.Idle);
    }

    [Test]
    public void Cancel_during_Aircraft_placement_discards_partial_entity()
    {
        _c.BeginPlace(EntityKind.Aircraft);
        _c.OnMapMouseDown(34.2, 74.5, 1500, isValidPick: true);
        _c.Cancel();

        _c.Mode.Should().Be(InteractionMode.Idle);
        _svc.Children.Should().BeEmpty();
    }

    [Test]
    public void StartEditingOnMap_enters_EditingEntity_with_path()
    {
        _svc.AddEntity(EntityKind.Aircraft, "A1", null);
        _c.StartEditingOnMap("A1");
        _c.Mode.Should().Be(InteractionMode.EditingEntity);
        _c.EditingPath.Should().Be("A1");
    }

    [Test]
    public void ModeChanged_fires_on_every_transition()
    {
        var count = 0;
        _c.ModeChanged += (_, _) => count++;

        _c.BeginPlace(EntityKind.Facility);                                   // 1: Idle → PlacingFacility
        _c.OnMapMouseDown(10, 10, 0, isValidPick: true);                      // 2: PlacingFacility → Idle
        _c.StartEditingOnMap(_svc.Children.Single().Path);                    // 3: Idle → EditingEntity
        _c.CancelEdit();                                                       // 4: EditingEntity → Idle

        count.Should().Be(4);
    }

    [Test]
    public void Beginning_new_placement_while_in_placement_cancels_current()
    {
        _c.BeginPlace(EntityKind.Aircraft);
        _c.OnMapMouseDown(34, 74, 1500, isValidPick: true);
        _c.BeginPlace(EntityKind.Facility);   // should cancel the aircraft

        _c.Mode.Should().Be(InteractionMode.PlacingFacility);
        _svc.Children.Should().BeEmpty();   // aircraft was discarded
    }

    [Test]
    public void Cancel_while_Idle_does_not_fire_ModeChanged()
    {
        var count = 0;
        _c.ModeChanged += (_, _) => count++;

        _c.Cancel();   // already Idle — must be a no-op

        count.Should().Be(0);
        _c.Mode.Should().Be(InteractionMode.Idle);
    }
}
