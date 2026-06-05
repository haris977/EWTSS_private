using System.Text.Json;
using FluentAssertions;
using NUnit.Framework;
using Sg.Domain.Contracts;
using Sg.Domain.Models;

namespace Sg.Tests.Contracts;

[TestFixture, Category(TestCategories.Unit)]
public class DtoJsonRoundTripTests
{
    private static T RoundTrip<T>(T value) =>
        JsonSerializer.Deserialize<T>(JsonSerializer.Serialize(value))!;

    [Test]
    public void AircraftDto_round_trips()
    {
        var dto = new AircraftDto("A1", "#FF0000", true,
            new[] { new WaypointDto(34.2, 74.5, 1500, 100) });
        RoundTrip(dto).Should().BeEquivalentTo(dto);
    }

    [Test]
    public void FacilityDto_round_trips()
    {
        var dto = new FacilityDto("F1", "#00FF00", 12.34, 56.78, 100);
        RoundTrip(dto).Should().BeEquivalentTo(dto);
    }

    [Test]
    public void AreaTargetDto_round_trips()
    {
        var dto = new AreaTargetDto("AT1", "#FF0000", true, "#FF8888",
            new[] { new VertexDto(10, 20), new VertexDto(11, 21), new VertexDto(12, 22) });
        RoundTrip(dto).Should().BeEquivalentTo(dto);
    }

    [Test]
    public void SensorDto_round_trips()
    {
        var dto = new SensorDto("S1", "A1", "#00FFFF", true, 5.0, 0.0,
            PointingMode.Targeted, 90, 10, "*/Aircraft/A2");
        RoundTrip(dto).Should().BeEquivalentTo(dto);
    }

    [Test]
    public void CoverageDefinitionDto_round_trips()
    {
        var dto = new CoverageDefinitionDto("Cov1", -10, 10, -20, 20, 0.5, 0.5, 0, 5,
            new[] { "Aircraft1", "Facility1" });
        RoundTrip(dto).Should().BeEquivalentTo(dto);
    }

    [Test]
    public void FigureOfMeritDto_round_trips()
    {
        var dto = new FigureOfMeritDto("FOM1", "Cov1", FomKind.AccessDuration, FomStatistic.Average);
        RoundTrip(dto).Should().BeEquivalentTo(dto);
    }

    [Test]
    public void EntityNodeDto_round_trips()
    {
        var dto = new EntityNodeDto(EntityKind.Aircraft, "A1", "A1",
            new[] { new EntityNodeDto(EntityKind.Sensor, "S1", "A1/S1",
                       Array.Empty<EntityNodeDto>()) });
        RoundTrip(dto).Should().BeEquivalentTo(dto);
    }

    [Test]
    public void ComputeResultDto_round_trips()
    {
        var dto = new ComputeResultDto(true, null, new DateTime(2026, 5, 1, 12, 0, 0, DateTimeKind.Utc));
        RoundTrip(dto).Should().BeEquivalentTo(dto);
    }
}
