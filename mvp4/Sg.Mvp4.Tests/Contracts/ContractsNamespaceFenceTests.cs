using FluentAssertions;
using NUnit.Framework;

namespace Sg.Mvp4.Tests.Contracts;

[TestFixture, Category(TestCategories.Unit)]
public class ContractsNamespaceFenceTests
{
    [Test]
    public void Contracts_namespace_must_not_import_AGI_types()
    {
        var asmDir = AppContext.BaseDirectory;
        var dir = new DirectoryInfo(asmDir);
        while (dir is not null && !File.Exists(Path.Combine(dir.FullName, "Sg.Mvp4.sln")))
            dir = dir.Parent;
        dir.Should().NotBeNull("solution root should be found by walking up from test bin folder");
        var contractsDir = Path.Combine(dir!.FullName, "Sg.Mvp4.Domain", "Contracts");
        Directory.Exists(contractsDir).Should().BeTrue();

        var violations = Directory.EnumerateFiles(contractsDir, "*.cs", SearchOption.AllDirectories)
            .SelectMany(file => File.ReadAllLines(file)
                .Select((line, idx) => (file, line, idx))
                .Where(t => t.line.TrimStart().StartsWith("using AGI.")))
            .ToList();

        violations.Should().BeEmpty(
            $"Sg.Mvp4.Domain.Contracts.* must not reference AGI.* (found: {string.Join(", ", violations.Select(v => $"{v.file}:{v.idx + 1}"))})");
    }
}
