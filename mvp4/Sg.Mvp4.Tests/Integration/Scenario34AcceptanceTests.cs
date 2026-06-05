using FluentAssertions;
using NUnit.Framework;
using Sg.Mvp4.Domain.Models;
using Sg.Mvp4.Domain.Stk;

namespace Sg.Mvp4.Tests.Integration;

/// <summary>
/// End-to-end check that MVP4 can author a scenario whose topology matches the
/// reference Scenario34.czml the user provided during brainstorming.
/// </summary>
[TestFixture, Category(TestCategories.Integration)]
[Apartment(System.Threading.ApartmentState.STA)]
public class Scenario34AcceptanceTests
{
    [Test]
    public void Can_author_and_roundtrip_Scenario34_shape()
    {
        var stk = StkBackendFixture.Shared;
        stk.CloseScenario();

        var start = new DateTime(2026, 4, 15,  6, 30,  0, DateTimeKind.Utc);
        var stop  = new DateTime(2026, 4, 15,  8, 46, 52, DateTimeKind.Utc);
        stk.NewScenario("Scenario34", start, stop);

        var aircraft = stk.AddEntity(EntityKind.Aircraft,           "Aircraft1",          null);
        stk.AddEntity(EntityKind.Sensor,                            "Sensor1",            aircraft.Path);
        stk.AddEntity(EntityKind.Facility,                          "Facility1",          null);
        stk.AddEntity(EntityKind.AreaTarget,                        "AreaTarget1",        null);
        var coverage = stk.AddEntity(EntityKind.CoverageDefinition, "CoverageDefinition1", null);
        stk.AddEntity(EntityKind.FigureOfMerit,                     "FigureOfMerit1",     coverage.Path);

        // Topology: 4 root-level objects, plus nested children.
        stk.Children.Select(c => c.Name).Should().BeEquivalentTo(
            new[] { "Aircraft1", "Facility1", "AreaTarget1", "CoverageDefinition1" });
        stk.FindByPath("Aircraft1/Sensor1").Should().NotBeNull();
        stk.FindByPath("CoverageDefinition1/FigureOfMerit1").Should().NotBeNull();

        // Round-trip via .sc on the same backend (STK 12.9 disallows a second
        // AgSTKXApplication lifecycle in-process; CloseScenario+LoadSc gives the
        // same isolation guarantee against in-memory state without a re-init).
        var tmp = Path.Combine(Path.GetTempPath(), "Scenario34-accept-" + Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(tmp);
        var scPath = Path.Combine(tmp, "Scenario34.sc");
        try
        {
            stk.SaveAsSc(scPath);
            stk.CloseScenario();
            stk.LoadSc(scPath);
            stk.Children.Should().HaveCount(4);
            stk.FindByPath("Aircraft1/Sensor1").Should().NotBeNull();
        }
        finally
        {
            try { Directory.Delete(tmp, recursive: true); } catch { /* best-effort */ }
        }
    }
}
