using FluentAssertions;
using NUnit.Framework;
using Sg.Mvp4.Domain.Contracts;
using Sg.Mvp4.Domain.Models;
using Sg.Mvp4.Domain.ViewModels;
using Sg.Mvp4.Tests.Fakes;

namespace Sg.Mvp4.Tests.ViewModels;

[TestFixture, Category(TestCategories.Unit)]
public class AreaTargetViewModelTests
{
    private static (FakeScenarioBackend, AreaTargetViewModel) Setup(AreaTargetDto seed)
    {
        var back = new FakeScenarioBackend();
        back.AreaTargets[seed.Name] = seed;
        return (back, new AreaTargetViewModel(back, seed.Name));
    }

    [Test]
    public void EntityName_returns_dto_name()
    {
        var (_, vm) = Setup(new AreaTargetDto("AT1", "#FFFFFF", true, "#80808080", Array.Empty<VertexDto>()));
        vm.EntityName.Should().Be("AT1");
    }

    [Test]
    public void AddVertex_marks_dirty()
    {
        var (_, vm) = Setup(new AreaTargetDto("AT1", "#FFFFFF", true, "#80808080", Array.Empty<VertexDto>()));
        vm.AddVertexCommand.Execute(null);
        vm.IsDirty.Should().BeTrue();
    }

    [Test]
    public void Apply_writes_full_dto()
    {
        var (back, vm) = Setup(new AreaTargetDto("AT1", "#FFFFFF", true, "#80808080", Array.Empty<VertexDto>()));
        vm.Vertices.Add(new VertexModel { LatitudeDeg = 34.2, LongitudeDeg = 74.5 });
        vm.Vertices.Add(new VertexModel { LatitudeDeg = 35.0, LongitudeDeg = 75.0 });
        vm.MarkDirty();
        vm.ApplyCommand.Execute(null);
        back.AreaTargets["AT1"].Vertices.Should().HaveCount(2);
        back.AreaTargets["AT1"].Vertices[0].LatitudeDeg.Should().Be(34.2);
        back.AreaTargets["AT1"].Vertices[1].LongitudeDeg.Should().Be(75.0);
    }

    [Test]
    public void OutlineColorHex_change_marks_dirty()
    {
        var (_, vm) = Setup(new AreaTargetDto("AT1", "#FFFFFF", true, "#80808080", Array.Empty<VertexDto>()));
        vm.OutlineColorHex = "#FF0000";
        vm.IsDirty.Should().BeTrue();
    }

    [Test]
    public void Reset_reverts_to_committed()
    {
        var (_, vm) = Setup(new AreaTargetDto("AT1", "#00FFFF", true, "#80808080", Array.Empty<VertexDto>()));
        vm.Vertices.Add(new VertexModel { LatitudeDeg = 99.0, LongitudeDeg = 99.0 });
        vm.OutlineColorHex = "#FF0000";
        vm.ResetCommand.Execute(null);
        vm.OutlineColorHex.Should().Be("#00FFFF");
        vm.Vertices.Should().BeEmpty();
        vm.IsDirty.Should().BeFalse();
    }

    [Test]
    public void FillEnabled_change_marks_dirty()
    {
        var (_, vm) = Setup(new AreaTargetDto("AT1", "#FFFFFF", false, "#80808080", Array.Empty<VertexDto>()));
        vm.FillEnabled = true;
        vm.IsDirty.Should().BeTrue();
    }
}
