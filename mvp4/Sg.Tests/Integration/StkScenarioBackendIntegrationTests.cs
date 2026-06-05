using FluentAssertions;
using NUnit.Framework;
using Sg.Domain.Contracts;
using Sg.Domain.Models;
using Sg.Domain.Stk;

namespace Sg.Tests.Integration;

[TestFixture, Category(TestCategories.Integration)]
[Apartment(System.Threading.ApartmentState.STA)]
public class StkScenarioBackendIntegrationTests
{
    private StkScenarioBackend _svc = null!;

    [SetUp]
    public void SetUp()
    {
        _svc = StkBackendFixture.Shared;
        _svc.CloseScenario();
    }

    [Test]
    public void NewScenario_creates_empty_scenario_with_time_window()
    {
        var start = new DateTime(2026, 4, 15, 6, 30, 0, DateTimeKind.Utc);
        var stop  = new DateTime(2026, 4, 15, 8, 46, 0, DateTimeKind.Utc);

        _svc.NewScenario("IntegTest", start, stop);

        _svc.ScenarioName.Should().Be("IntegTest");
        _svc.Children.Should().BeEmpty();
    }

    [Test]
    public void AddEntity_aircraft_then_GetAircraft_round_trips_dto()
    {
        _svc.NewScenario("T", DateTime.UtcNow, DateTime.UtcNow.AddHours(1));
        _svc.AddEntity(EntityKind.Aircraft, "A1", null);
        _svc.UpdateAircraft("A1", new AircraftDto("A1", "#FF0000", true,
            new[] { new WaypointDto(34.2, 74.5, 1500, 100),
                    new WaypointDto(36.7, 77.1, 1500, 100) }));

        var got = _svc.GetAircraft("A1");
        got.ColorHex.Should().Be("#FF0000");
        got.Waypoints.Should().HaveCount(2);
    }

    [Test]
    public void AddEntity_sensor_under_aircraft_creates_child()
    {
        _svc.NewScenario("T", DateTime.UtcNow, DateTime.UtcNow.AddHours(1));
        var ac = _svc.AddEntity(EntityKind.Aircraft, "A1", null);
        _svc.AddEntity(EntityKind.Sensor, "S1", ac.Path);

        ac = _svc.FindByPath("A1")!;
        ac.Children.Should().ContainSingle(c => c.Name == "S1");
    }
}
