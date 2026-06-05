using FluentAssertions;
using NUnit.Framework;
using Sg.Mvp4.Domain.Contracts;
using Sg.Mvp4.Domain.Models;
using Sg.Mvp4.Domain.ViewModels;
using Sg.Mvp4.Tests.Fakes;

namespace Sg.Mvp4.Tests.ViewModels;

[TestFixture, Category(TestCategories.Unit)]
public class FigureOfMeritViewModelTests
{
    private static (FakeScenarioBackend, FigureOfMeritViewModel) Setup(FigureOfMeritDto seed)
    {
        var back = new FakeScenarioBackend();
        var path = $"{seed.ParentPath}/{seed.Name}";
        back.Foms[path] = seed;
        return (back, new FigureOfMeritViewModel(back, path));
    }

    [Test]
    public void EntityName_returns_dto_name()
    {
        var (_, vm) = Setup(new FigureOfMeritDto("FOM1", "Cov1", FomKind.AccessDuration, FomStatistic.Maximum));
        vm.EntityName.Should().Be("FOM1");
    }

    [Test]
    public void Kind_change_marks_dirty()
    {
        var (_, vm) = Setup(new FigureOfMeritDto("FOM1", "Cov1", FomKind.AccessDuration, FomStatistic.Maximum));
        vm.Kind = FomKind.RevisitTime;
        vm.IsDirty.Should().BeTrue();
    }

    [Test]
    public void Statistic_change_marks_dirty()
    {
        var (_, vm) = Setup(new FigureOfMeritDto("FOM1", "Cov1", FomKind.AccessDuration, FomStatistic.Maximum));
        vm.Statistic = FomStatistic.Minimum;
        vm.IsDirty.Should().BeTrue();
    }

    [Test]
    public void Apply_writes_kind_and_statistic_back_to_backend()
    {
        var (back, vm) = Setup(new FigureOfMeritDto("FOM1", "Cov1", FomKind.AccessDuration, FomStatistic.Maximum));
        vm.Kind      = FomKind.Coverage;
        vm.Statistic = FomStatistic.Average;
        vm.ApplyCommand.Execute(null);

        var saved = back.Foms["Cov1/FOM1"];
        saved.Kind.Should().Be(FomKind.Coverage);
        saved.Statistic.Should().Be(FomStatistic.Average);
    }

    [Test]
    public void Reset_discards_unapplied_changes()
    {
        var (_, vm) = Setup(new FigureOfMeritDto("FOM1", "Cov1", FomKind.RevisitTime, FomStatistic.Minimum));
        vm.Kind      = FomKind.SampleCount;
        vm.Statistic = FomStatistic.Total;
        vm.ResetCommand.Execute(null);
        vm.Kind.Should().Be(FomKind.RevisitTime);
        vm.Statistic.Should().Be(FomStatistic.Minimum);
        vm.IsDirty.Should().BeFalse();
    }
}
