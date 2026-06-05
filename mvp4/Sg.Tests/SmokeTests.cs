using FluentAssertions;
using NUnit.Framework;

namespace Sg.Tests;

[TestFixture]
public class SmokeTests
{
    [Test]
    public void Domain_assembly_loads()
    {
        var asm = typeof(Sg.Domain.Models.EntityKind).Assembly;
        asm.Should().NotBeNull();
        asm.GetName().Name.Should().Be("Sg.Domain");
    }
}
