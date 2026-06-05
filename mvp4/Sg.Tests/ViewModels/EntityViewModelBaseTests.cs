using FluentAssertions;
using NUnit.Framework;
using Sg.Domain.ViewModels;

namespace Sg.Tests.ViewModels;

[TestFixture, Category(TestCategories.Unit)]
public class EntityViewModelBaseTests
{
    private sealed class TestVm : EntityViewModelBase
    {
        public string Name { get; set; } = "Initial";
        public override string EntityName => Name;
        protected override void DoApply() => _applied = Name;
        protected override void DoReset() => Name = _applied ?? "Initial";
        private string? _applied;
    }

    [Test]
    public void IsDirty_starts_false_and_flips_on_MarkDirty()
    {
        var vm = new TestVm();
        vm.IsDirty.Should().BeFalse();
        vm.MarkDirty();
        vm.IsDirty.Should().BeTrue();
    }

    [Test]
    public void ApplyCommand_clears_IsDirty_when_dirty()
    {
        var vm = new TestVm();
        vm.MarkDirty();
        vm.ApplyCommand.Execute(null);
        vm.IsDirty.Should().BeFalse();
    }

    [Test]
    public void ApplyCommand_is_disabled_when_not_dirty()
    {
        var vm = new TestVm();
        vm.ApplyCommand.CanExecute(null).Should().BeFalse();
    }

    [Test]
    public void ResetCommand_is_disabled_when_not_dirty()
    {
        var vm = new TestVm();
        vm.ResetCommand.CanExecute(null).Should().BeFalse();
    }
}
