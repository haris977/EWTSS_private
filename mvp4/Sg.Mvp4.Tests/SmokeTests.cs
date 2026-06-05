using FluentAssertions;
using NUnit.Framework;

namespace Sg.Mvp4.Tests;

[TestFixture]
public class SmokeTests
{
    [Test]
    public void Domain_assembly_loads()
    {
        var asm = typeof(Sg.Mvp4.Domain.Models.EntityKind).Assembly;
        asm.Should().NotBeNull();
        asm.GetName().Name.Should().Be("Sg.Mvp4.Domain");
    }
}
