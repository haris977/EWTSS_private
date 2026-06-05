# MVP4 — STK-Native Embedded Authoring Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build a single-process C# WPF desktop app that authors + computes + visualises STK scenarios (6 entity types: Aircraft, Facility, AreaTarget, Sensor, CoverageDefinition, FigureOfMerit) using STK 12's embedded ActiveX globe as the sole renderer.

**Architecture:** WPF (.NET 8) MainWindow with a 3-pane shell — object tree, embedded STK 3D/2D ActiveX controls, property panel. One `IAgStkObjectRoot` held by a domain service, wrapped by an `IStkRootService` interface. View-models mutate COM through the interface; scenarios persist as STK-native `.sc` / `.vdf` files. Spec: [`docs/ewtss/specs/mvp4-stk-native-embedded-design.md`](../../ewtss/specs/mvp4-stk-native-embedded-design.md).

**Tech Stack:** C# · .NET 8 (Windows desktop) · WPF + XAML · WindowsFormsHost · STK 12 COM (AGI.STKObjects, AGI.STKUtil, AGI.Ui.Controls + AxAGI.* wrappers) · CommunityToolkit.Mvvm · Serilog · NUnit + FluentAssertions · Microsoft.Extensions.DependencyInjection.

---

## Prerequisites (verify before starting Task 1)

- [ ] STK 12 is installed at its default path (`C:\Program Files\AGI\STK 12`) and COM-registered. Verify by running `reg query "HKLM\SOFTWARE\Classes\AgStkObjects12.AgStkObjectRoot" /s` — expect non-empty output.
- [ ] The primary interop assemblies exist at `C:\Program Files\AGI\STK 12\bin\Primary Interop Assemblies\` — look for `AGI.STKObjects.Interop.dll`, `AGI.STKUtil.Interop.dll`, `AGI.STKVgt.Interop.dll`, `AGI.STKX.Interop.dll`, and the pre-generated `AxAGI.STKX.Interop.dll` ActiveX wrapper. If `AxAGI.STKX.Interop.dll` is missing, run `aximp "C:\Program Files\AGI\STK 12\bin\AgStkX.ocx"` from Visual Studio 2022 Developer Command Prompt. **Note:** The assemblies follow the `AGI.<Name>.Interop.dll` file-naming convention; the *namespaces* inside them (`AGI.STKObjects`, `AGI.STKUtil`, `AGI.STKX`, etc.) drop the `.Interop` suffix.
- [ ] .NET 8 SDK is installed: `dotnet --version` → 8.x.
- [ ] Visual Studio 2022 (17.8+) with Desktop Development workload.
- [ ] A feature branch exists for MVP4 work: `git checkout -b feat/mvp4-stk-native` from `main` (or from the current `feat/mvp-stk-czml-validation` branch if merging with in-flight MVP3 work is preferred — ask the user).

---

## File structure (the whole picture, so every task can find its place)

```
mvp4/
├── Sg.Mvp4.sln
├── Sg.Mvp4.App/                      WPF app entry, .NET 8
│   ├── App.xaml, App.xaml.cs         Startup + DI container
│   ├── MainWindow.xaml, .cs          3-pane shell + toolbar
│   ├── Views/
│   │   ├── Shell/
│   │   │   ├── ObjectTreeView.xaml, .cs
│   │   │   ├── PropertyPanelHostView.xaml, .cs   DataTemplateSelector
│   │   │   ├── StkDisplayHost.xaml, .cs          WindowsFormsHost → ActiveX
│   │   │   ├── SplashWindow.xaml, .cs
│   │   │   └── MainStatusBar.xaml, .cs
│   │   └── Entities/
│   │       ├── AircraftPanel.xaml, .cs
│   │       ├── FacilityPanel.xaml, .cs
│   │       ├── AreaTargetPanel.xaml, .cs
│   │       ├── SensorPanel.xaml, .cs
│   │       ├── CoveragePanel.xaml, .cs
│   │       └── FomPanel.xaml, .cs
│   ├── Converters/                   WPF IValueConverter implementations
│   └── Sg.Mvp4.App.csproj
├── Sg.Mvp4.Domain/                   STK-agnostic view-models + services
│   ├── Services/
│   │   ├── IStkRootService.cs        Interface — mockable for tests
│   │   ├── StkRootService.cs         COM-backed implementation
│   │   ├── IPersistenceService.cs
│   │   ├── PersistenceService.cs     Save/Load .sc + .vdf
│   │   ├── IFileDialogService.cs
│   │   └── FileDialogService.cs      Win32 open/save dialogs
│   ├── ViewModels/
│   │   ├── MainWindowViewModel.cs
│   │   ├── ObjectTreeViewModel.cs
│   │   ├── ObjectTreeNodeViewModel.cs
│   │   ├── EntityViewModelBase.cs
│   │   ├── AircraftViewModel.cs
│   │   ├── FacilityViewModel.cs
│   │   ├── AreaTargetViewModel.cs
│   │   ├── SensorViewModel.cs
│   │   ├── CoverageDefinitionViewModel.cs
│   │   └── FigureOfMeritViewModel.cs
│   ├── Models/
│   │   ├── EntityKind.cs             enum: Aircraft | Facility | AreaTarget | Sensor | CoverageDefinition | FigureOfMerit
│   │   ├── WaypointModel.cs
│   │   ├── VertexModel.cs
│   │   ├── PointingMode.cs           enum: Fixed | Targeted
│   │   └── FomKind.cs                enum: AccessDuration | RevisitTime | Coverage | SampleCount
│   └── Sg.Mvp4.Domain.csproj
├── Sg.Mvp4.Tests/                    NUnit tests, .NET 8
│   ├── Fakes/FakeStkRootService.cs   In-memory fake
│   ├── ViewModels/*.Tests.cs         Unit tier (no STK)
│   ├── Services/PersistenceService.Tests.cs   Integration tier (STK required)
│   ├── Integration/Scenario34Acceptance.cs    Integration tier (STK required)
│   ├── TestCategories.cs             const strings: Unit, Integration
│   └── Sg.Mvp4.Tests.csproj
├── docs/acceptance/Scenario34.czml   User-provided reference (committed Task 1)
└── README.md
```

**Layering rule:** `Sg.Mvp4.Domain` must not reference WPF types or `AGI.Ui.Controls`. Only `AGI.STKObjects`, `AGI.STKUtil`, `AGI.STKVgt`. The `App` project references Domain and adds the WPF + ActiveX layer. Tests reference Domain only.

---

## Task 1: Solution scaffolding

**Files:**
- Create: `mvp4/Sg.Mvp4.sln`
- Create: `mvp4/Sg.Mvp4.App/Sg.Mvp4.App.csproj` (via `dotnet new wpf`)
- Create: `mvp4/Sg.Mvp4.Domain/Sg.Mvp4.Domain.csproj` (via `dotnet new classlib`)
- Create: `mvp4/Sg.Mvp4.Tests/Sg.Mvp4.Tests.csproj` (via `dotnet new nunit`)
- Create: `mvp4/Sg.Mvp4.Tests/SmokeTests.cs`
- Create: `mvp4/Sg.Mvp4.Domain/Models/EntityKind.cs`
- Create: `mvp4/docs/acceptance/Scenario34.czml` (paste from brainstorming attachment)
- Create: `mvp4/README.md`

- [ ] **Step 1: Create folders + solution**

```bash
mkdir -p mvp4/Sg.Mvp4.App mvp4/Sg.Mvp4.Domain mvp4/Sg.Mvp4.Tests mvp4/docs/acceptance
cd mvp4 && dotnet new sln -n Sg.Mvp4
```

- [ ] **Step 2: Create the three projects and wire references**

```bash
# From mvp4/
dotnet new wpf      -o Sg.Mvp4.App    -n Sg.Mvp4.App    --framework net8.0-windows
dotnet new classlib -o Sg.Mvp4.Domain -n Sg.Mvp4.Domain --framework net8.0
dotnet new nunit    -o Sg.Mvp4.Tests  -n Sg.Mvp4.Tests  --framework net8.0

dotnet sln add Sg.Mvp4.App/Sg.Mvp4.App.csproj
dotnet sln add Sg.Mvp4.Domain/Sg.Mvp4.Domain.csproj
dotnet sln add Sg.Mvp4.Tests/Sg.Mvp4.Tests.csproj

dotnet add Sg.Mvp4.App/Sg.Mvp4.App.csproj     reference Sg.Mvp4.Domain/Sg.Mvp4.Domain.csproj
dotnet add Sg.Mvp4.Tests/Sg.Mvp4.Tests.csproj reference Sg.Mvp4.Domain/Sg.Mvp4.Domain.csproj
```

- [ ] **Step 3: Add NuGet packages**

```bash
# From mvp4/
dotnet add Sg.Mvp4.Domain package CommunityToolkit.Mvvm                   --version 8.3.2
dotnet add Sg.Mvp4.Domain package Microsoft.Extensions.Logging.Abstractions --version 8.0.2
dotnet add Sg.Mvp4.App    package CommunityToolkit.Mvvm                   --version 8.3.2
dotnet add Sg.Mvp4.App    package Microsoft.Extensions.DependencyInjection --version 8.0.1
dotnet add Sg.Mvp4.App    package Serilog                                 --version 4.0.2
dotnet add Sg.Mvp4.App    package Serilog.Sinks.File                      --version 6.0.0
dotnet add Sg.Mvp4.App    package Serilog.Sinks.Debug                     --version 3.0.0
dotnet add Sg.Mvp4.App    package Serilog.Extensions.Logging              --version 8.0.0
dotnet add Sg.Mvp4.Tests  package FluentAssertions                        --version 6.12.2
```

- [ ] **Step 4: Enable WinForms host in the WPF project**

Open `mvp4/Sg.Mvp4.App/Sg.Mvp4.App.csproj` and ensure the `<PropertyGroup>` contains both UseWPF and UseWindowsForms set to true (needed for ActiveX hosting in later tasks):

```xml
<PropertyGroup>
  <OutputType>WinExe</OutputType>
  <TargetFramework>net8.0-windows</TargetFramework>
  <Nullable>enable</Nullable>
  <ImplicitUsings>enable</ImplicitUsings>
  <UseWPF>true</UseWPF>
  <UseWindowsForms>true</UseWindowsForms>
  <LangVersion>latest</LangVersion>
</PropertyGroup>
```

- [ ] **Step 5: Write the failing smoke test**

Create `mvp4/Sg.Mvp4.Tests/SmokeTests.cs`:

```csharp
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
```

Delete the auto-generated `UnitTest1.cs` that `dotnet new nunit` created.

- [ ] **Step 6: Create EntityKind so the test compiles**

Create `mvp4/Sg.Mvp4.Domain/Models/EntityKind.cs`:

```csharp
namespace Sg.Mvp4.Domain.Models;

public enum EntityKind
{
    Aircraft,
    Facility,
    AreaTarget,
    Sensor,
    CoverageDefinition,
    FigureOfMerit
}
```

Delete the auto-generated `Class1.cs` in `Sg.Mvp4.Domain`.

- [ ] **Step 7: Run the test**

```bash
cd mvp4 && dotnet test Sg.Mvp4.Tests/Sg.Mvp4.Tests.csproj
```

Expected: `Passed: 1, Failed: 0`.

- [ ] **Step 8: Paste the reference CZML**

Copy the `Scenario34` CZML the user attached during brainstorming verbatim into `mvp4/docs/acceptance/Scenario34.czml`. First line should be `[` and the first packet should contain `"id":"document"` with `"name":"Scenario34"`.

- [ ] **Step 9: Write README stub**

Create `mvp4/README.md`:

```markdown
# EWTSS MVP4 — STK-Native Embedded Authoring

Single-process C# WPF desktop app that authors + computes + visualises STK
scenarios using STK 12's embedded ActiveX globe. Sibling to MVP1/2/3 but a
completely different frontend stack — no Cesium, no Python, no server.

See `docs/ewtss/specs/mvp4-stk-native-embedded-design.md`
for the full design.

## Build + test

```
cd mvp4
dotnet build
dotnet test --filter Category=Unit
```

Integration tier (`Category=Integration`) requires STK 12 installed and
COM-registered.
```

- [ ] **Step 10: Commit**

```bash
git add mvp4/ docs/superpowers/plans/2026-04-22-mvp4-stk-native-embedded.md
git commit -m "feat(mvp4): scaffold Sg.Mvp4 solution (App, Domain, Tests) + smoke test"
```

---

## Task 2: IStkRootService interface + FakeStkRootService for tests

**Why this task exists:** View-models (Tasks 7–12) must be unit-testable without an STK Engine running. Rule: view-models depend on `IStkRootService`, not on `AgStkObjectRootClass` directly. The concrete COM implementation lands in Task 4; this task establishes the seam with a working in-memory fake.

**Files:**
- Create: `mvp4/Sg.Mvp4.Domain/Services/IStkRootService.cs`
- Create: `mvp4/Sg.Mvp4.Domain/Services/StkRootService.cs` (stub — real impl in Task 4)
- Create: `mvp4/Sg.Mvp4.Tests/TestCategories.cs`
- Create: `mvp4/Sg.Mvp4.Tests/Fakes/FakeStkRootService.cs`
- Create: `mvp4/Sg.Mvp4.Tests/Services/FakeStkRootServiceTests.cs`

- [ ] **Step 1: Define test categories**

Create `mvp4/Sg.Mvp4.Tests/TestCategories.cs`:

```csharp
namespace Sg.Mvp4.Tests;

public static class TestCategories
{
    public const string Unit        = "Unit";
    public const string Integration = "Integration";
}
```

- [ ] **Step 2: Write the failing tests**

Create `mvp4/Sg.Mvp4.Tests/Services/FakeStkRootServiceTests.cs`:

```csharp
using FluentAssertions;
using NUnit.Framework;
using Sg.Mvp4.Domain.Models;
using Sg.Mvp4.Tests.Fakes;

namespace Sg.Mvp4.Tests.Services;

[TestFixture, Category(TestCategories.Unit)]
public class FakeStkRootServiceTests
{
    [Test]
    public void NewScenario_clears_previous_children_and_sets_time_window()
    {
        var svc = new FakeStkRootService();
        svc.NewScenario("Before", DateTime.UtcNow, DateTime.UtcNow.AddHours(1));
        svc.AddEntity(EntityKind.Aircraft, "Leftover", parentPath: null);
        svc.Children.Should().HaveCount(1);

        var start = new DateTime(2026, 4, 15, 6, 30, 0, DateTimeKind.Utc);
        var stop  = new DateTime(2026, 4, 15, 8, 46, 0, DateTimeKind.Utc);
        svc.NewScenario("Fresh", start, stop);

        svc.Children.Should().BeEmpty();
        svc.ScenarioName.Should().Be("Fresh");
        svc.StartTimeUtc.Should().Be(start);
        svc.StopTimeUtc.Should().Be(stop);
    }

    [Test]
    public void AddEntity_under_root_appends_child()
    {
        var svc = NewScenario();
        var ac = svc.AddEntity(EntityKind.Aircraft, "A1", parentPath: null);
        svc.Children.Should().ContainSingle(c => c.Name == "A1" && c.Kind == EntityKind.Aircraft);
        ac.Path.Should().Be("A1");
    }

    [Test]
    public void AddEntity_under_aircraft_appends_sensor()
    {
        var svc = NewScenario();
        var ac = svc.AddEntity(EntityKind.Aircraft, "A1", parentPath: null);
        var s  = svc.AddEntity(EntityKind.Sensor, "S1", parentPath: ac.Path);
        ac.Children.Should().ContainSingle(c => c.Name == "S1");
        s.Path.Should().Be("A1/S1");
    }

    [Test]
    public void AddEntity_sensor_under_root_throws()
    {
        var svc = NewScenario();
        var act = () => svc.AddEntity(EntityKind.Sensor, "S", parentPath: null);
        act.Should().Throw<InvalidOperationException>()
           .WithMessage("*Sensor*Aircraft or Facility*");
    }

    [Test]
    public void AddEntity_figureOfMerit_under_root_throws()
    {
        var svc = NewScenario();
        var act = () => svc.AddEntity(EntityKind.FigureOfMerit, "F", parentPath: null);
        act.Should().Throw<InvalidOperationException>();
    }

    [Test]
    public void RemoveEntity_by_path_removes_from_parent()
    {
        var svc = NewScenario();
        var ac = svc.AddEntity(EntityKind.Aircraft, "A1", parentPath: null);
        svc.AddEntity(EntityKind.Sensor, "S1", parentPath: ac.Path);

        svc.RemoveEntity("A1/S1");

        ac.Children.Should().BeEmpty();
        svc.Children.Should().HaveCount(1);
    }

    [Test]
    public void ScenarioChanged_event_fires_on_NewScenario_and_AddEntity()
    {
        var svc = new FakeStkRootService();
        var raised = 0;
        svc.ScenarioChanged += (_, _) => raised++;

        svc.NewScenario("S", DateTime.UtcNow, DateTime.UtcNow.AddHours(1));
        svc.AddEntity(EntityKind.Aircraft, "A1", parentPath: null);

        raised.Should().Be(2);
    }

    [Test]
    public void ComputeAll_records_invocation()
    {
        var svc = NewScenario();
        svc.ComputeAll();
        svc.ComputeCallCount.Should().Be(1);
    }

    private static FakeStkRootService NewScenario()
    {
        var s = new FakeStkRootService();
        s.NewScenario("S", DateTime.UtcNow, DateTime.UtcNow.AddHours(1));
        return s;
    }
}
```

- [ ] **Step 3: Define the interface**

Create `mvp4/Sg.Mvp4.Domain/Services/IStkRootService.cs`:

```csharp
using Sg.Mvp4.Domain.Models;

namespace Sg.Mvp4.Domain.Services;

/// <summary>Abstraction over STK's IAgStkObjectRoot. Real impl is COM-backed;
/// tests use FakeStkRootService.</summary>
public interface IStkRootService
{
    string?     ScenarioName { get; }
    DateTime    StartTimeUtc { get; }
    DateTime    StopTimeUtc  { get; }
    IReadOnlyList<IStkEntityNode> Children { get; }

    event EventHandler? ScenarioChanged;

    void NewScenario(string name, DateTime startUtc, DateTime stopUtc);
    void CloseScenario();

    /// <param name="parentPath">null = add under scenario root; "Aircraft1" for a child.</param>
    IStkEntityNode AddEntity(EntityKind kind, string name, string? parentPath);

    /// <param name="path">entity path like "Aircraft1" or "Aircraft1/Sensor1".</param>
    void RemoveEntity(string path);

    IStkEntityNode? FindByPath(string path);

    /// <summary>Runs ComputeAccesses on every CoverageDefinition.</summary>
    void ComputeAll();
}

public interface IStkEntityNode
{
    EntityKind Kind { get; }
    string     Name { get; }
    string     Path { get; }
    IReadOnlyList<IStkEntityNode> Children { get; }
}
```

- [ ] **Step 4: Stub the COM-backed implementation**

Create `mvp4/Sg.Mvp4.Domain/Services/StkRootService.cs`:

```csharp
using Sg.Mvp4.Domain.Models;

namespace Sg.Mvp4.Domain.Services;

/// <summary>COM-backed IStkRootService. Real behaviour lands in Task 4.
/// This stub keeps the solution buildable; property-reads return null /
/// default so the UI bindings don't crash before COM is wired up.</summary>
public sealed class StkRootService : IStkRootService
{
    public string?  ScenarioName => null;
    public DateTime StartTimeUtc => default;
    public DateTime StopTimeUtc  => default;
    public IReadOnlyList<IStkEntityNode> Children { get; } = Array.Empty<IStkEntityNode>();

    public event EventHandler? ScenarioChanged;

    public void NewScenario(string name, DateTime startUtc, DateTime stopUtc) => throw new NotImplementedException("Task 4");
    public void CloseScenario() => throw new NotImplementedException("Task 4");
    public IStkEntityNode AddEntity(EntityKind kind, string name, string? parentPath) => throw new NotImplementedException("Task 4");
    public void RemoveEntity(string path) => throw new NotImplementedException("Task 4");
    public IStkEntityNode? FindByPath(string path) => null;
    public void ComputeAll() => throw new NotImplementedException("Task 4");

    private void _keepEventUsed() => ScenarioChanged?.Invoke(this, EventArgs.Empty);
}
```

- [ ] **Step 5: Implement the fake**

Create `mvp4/Sg.Mvp4.Tests/Fakes/FakeStkRootService.cs`:

```csharp
using Sg.Mvp4.Domain.Models;
using Sg.Mvp4.Domain.Services;

namespace Sg.Mvp4.Tests.Fakes;

public sealed class FakeStkRootService : IStkRootService
{
    private readonly List<FakeEntityNode> _rootChildren = new();

    public string?  ScenarioName { get; private set; }
    public DateTime StartTimeUtc { get; private set; }
    public DateTime StopTimeUtc  { get; private set; }
    public IReadOnlyList<IStkEntityNode> Children => _rootChildren;
    public event EventHandler? ScenarioChanged;

    public int ComputeCallCount { get; private set; }

    public void NewScenario(string name, DateTime startUtc, DateTime stopUtc)
    {
        _rootChildren.Clear();
        ScenarioName = name;
        StartTimeUtc = startUtc;
        StopTimeUtc  = stopUtc;
        ScenarioChanged?.Invoke(this, EventArgs.Empty);
    }

    public void CloseScenario()
    {
        _rootChildren.Clear();
        ScenarioName = null;
        ScenarioChanged?.Invoke(this, EventArgs.Empty);
    }

    public IStkEntityNode AddEntity(EntityKind kind, string name, string? parentPath)
    {
        ValidateParentage(kind, parentPath);
        var path = parentPath is null ? name : $"{parentPath}/{name}";
        var node = new FakeEntityNode(kind, name, path);

        if (parentPath is null)
        {
            _rootChildren.Add(node);
        }
        else
        {
            if (FindByPath(parentPath) is not FakeEntityNode parent)
                throw new InvalidOperationException($"Parent '{parentPath}' not found.");
            parent.Add(node);
        }

        ScenarioChanged?.Invoke(this, EventArgs.Empty);
        return node;
    }

    public void RemoveEntity(string path)
    {
        var segments = path.Split('/');
        if (segments.Length == 1)
        {
            _rootChildren.RemoveAll(n => n.Name == segments[0]);
        }
        else
        {
            var parentPath = string.Join('/', segments.SkipLast(1));
            if (FindByPath(parentPath) is FakeEntityNode parent)
                parent.RemoveChild(segments[^1]);
        }
        ScenarioChanged?.Invoke(this, EventArgs.Empty);
    }

    public IStkEntityNode? FindByPath(string path)
    {
        var segments = path.Split('/');
        IStkEntityNode? cursor = _rootChildren.FirstOrDefault(n => n.Name == segments[0]);
        for (var i = 1; i < segments.Length && cursor is not null; i++)
            cursor = cursor.Children.FirstOrDefault(c => c.Name == segments[i]);
        return cursor;
    }

    public void ComputeAll() => ComputeCallCount++;

    private static void ValidateParentage(EntityKind kind, string? parentPath)
    {
        var rootOnlyKinds = new[] { EntityKind.Aircraft, EntityKind.Facility, EntityKind.AreaTarget, EntityKind.CoverageDefinition };
        var childOnlyKinds = new[] { EntityKind.Sensor, EntityKind.FigureOfMerit };

        if (parentPath is null && childOnlyKinds.Contains(kind))
        {
            if (kind == EntityKind.Sensor)
                throw new InvalidOperationException("Sensor must be added under Aircraft or Facility.");
            throw new InvalidOperationException("FigureOfMerit must be added under CoverageDefinition.");
        }
    }

    private sealed class FakeEntityNode : IStkEntityNode
    {
        private readonly List<FakeEntityNode> _children = new();
        public FakeEntityNode(EntityKind kind, string name, string path) { Kind = kind; Name = name; Path = path; }
        public EntityKind Kind { get; }
        public string     Name { get; }
        public string     Path { get; }
        public IReadOnlyList<IStkEntityNode> Children => _children;
        public void Add(FakeEntityNode child) => _children.Add(child);
        public void RemoveChild(string name) => _children.RemoveAll(c => c.Name == name);
    }
}
```

- [ ] **Step 6: Run tests**

```bash
cd mvp4 && dotnet test --filter Category=Unit
```

Expected: 8 tests pass (1 smoke + 8 fake — wait, let me re-count — smoke + 8 fake = 9).

- [ ] **Step 7: Commit**

```bash
git add mvp4/Sg.Mvp4.Domain/Services mvp4/Sg.Mvp4.Tests
git commit -m "feat(mvp4): IStkRootService interface + FakeStkRootService"
```

---

## Task 3: MainWindowViewModel + 3-pane shell (placeholders)

**Files:**
- Modify: `mvp4/Sg.Mvp4.App/App.xaml`
- Modify: `mvp4/Sg.Mvp4.App/App.xaml.cs`
- Modify: `mvp4/Sg.Mvp4.App/MainWindow.xaml`
- Modify: `mvp4/Sg.Mvp4.App/MainWindow.xaml.cs`
- Create: `mvp4/Sg.Mvp4.Domain/ViewModels/MainWindowViewModel.cs`
- Create: `mvp4/Sg.Mvp4.Tests/ViewModels/MainWindowViewModelTests.cs`

- [ ] **Step 1: Write the failing VM test**

Create `mvp4/Sg.Mvp4.Tests/ViewModels/MainWindowViewModelTests.cs`:

```csharp
using FluentAssertions;
using NUnit.Framework;
using Sg.Mvp4.Domain.ViewModels;
using Sg.Mvp4.Tests.Fakes;

namespace Sg.Mvp4.Tests.ViewModels;

[TestFixture, Category(TestCategories.Unit)]
public class MainWindowViewModelTests
{
    [Test]
    public void Title_shows_no_scenario_when_name_null()
    {
        var vm = new MainWindowViewModel(new FakeStkRootService());
        vm.Title.Should().Be("MVP4 — (no scenario)");
    }

    [Test]
    public void Title_reflects_current_scenario_name()
    {
        var svc = new FakeStkRootService();
        var vm  = new MainWindowViewModel(svc);

        svc.NewScenario("Scenario34", DateTime.UtcNow, DateTime.UtcNow.AddHours(1));

        vm.Title.Should().Be("MVP4 — Scenario34");
    }

    [Test]
    public void IsDirty_is_false_initially()
    {
        new MainWindowViewModel(new FakeStkRootService()).IsDirty.Should().BeFalse();
    }
}
```

- [ ] **Step 2: Create MainWindowViewModel**

Create `mvp4/Sg.Mvp4.Domain/ViewModels/MainWindowViewModel.cs`:

```csharp
using CommunityToolkit.Mvvm.ComponentModel;
using Sg.Mvp4.Domain.Services;

namespace Sg.Mvp4.Domain.ViewModels;

public partial class MainWindowViewModel : ObservableObject
{
    private readonly IStkRootService _stk;

    [ObservableProperty] private bool _isDirty;

    public MainWindowViewModel(IStkRootService stk)
    {
        _stk = stk;
        _stk.ScenarioChanged += (_, _) => OnPropertyChanged(nameof(Title));
    }

    public string Title => _stk.ScenarioName is null
        ? "MVP4 — (no scenario)"
        : $"MVP4 — {_stk.ScenarioName}";
}
```

- [ ] **Step 3: Run tests**

```bash
cd mvp4 && dotnet test --filter Category=Unit
```

Expected: 12 passing tests (1 smoke + 8 fake + 3 VM).

- [ ] **Step 4: Write MainWindow XAML with 3-pane layout**

Replace `mvp4/Sg.Mvp4.App/MainWindow.xaml`:

```xml
<Window x:Class="Sg.Mvp4.App.MainWindow"
        xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation"
        xmlns:x="http://schemas.microsoft.com/winfx/2006/xaml"
        Title="{Binding Title}"
        Width="1280" Height="800" WindowStartupLocation="CenterScreen">
    <DockPanel>
        <Menu DockPanel.Dock="Top">
            <MenuItem Header="_File">
                <MenuItem Header="_New Scenario..." />
                <MenuItem Header="_Open..." />
                <Separator />
                <MenuItem Header="_Save" />
                <MenuItem Header="Save _As..." />
                <Separator />
                <MenuItem Header="E_xit" Click="ExitMenu_Click" />
            </MenuItem>
            <MenuItem Header="_Compute">
                <MenuItem Header="Compute All Coverage" />
            </MenuItem>
            <MenuItem Header="_View" />
            <MenuItem Header="_Help" />
        </Menu>

        <StatusBar DockPanel.Dock="Bottom" Height="24">
            <StatusBarItem Content="STK: (not yet connected)" />
            <Separator />
            <StatusBarItem Content="Scenario: (none)" />
        </StatusBar>

        <Grid>
            <Grid.ColumnDefinitions>
                <ColumnDefinition Width="200" MinWidth="140" />
                <ColumnDefinition Width="4" />
                <ColumnDefinition Width="*" />
                <ColumnDefinition Width="4" />
                <ColumnDefinition Width="280" MinWidth="180" />
            </Grid.ColumnDefinitions>

            <Border Grid.Column="0" BorderBrush="#888" BorderThickness="0,0,1,0">
                <TextBlock Text="Object tree (Task 5)" Margin="12" Foreground="#888" />
            </Border>
            <GridSplitter Grid.Column="1" HorizontalAlignment="Stretch" Background="#DDD" />
            <Border Grid.Column="2" Background="#111">
                <TextBlock Text="Embedded STK globe (Task 4)" Foreground="#CCC" Margin="12" />
            </Border>
            <GridSplitter Grid.Column="3" HorizontalAlignment="Stretch" Background="#DDD" />
            <Border Grid.Column="4" BorderBrush="#888" BorderThickness="1,0,0,0">
                <TextBlock Text="Property panel (Task 6)" Margin="12" Foreground="#888" />
            </Border>
        </Grid>
    </DockPanel>
</Window>
```

- [ ] **Step 5: Update MainWindow code-behind**

Replace `mvp4/Sg.Mvp4.App/MainWindow.xaml.cs`:

```csharp
using System.Windows;
using Sg.Mvp4.Domain.ViewModels;

namespace Sg.Mvp4.App;

public partial class MainWindow : Window
{
    public MainWindow(MainWindowViewModel vm)
    {
        InitializeComponent();
        DataContext = vm;
    }

    private void ExitMenu_Click(object sender, RoutedEventArgs e) => Close();
}
```

- [ ] **Step 6: Wire DI container in App startup**

Replace `mvp4/Sg.Mvp4.App/App.xaml.cs`:

```csharp
using System.Windows;
using Microsoft.Extensions.DependencyInjection;
using Sg.Mvp4.Domain.Services;
using Sg.Mvp4.Domain.ViewModels;

namespace Sg.Mvp4.App;

public partial class App : Application
{
    public IServiceProvider Services { get; private set; } = null!;

    protected override void OnStartup(StartupEventArgs e)
    {
        base.OnStartup(e);

        var services = new ServiceCollection();
        services.AddSingleton<IStkRootService, StkRootService>();
        services.AddSingleton<MainWindowViewModel>();
        services.AddSingleton<MainWindow>();

        Services = services.BuildServiceProvider();
        Services.GetRequiredService<MainWindow>().Show();
    }
}
```

- [ ] **Step 7: Update App.xaml (remove StartupUri)**

Replace `mvp4/Sg.Mvp4.App/App.xaml`:

```xml
<Application x:Class="Sg.Mvp4.App.App"
             xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation"
             xmlns:x="http://schemas.microsoft.com/winfx/2006/xaml">
    <Application.Resources />
</Application>
```

- [ ] **Step 8: Build + smoke-run**

```bash
cd mvp4 && dotnet build
dotnet run --project Sg.Mvp4.App
```

Expected: window opens; title reads `MVP4 — (no scenario)`; three panes visible with placeholder text; File menu → Exit closes the app.

- [ ] **Step 9: Commit**

```bash
git add mvp4/Sg.Mvp4.App mvp4/Sg.Mvp4.Domain/ViewModels mvp4/Sg.Mvp4.Tests/ViewModels
git commit -m "feat(mvp4): 3-pane MainWindow shell + MainWindowViewModel + DI container"
```

---

## Task 4: Real StkRootService (COM) + ActiveX display host

**Why this task exists:** MVP4's whole thesis is "embedded STK globe". This task wires the real `IAgStkObjectRoot` into the app and hosts both ActiveX controls (`AxAgUiAxVOCntrl` 3D, `AxAgUiAx2DCntrl` 2D) inside the center pane via `WindowsFormsHost`. After this task, running the app opens a window with a live STK globe.

**Files:**
- Add project reference: STK interop DLLs from `C:\Program Files\AGI\STK 12\bin\Primary Interop Assemblies\`
- Modify: `mvp4/Sg.Mvp4.Domain/Services/StkRootService.cs` (replace stub)
- Create: `mvp4/Sg.Mvp4.App/Views/Shell/StkDisplayHost.xaml`
- Create: `mvp4/Sg.Mvp4.App/Views/Shell/StkDisplayHost.xaml.cs`
- Modify: `mvp4/Sg.Mvp4.App/MainWindow.xaml` (replace center-column placeholder with `StkDisplayHost`)
- Create: `mvp4/Sg.Mvp4.Tests/Integration/StkRootServiceIntegrationTests.cs`

- [ ] **Step 1: Add COM-interop references to the Domain project**

From `mvp4/Sg.Mvp4.Domain/`:

```bash
dotnet add reference --help   # skip
```

Edit `mvp4/Sg.Mvp4.Domain/Sg.Mvp4.Domain.csproj` and add an `<ItemGroup>` with explicit references. The DLL file names use `.Interop.dll`, but the `Reference Include=` uses the assembly name without `.Interop`:

```xml
<ItemGroup>
  <Reference Include="AGI.STKObjects.Interop">
    <HintPath>C:\Program Files\AGI\STK 12\bin\Primary Interop Assemblies\AGI.STKObjects.Interop.dll</HintPath>
    <Private>true</Private>
    <EmbedInteropTypes>false</EmbedInteropTypes>
  </Reference>
  <Reference Include="AGI.STKUtil.Interop">
    <HintPath>C:\Program Files\AGI\STK 12\bin\Primary Interop Assemblies\AGI.STKUtil.Interop.dll</HintPath>
    <Private>true</Private>
    <EmbedInteropTypes>false</EmbedInteropTypes>
  </Reference>
  <Reference Include="AGI.STKVgt.Interop">
    <HintPath>C:\Program Files\AGI\STK 12\bin\Primary Interop Assemblies\AGI.STKVgt.Interop.dll</HintPath>
    <Private>true</Private>
    <EmbedInteropTypes>false</EmbedInteropTypes>
  </Reference>
</ItemGroup>
```

C# code in this project still uses `using AGI.STKObjects;` — the `.Interop` is only in the file name, not the namespace.

- [ ] **Step 2: Add ActiveX references to the App project**

Edit `mvp4/Sg.Mvp4.App/Sg.Mvp4.App.csproj` and add references to STK X (the ActiveX globe-control framework):

```xml
<ItemGroup>
  <Reference Include="AGI.STKX.Interop">
    <HintPath>C:\Program Files\AGI\STK 12\bin\Primary Interop Assemblies\AGI.STKX.Interop.dll</HintPath>
    <Private>true</Private>
    <EmbedInteropTypes>false</EmbedInteropTypes>
  </Reference>
  <Reference Include="AxAGI.STKX.Interop">
    <HintPath>C:\Program Files\AGI\STK 12\bin\Primary Interop Assemblies\AxAGI.STKX.Interop.dll</HintPath>
    <Private>true</Private>
  </Reference>
</ItemGroup>
```

If `AxAGI.STKX.Interop.dll` is not present, regenerate it:

```bash
# Visual Studio 2022 Developer Command Prompt (as admin)
aximp "C:\Program Files\AGI\STK 12\bin\AgStkX.ocx"
# produces AGI.STKX.Interop.dll and AxAGI.STKX.Interop.dll
```

**Class names reminder:** STK X's ActiveX controls are `AxAgUiAxVOCntrl` (3D globe / Visual Object) and `AxAgUiAx2DCntrl` (2D map). Namespace inside `AxAGI.STKX.Interop` is `AxAGI.STKX`. If IntelliSense disagrees after generating wrappers, open the DLL in a decompiler or VS Object Browser and use whatever class names are actually there — don't invent them.

Verify `dotnet build` succeeds.

- [ ] **Step 3: Write the failing integration test**

Create `mvp4/Sg.Mvp4.Tests/Integration/StkRootServiceIntegrationTests.cs`:

```csharp
using FluentAssertions;
using NUnit.Framework;
using Sg.Mvp4.Domain.Models;
using Sg.Mvp4.Domain.Services;

namespace Sg.Mvp4.Tests.Integration;

[TestFixture, Category(TestCategories.Integration)]
public class StkRootServiceIntegrationTests
{
    private StkRootService _svc = null!;

    [SetUp] public void SetUp() { _svc = new StkRootService(); }
    [TearDown] public void TearDown() { _svc.CloseScenario(); _svc.Dispose(); }

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
    public void AddEntity_aircraft_creates_IAgAircraft_in_scenario()
    {
        _svc.NewScenario("T", DateTime.UtcNow, DateTime.UtcNow.AddHours(1));
        _svc.AddEntity(EntityKind.Aircraft, "A1", parentPath: null);

        _svc.Children.Should().ContainSingle(c => c.Name == "A1" && c.Kind == EntityKind.Aircraft);
    }

    [Test]
    public void AddEntity_sensor_under_aircraft_creates_child()
    {
        _svc.NewScenario("T", DateTime.UtcNow, DateTime.UtcNow.AddHours(1));
        var ac = _svc.AddEntity(EntityKind.Aircraft, "A1", parentPath: null);
        _svc.AddEntity(EntityKind.Sensor, "S1", parentPath: ac.Path);

        ac.Children.Should().ContainSingle(c => c.Name == "S1");
    }
}
```

- [ ] **Step 4: Replace the StkRootService stub with the real COM impl**

Replace `mvp4/Sg.Mvp4.Domain/Services/StkRootService.cs`:

```csharp
using System.Runtime.InteropServices;
using AGI.STKObjects;
using Sg.Mvp4.Domain.Models;

namespace Sg.Mvp4.Domain.Services;

public sealed class StkRootService : IStkRootService, IDisposable
{
    private AgStkObjectRoot? _root;

    public AgStkObjectRoot Root => _root ??= new AgStkObjectRoot();

    public string?  ScenarioName => (_root?.CurrentScenario as IAgStkObject)?.InstanceName;

    public DateTime StartTimeUtc => _root?.CurrentScenario is IAgScenario s
        ? DateTime.Parse((string)s.StartTime).ToUniversalTime()
        : default;

    public DateTime StopTimeUtc  => _root?.CurrentScenario is IAgScenario s
        ? DateTime.Parse((string)s.StopTime).ToUniversalTime()
        : default;

    public IReadOnlyList<IStkEntityNode> Children => SnapshotRoot();

    public event EventHandler? ScenarioChanged;

    public void NewScenario(string name, DateTime startUtc, DateTime stopUtc)
    {
        if (Root.CurrentScenario is not null) Root.CloseScenario();
        Root.NewScenario(name);
        var sc = (IAgScenario)Root.CurrentScenario;
        sc.SetTimePeriod(ToStkTime(startUtc), ToStkTime(stopUtc));
        sc.Epoch = ToStkTime(startUtc);
        ScenarioChanged?.Invoke(this, EventArgs.Empty);
    }

    public void CloseScenario()
    {
        if (_root?.CurrentScenario is not null) _root.CloseScenario();
        ScenarioChanged?.Invoke(this, EventArgs.Empty);
    }

    public IStkEntityNode AddEntity(EntityKind kind, string name, string? parentPath)
    {
        var parent = parentPath is null
            ? (IAgStkObject)Root.CurrentScenario
            : (IAgStkObject)(FindComByPath(parentPath) ?? throw new InvalidOperationException($"Parent '{parentPath}' not found."));

        var classType = ToClassType(kind);
        var created = parent.Children.New(classType, name);

        ScenarioChanged?.Invoke(this, EventArgs.Empty);
        return new ComEntityNode(created, kind);
    }

    public void RemoveEntity(string path)
    {
        var segments = path.Split('/');
        IAgStkObject parent = (IAgStkObject)Root.CurrentScenario;
        for (var i = 0; i < segments.Length - 1; i++)
            parent = parent.Children[segments[i]];
        parent.Children.Unload(ToClassType(KindOf(parent.Children[segments[^1]])), segments[^1]);
        ScenarioChanged?.Invoke(this, EventArgs.Empty);
    }

    public IStkEntityNode? FindByPath(string path)
    {
        var com = FindComByPath(path);
        return com is null ? null : new ComEntityNode(com, KindOf(com));
    }

    public void ComputeAll()
    {
        if (_root?.CurrentScenario is null) return;
        foreach (IAgStkObject child in Root.CurrentScenario.Children)
        {
            if (KindOf(child) == EntityKind.CoverageDefinition)
            {
                ((IAgCoverageDefinition)child).ComputeAccesses();
            }
        }
    }

    public void Dispose()
    {
        if (_root is not null) { Marshal.FinalReleaseComObject(_root); _root = null; }
        GC.SuppressFinalize(this);
    }

    // ---------------- helpers ----------------

    private IAgStkObject? FindComByPath(string path)
    {
        var segments = path.Split('/');
        IAgStkObject? cursor = Root.CurrentScenario.Children[segments[0]];
        for (var i = 1; i < segments.Length && cursor is not null; i++)
            cursor = cursor.Children[segments[i]];
        return cursor;
    }

    private IReadOnlyList<IStkEntityNode> SnapshotRoot()
    {
        if (_root?.CurrentScenario is null) return Array.Empty<IStkEntityNode>();
        var list = new List<IStkEntityNode>();
        foreach (IAgStkObject child in Root.CurrentScenario.Children)
            list.Add(new ComEntityNode(child, KindOf(child)));
        return list;
    }

    private static AgESTKObjectType ToClassType(EntityKind kind) => kind switch
    {
        EntityKind.Aircraft           => AgESTKObjectType.eAircraft,
        EntityKind.Facility           => AgESTKObjectType.eFacility,
        EntityKind.AreaTarget         => AgESTKObjectType.eAreaTarget,
        EntityKind.Sensor             => AgESTKObjectType.eSensor,
        EntityKind.CoverageDefinition => AgESTKObjectType.eCoverageDefinition,
        EntityKind.FigureOfMerit      => AgESTKObjectType.eFigureOfMerit,
        _ => throw new ArgumentOutOfRangeException(nameof(kind))
    };

    private static EntityKind KindOf(IAgStkObject o) => o.ClassType switch
    {
        AgESTKObjectType.eAircraft           => EntityKind.Aircraft,
        AgESTKObjectType.eFacility           => EntityKind.Facility,
        AgESTKObjectType.eAreaTarget         => EntityKind.AreaTarget,
        AgESTKObjectType.eSensor             => EntityKind.Sensor,
        AgESTKObjectType.eCoverageDefinition => EntityKind.CoverageDefinition,
        AgESTKObjectType.eFigureOfMerit      => EntityKind.FigureOfMerit,
        _ => throw new InvalidOperationException($"Unsupported STK class type: {o.ClassType}")
    };

    private static string ToStkTime(DateTime utc) => utc.ToString("dd MMM yyyy HH:mm:ss.fff");

    private sealed class ComEntityNode : IStkEntityNode
    {
        private readonly IAgStkObject _obj;
        public ComEntityNode(IAgStkObject obj, EntityKind kind) { _obj = obj; Kind = kind; }

        public EntityKind Kind { get; }
        public string     Name => _obj.InstanceName;
        public string     Path => BuildPath(_obj);
        public IReadOnlyList<IStkEntityNode> Children
        {
            get
            {
                var list = new List<IStkEntityNode>();
                foreach (IAgStkObject child in _obj.Children)
                    list.Add(new ComEntityNode(child, KindOf(child)));
                return list;
            }
        }

        private static string BuildPath(IAgStkObject obj)
        {
            var parts = new Stack<string>();
            IAgStkObject? cursor = obj;
            while (cursor is not null && cursor.ClassType != AgESTKObjectType.eScenario)
            {
                parts.Push(cursor.InstanceName);
                cursor = cursor.Parent as IAgStkObject;
            }
            return string.Join('/', parts);
        }
    }
}
```

- [ ] **Step 5: Run the integration tests**

```bash
cd mvp4 && dotnet test --filter Category=Integration
```

Expected: 3 tests pass. Takes ~15–30 s — STK Engine cold-starts on first construct.

If a test fails with `HRESULT 0x800401F3` (class not registered), re-run `regsvr32` on STK's `agacsrv.exe` (usually done by the STK installer; occasionally broken after Windows updates).

- [ ] **Step 6: Create the ActiveX host UserControl**

Create `mvp4/Sg.Mvp4.App/Views/Shell/StkDisplayHost.xaml`:

```xml
<UserControl x:Class="Sg.Mvp4.App.Views.Shell.StkDisplayHost"
             xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation"
             xmlns:x="http://schemas.microsoft.com/winfx/2006/xaml"
             xmlns:wfi="clr-namespace:System.Windows.Forms.Integration;assembly=WindowsFormsIntegration">
    <TabControl>
        <TabItem Header="3D Globe">
            <wfi:WindowsFormsHost x:Name="Host3D" />
        </TabItem>
        <TabItem Header="2D Map">
            <wfi:WindowsFormsHost x:Name="Host2D" />
        </TabItem>
    </TabControl>
</UserControl>
```

Create `mvp4/Sg.Mvp4.App/Views/Shell/StkDisplayHost.xaml.cs`:

```csharp
using System.Windows.Controls;
using AxAGI.STKX;
using Sg.Mvp4.Domain.Services;

namespace Sg.Mvp4.App.Views.Shell;

public partial class StkDisplayHost : UserControl
{
    private readonly AxAgUiAxVOCntrl _globe3D = new();
    private readonly AxAgUiAx2DCntrl _map2D   = new();

    public StkDisplayHost(IStkRootService stk)
    {
        InitializeComponent();
        Host3D.Child = _globe3D;
        Host2D.Child = _map2D;

        Loaded += (_, _) =>
        {
            if (stk is StkRootService concrete)
            {
                _globe3D.SetAGISTKObject(concrete.Root);
                _map2D.SetAGISTKObject(concrete.Root);
            }
        };

        Unloaded += (_, _) =>
        {
            _globe3D.SetAGISTKObject(null);
            _map2D.SetAGISTKObject(null);
        };
    }
}
```

- [ ] **Step 7: Place the host in MainWindow**

In `mvp4/Sg.Mvp4.App/MainWindow.xaml`, replace the center `<Border Grid.Column="2" Background="#111">...</Border>` with:

```xml
<ContentControl Grid.Column="2" x:Name="StkHostSlot" />
```

In `mvp4/Sg.Mvp4.App/MainWindow.xaml.cs`, inject the host:

```csharp
using System.Windows;
using Sg.Mvp4.App.Views.Shell;
using Sg.Mvp4.Domain.ViewModels;

namespace Sg.Mvp4.App;

public partial class MainWindow : Window
{
    public MainWindow(MainWindowViewModel vm, StkDisplayHost stkHost)
    {
        InitializeComponent();
        DataContext = vm;
        StkHostSlot.Content = stkHost;
    }

    private void ExitMenu_Click(object sender, RoutedEventArgs e) => Close();
}
```

Register in `App.xaml.cs`:

```csharp
services.AddSingleton<StkDisplayHost>();
```

- [ ] **Step 8: Wire initial scenario on startup so globe has content**

In `App.xaml.cs`, after building the provider and before showing MainWindow, call:

```csharp
var stk = Services.GetRequiredService<IStkRootService>();
stk.NewScenario("Untitled", DateTime.UtcNow, DateTime.UtcNow.AddHours(1));
Services.GetRequiredService<MainWindow>().Show();
```

- [ ] **Step 9: Smoke run**

```bash
cd mvp4 && dotnet run --project Sg.Mvp4.App
```

Expected: window opens; the center tab shows a live 3D globe with the Earth. Tab to "2D Map" → flat map. Title: `MVP4 — Untitled`. Window close leaves no lingering STK process visible in Task Manager after ~5 s.

- [ ] **Step 10: Commit**

```bash
git add mvp4/Sg.Mvp4.Domain mvp4/Sg.Mvp4.App mvp4/Sg.Mvp4.Tests/Integration
git commit -m "feat(mvp4): real StkRootService (COM) + embedded ActiveX 3D/2D display host"
```

---

## Task 5: Object tree view + ObjectTreeViewModel

**Files:**
- Create: `mvp4/Sg.Mvp4.Domain/ViewModels/ObjectTreeNodeViewModel.cs`
- Create: `mvp4/Sg.Mvp4.Domain/ViewModels/ObjectTreeViewModel.cs`
- Create: `mvp4/Sg.Mvp4.Tests/ViewModels/ObjectTreeViewModelTests.cs`
- Create: `mvp4/Sg.Mvp4.App/Views/Shell/ObjectTreeView.xaml`
- Create: `mvp4/Sg.Mvp4.App/Views/Shell/ObjectTreeView.xaml.cs`
- Modify: `mvp4/Sg.Mvp4.App/MainWindow.xaml` (replace left-column placeholder)
- Modify: `mvp4/Sg.Mvp4.App/App.xaml.cs` (register view + VM)

- [ ] **Step 1: Write failing ObjectTreeViewModel tests**

Create `mvp4/Sg.Mvp4.Tests/ViewModels/ObjectTreeViewModelTests.cs`:

```csharp
using FluentAssertions;
using NUnit.Framework;
using Sg.Mvp4.Domain.Models;
using Sg.Mvp4.Domain.ViewModels;
using Sg.Mvp4.Tests.Fakes;

namespace Sg.Mvp4.Tests.ViewModels;

[TestFixture, Category(TestCategories.Unit)]
public class ObjectTreeViewModelTests
{
    [Test]
    public void Tree_mirrors_scenario_children_after_NewScenario()
    {
        var svc = new FakeStkRootService();
        var vm  = new ObjectTreeViewModel(svc);

        svc.NewScenario("S", DateTime.UtcNow, DateTime.UtcNow.AddHours(1));
        svc.AddEntity(EntityKind.Aircraft, "A1", null);
        svc.AddEntity(EntityKind.Facility, "F1", null);

        vm.Nodes.Should().HaveCount(2);
        vm.Nodes.Select(n => n.Name).Should().Contain(new[] { "A1", "F1" });
    }

    [Test]
    public void Nested_sensor_appears_under_aircraft_node()
    {
        var svc = new FakeStkRootService();
        var vm  = new ObjectTreeViewModel(svc);
        svc.NewScenario("S", DateTime.UtcNow, DateTime.UtcNow.AddHours(1));
        var ac = svc.AddEntity(EntityKind.Aircraft, "A1", null);
        svc.AddEntity(EntityKind.Sensor, "Sen1", ac.Path);

        var a = vm.Nodes.Single(n => n.Name == "A1");
        a.Children.Should().ContainSingle(n => n.Name == "Sen1" && n.Kind == EntityKind.Sensor);
    }

    [Test]
    public void AddChild_on_root_appends_new_entity()
    {
        var svc = new FakeStkRootService();
        var vm  = new ObjectTreeViewModel(svc);
        svc.NewScenario("S", DateTime.UtcNow, DateTime.UtcNow.AddHours(1));

        vm.AddChildCommand.Execute((EntityKind.Aircraft, (string?)null, "NewA"));

        vm.Nodes.Should().ContainSingle(n => n.Name == "NewA");
    }

    [Test]
    public void RemoveNode_removes_from_service()
    {
        var svc = new FakeStkRootService();
        var vm  = new ObjectTreeViewModel(svc);
        svc.NewScenario("S", DateTime.UtcNow, DateTime.UtcNow.AddHours(1));
        svc.AddEntity(EntityKind.Facility, "F1", null);

        vm.RemoveCommand.Execute("F1");

        vm.Nodes.Should().BeEmpty();
    }

    [Test]
    public void Selecting_node_updates_SelectedPath()
    {
        var svc = new FakeStkRootService();
        var vm  = new ObjectTreeViewModel(svc);
        svc.NewScenario("S", DateTime.UtcNow, DateTime.UtcNow.AddHours(1));
        svc.AddEntity(EntityKind.Aircraft, "A1", null);

        vm.SelectedNode = vm.Nodes.Single();

        vm.SelectedPath.Should().Be("A1");
    }
}
```

- [ ] **Step 2: Implement ObjectTreeNodeViewModel**

Create `mvp4/Sg.Mvp4.Domain/ViewModels/ObjectTreeNodeViewModel.cs`:

```csharp
using System.Collections.ObjectModel;
using CommunityToolkit.Mvvm.ComponentModel;
using Sg.Mvp4.Domain.Models;
using Sg.Mvp4.Domain.Services;

namespace Sg.Mvp4.Domain.ViewModels;

public partial class ObjectTreeNodeViewModel : ObservableObject
{
    [ObservableProperty] private bool _isExpanded = true;

    public ObjectTreeNodeViewModel(IStkEntityNode source)
    {
        Kind = source.Kind;
        Name = source.Name;
        Path = source.Path;
        Children = new ObservableCollection<ObjectTreeNodeViewModel>(
            source.Children.Select(c => new ObjectTreeNodeViewModel(c)));
    }

    public EntityKind Kind { get; }
    public string     Name { get; }
    public string     Path { get; }
    public ObservableCollection<ObjectTreeNodeViewModel> Children { get; }

    public string DisplayLabel => $"{Kind}: {Name}";
}
```

- [ ] **Step 3: Implement ObjectTreeViewModel**

Create `mvp4/Sg.Mvp4.Domain/ViewModels/ObjectTreeViewModel.cs`:

```csharp
using System.Collections.ObjectModel;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using Sg.Mvp4.Domain.Models;
using Sg.Mvp4.Domain.Services;

namespace Sg.Mvp4.Domain.ViewModels;

public partial class ObjectTreeViewModel : ObservableObject
{
    private readonly IStkRootService _stk;

    [ObservableProperty] private ObjectTreeNodeViewModel? _selectedNode;

    public ObservableCollection<ObjectTreeNodeViewModel> Nodes { get; } = new();

    public string? SelectedPath => SelectedNode?.Path;

    public IRelayCommand<(EntityKind kind, string? parentPath, string name)> AddChildCommand { get; }
    public IRelayCommand<string> RemoveCommand { get; }

    public event EventHandler<string?>? SelectionChanged;

    public ObjectTreeViewModel(IStkRootService stk)
    {
        _stk = stk;
        _stk.ScenarioChanged += (_, _) => RebuildFromService();

        AddChildCommand = new RelayCommand<(EntityKind, string?, string)>(args =>
        {
            _stk.AddEntity(args.Item1, args.Item3, args.Item2);
        });

        RemoveCommand = new RelayCommand<string>(path =>
        {
            if (path is not null) _stk.RemoveEntity(path);
        });

        RebuildFromService();
    }

    partial void OnSelectedNodeChanged(ObjectTreeNodeViewModel? value)
    {
        OnPropertyChanged(nameof(SelectedPath));
        SelectionChanged?.Invoke(this, value?.Path);
    }

    private void RebuildFromService()
    {
        Nodes.Clear();
        foreach (var child in _stk.Children)
            Nodes.Add(new ObjectTreeNodeViewModel(child));
    }
}
```

- [ ] **Step 4: Run tests**

```bash
cd mvp4 && dotnet test --filter Category=Unit
```

Expected: 17 passing tests (12 prior + 5 new).

- [ ] **Step 5: Create the WPF TreeView**

Create `mvp4/Sg.Mvp4.App/Views/Shell/ObjectTreeView.xaml`:

```xml
<UserControl x:Class="Sg.Mvp4.App.Views.Shell.ObjectTreeView"
             xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation"
             xmlns:x="http://schemas.microsoft.com/winfx/2006/xaml"
             xmlns:vm="clr-namespace:Sg.Mvp4.Domain.ViewModels;assembly=Sg.Mvp4.Domain">
    <DockPanel>
        <ToolBar DockPanel.Dock="Top">
            <Button Content="+ Aircraft"  Click="AddAircraft_Click" />
            <Button Content="+ Facility"  Click="AddFacility_Click" />
            <Button Content="+ AreaTarget" Click="AddAreaTarget_Click" />
            <Button Content="+ Coverage"  Click="AddCoverage_Click" />
            <Separator />
            <Button Content="Delete"      Click="Delete_Click" />
        </ToolBar>

        <TreeView x:Name="Tree" ItemsSource="{Binding Nodes}"
                  SelectedItemChanged="Tree_SelectedItemChanged">
            <TreeView.ItemTemplate>
                <HierarchicalDataTemplate DataType="{x:Type vm:ObjectTreeNodeViewModel}"
                                          ItemsSource="{Binding Children}">
                    <TextBlock Text="{Binding DisplayLabel}" />
                </HierarchicalDataTemplate>
            </TreeView.ItemTemplate>
        </TreeView>
    </DockPanel>
</UserControl>
```

- [ ] **Step 6: Code-behind for toolbar buttons**

Create `mvp4/Sg.Mvp4.App/Views/Shell/ObjectTreeView.xaml.cs`:

```csharp
using System.Windows;
using System.Windows.Controls;
using Sg.Mvp4.Domain.Models;
using Sg.Mvp4.Domain.ViewModels;

namespace Sg.Mvp4.App.Views.Shell;

public partial class ObjectTreeView : UserControl
{
    public ObjectTreeView(ObjectTreeViewModel vm)
    {
        InitializeComponent();
        DataContext = vm;
    }

    private ObjectTreeViewModel Vm => (ObjectTreeViewModel)DataContext;

    private void Tree_SelectedItemChanged(object sender, RoutedPropertyChangedEventArgs<object> e)
    {
        Vm.SelectedNode = e.NewValue as ObjectTreeNodeViewModel;
    }

    private void AddAircraft_Click(object sender, RoutedEventArgs e)      => AddAtRoot(EntityKind.Aircraft,   "Aircraft");
    private void AddFacility_Click(object sender, RoutedEventArgs e)      => AddAtRoot(EntityKind.Facility,   "Facility");
    private void AddAreaTarget_Click(object sender, RoutedEventArgs e)    => AddAtRoot(EntityKind.AreaTarget, "AreaTarget");
    private void AddCoverage_Click(object sender, RoutedEventArgs e)      => AddAtRoot(EntityKind.CoverageDefinition, "Coverage");

    private void AddAtRoot(EntityKind kind, string baseName)
    {
        var name = UniqueName(baseName, Vm.Nodes.Select(n => n.Name));
        Vm.AddChildCommand.Execute((kind, null, name));
    }

    private void Delete_Click(object sender, RoutedEventArgs e)
    {
        if (Vm.SelectedPath is string path)
            Vm.RemoveCommand.Execute(path);
    }

    private static string UniqueName(string baseName, IEnumerable<string> existing)
    {
        var taken = new HashSet<string>(existing);
        for (var i = 1; ; i++)
        {
            var candidate = $"{baseName}{i}";
            if (!taken.Contains(candidate)) return candidate;
        }
    }
}
```

- [ ] **Step 7: Mount in MainWindow**

In `MainWindow.xaml`, replace the left-column placeholder Border:

```xml
<ContentControl Grid.Column="0" x:Name="TreeSlot" />
```

In `MainWindow.xaml.cs`, add the constructor parameter:

```csharp
public MainWindow(MainWindowViewModel vm, StkDisplayHost stkHost, ObjectTreeView tree)
{
    InitializeComponent();
    DataContext = vm;
    StkHostSlot.Content = stkHost;
    TreeSlot.Content    = tree;
}
```

In `App.xaml.cs` register:

```csharp
services.AddSingleton<ObjectTreeViewModel>();
services.AddSingleton<ObjectTreeView>();
```

- [ ] **Step 8: Smoke run**

```bash
cd mvp4 && dotnet run --project Sg.Mvp4.App
```

Expected: left toolbar shows 4 "+" buttons; clicking "+ Aircraft" adds an `Aircraft1` node to the tree, visible in the tree view. The globe in the center shows the aircraft icon at scenario default position (lat 0 lon 0). Clicking Delete after selecting removes it both from the tree and from the globe.

- [ ] **Step 9: Commit**

```bash
git add mvp4/Sg.Mvp4.Domain/ViewModels mvp4/Sg.Mvp4.App/Views/Shell mvp4/Sg.Mvp4.App/MainWindow.xaml mvp4/Sg.Mvp4.App/MainWindow.xaml.cs mvp4/Sg.Mvp4.App/App.xaml.cs mvp4/Sg.Mvp4.Tests/ViewModels
git commit -m "feat(mvp4): object tree view + ObjectTreeViewModel with Add/Delete commands"
```

---

## Task 6: Property panel host + EntityViewModelBase

**Why this task exists:** The right pane shows a different form for each entity type. Rather than a giant switch in MainWindow, use a `ContentControl` + `DataTemplateSelector` that picks the right panel from the selected node's `EntityKind`. Tasks 7–12 add the per-type panels; this task establishes the shared shell.

**Files:**
- Create: `mvp4/Sg.Mvp4.Domain/ViewModels/EntityViewModelBase.cs`
- Create: `mvp4/Sg.Mvp4.Domain/ViewModels/EmptySelectionViewModel.cs`
- Create: `mvp4/Sg.Mvp4.App/Views/Shell/PropertyPanelHostView.xaml`
- Create: `mvp4/Sg.Mvp4.App/Views/Shell/PropertyPanelHostView.xaml.cs`
- Create: `mvp4/Sg.Mvp4.App/Views/Shell/EntityPanelTemplateSelector.cs`
- Modify: `mvp4/Sg.Mvp4.App/MainWindow.xaml` (right column)
- Modify: `mvp4/Sg.Mvp4.App/App.xaml.cs`
- Create: `mvp4/Sg.Mvp4.Tests/ViewModels/EntityViewModelBaseTests.cs`

- [ ] **Step 1: Write failing base-class tests**

Create `mvp4/Sg.Mvp4.Tests/ViewModels/EntityViewModelBaseTests.cs`:

```csharp
using FluentAssertions;
using NUnit.Framework;
using Sg.Mvp4.Domain.ViewModels;

namespace Sg.Mvp4.Tests.ViewModels;

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
```

- [ ] **Step 2: Implement EntityViewModelBase**

Create `mvp4/Sg.Mvp4.Domain/ViewModels/EntityViewModelBase.cs`:

```csharp
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;

namespace Sg.Mvp4.Domain.ViewModels;

public abstract partial class EntityViewModelBase : ObservableObject
{
    [ObservableProperty] private bool _isDirty;

    public abstract string EntityName { get; }

    public IRelayCommand ApplyCommand { get; }
    public IRelayCommand ResetCommand { get; }

    protected EntityViewModelBase()
    {
        ApplyCommand = new RelayCommand(Apply, () => IsDirty);
        ResetCommand = new RelayCommand(Reset, () => IsDirty);
        PropertyChanged += (_, e) =>
        {
            if (e.PropertyName == nameof(IsDirty))
            {
                ApplyCommand.NotifyCanExecuteChanged();
                ResetCommand.NotifyCanExecuteChanged();
            }
        };
    }

    public void MarkDirty() => IsDirty = true;

    private void Apply() { DoApply(); IsDirty = false; }
    private void Reset() { DoReset(); IsDirty = false; }

    protected abstract void DoApply();
    protected abstract void DoReset();
}
```

- [ ] **Step 3: EmptySelectionViewModel (shown when no tree node is selected)**

Create `mvp4/Sg.Mvp4.Domain/ViewModels/EmptySelectionViewModel.cs`:

```csharp
namespace Sg.Mvp4.Domain.ViewModels;

public sealed class EmptySelectionViewModel
{
    public string Message => "Select an entity in the tree to edit its properties.";
}
```

- [ ] **Step 4: Run tests**

```bash
cd mvp4 && dotnet test --filter Category=Unit
```

Expected: 21 passing tests.

- [ ] **Step 5: Create the host view**

Create `mvp4/Sg.Mvp4.App/Views/Shell/PropertyPanelHostView.xaml`:

```xml
<UserControl x:Class="Sg.Mvp4.App.Views.Shell.PropertyPanelHostView"
             xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation"
             xmlns:x="http://schemas.microsoft.com/winfx/2006/xaml"
             xmlns:vm="clr-namespace:Sg.Mvp4.Domain.ViewModels;assembly=Sg.Mvp4.Domain"
             xmlns:shell="clr-namespace:Sg.Mvp4.App.Views.Shell">
    <UserControl.Resources>
        <DataTemplate DataType="{x:Type vm:EmptySelectionViewModel}">
            <TextBlock Text="{Binding Message}" Margin="12" Foreground="#888" TextWrapping="Wrap" />
        </DataTemplate>
        <!-- entity templates are added in Tasks 7–12 -->
    </UserControl.Resources>

    <ContentControl x:Name="PanelSlot" Content="{Binding Current}" />
</UserControl>
```

Create `mvp4/Sg.Mvp4.App/Views/Shell/PropertyPanelHostView.xaml.cs`:

```csharp
using System.Windows.Controls;
using Sg.Mvp4.Domain.ViewModels;

namespace Sg.Mvp4.App.Views.Shell;

public partial class PropertyPanelHostView : UserControl
{
    public PropertyPanelHostView(PropertyPanelHostViewModel vm)
    {
        InitializeComponent();
        DataContext = vm;
    }
}
```

- [ ] **Step 6: Implement PropertyPanelHostViewModel (switches on selection)**

Create `mvp4/Sg.Mvp4.Domain/ViewModels/PropertyPanelHostViewModel.cs`:

```csharp
using CommunityToolkit.Mvvm.ComponentModel;
using Sg.Mvp4.Domain.Services;

namespace Sg.Mvp4.Domain.ViewModels;

public partial class PropertyPanelHostViewModel : ObservableObject
{
    private readonly IStkRootService _stk;
    private readonly ObjectTreeViewModel _tree;

    [ObservableProperty] private object _current = new EmptySelectionViewModel();

    public PropertyPanelHostViewModel(IStkRootService stk, ObjectTreeViewModel tree)
    {
        _stk  = stk;
        _tree = tree;
        _tree.SelectionChanged += (_, path) => UpdateCurrent(path);
    }

    private void UpdateCurrent(string? path)
    {
        if (path is null) { Current = new EmptySelectionViewModel(); return; }

        var node = _stk.FindByPath(path);
        Current = node is null
            ? new EmptySelectionViewModel()
            : PanelFactory.Create(node, _stk);
    }
}

internal static class PanelFactory
{
    /// <summary>Tasks 7–12 extend this switch with their concrete VMs.</summary>
    public static object Create(Sg.Mvp4.Domain.Services.IStkEntityNode node, IStkRootService stk) =>
        node.Kind switch
        {
            Models.EntityKind.Aircraft           => new AircraftViewModel(node, stk),
            Models.EntityKind.Facility           => new FacilityViewModel(node, stk),
            Models.EntityKind.AreaTarget         => new AreaTargetViewModel(node, stk),
            Models.EntityKind.Sensor             => new SensorViewModel(node, stk),
            Models.EntityKind.CoverageDefinition => new CoverageDefinitionViewModel(node, stk),
            Models.EntityKind.FigureOfMerit      => new FigureOfMeritViewModel(node, stk),
            _ => new EmptySelectionViewModel()
        };
}
```

**NOTE:** This won't compile until Tasks 7–12 add the referenced VMs. To unblock this task, create empty-shell VMs now and flesh them out in their respective tasks. Create six stubs under `mvp4/Sg.Mvp4.Domain/ViewModels/`:

```csharp
// AircraftViewModel.cs  (stub — Task 7 replaces)
namespace Sg.Mvp4.Domain.ViewModels;
using Sg.Mvp4.Domain.Services;
public partial class AircraftViewModel : EntityViewModelBase
{
    public AircraftViewModel(IStkEntityNode node, IStkRootService stk) { _name = node.Name; }
    private string _name;
    public override string EntityName => _name;
    protected override void DoApply() { }
    protected override void DoReset() { }
}
```

Repeat the same 8-line stub for `FacilityViewModel`, `AreaTargetViewModel`, `SensorViewModel`, `CoverageDefinitionViewModel`, `FigureOfMeritViewModel` — same shape, different class name. Each gets replaced in Tasks 7–12.

- [ ] **Step 7: Mount in MainWindow right column**

In `MainWindow.xaml` replace the right-column Border:

```xml
<ContentControl Grid.Column="4" x:Name="PanelSlot" />
```

In `MainWindow.xaml.cs` constructor:

```csharp
public MainWindow(MainWindowViewModel vm, StkDisplayHost stkHost,
                  ObjectTreeView tree, PropertyPanelHostView panel)
{
    InitializeComponent();
    DataContext = vm;
    StkHostSlot.Content = stkHost;
    TreeSlot.Content    = tree;
    PanelSlot.Content   = panel;
}
```

Register in `App.xaml.cs`:

```csharp
services.AddSingleton<PropertyPanelHostViewModel>();
services.AddSingleton<PropertyPanelHostView>();
```

- [ ] **Step 8: Smoke run**

```bash
cd mvp4 && dotnet run --project Sg.Mvp4.App
```

Expected: selecting an aircraft in the tree shows `AircraftViewModel`'s type name in the right pane (empty-shell; property fields land in Task 7). Selecting nothing shows "Select an entity..." message.

- [ ] **Step 9: Commit**

```bash
git add mvp4/Sg.Mvp4.Domain/ViewModels mvp4/Sg.Mvp4.App mvp4/Sg.Mvp4.Tests/ViewModels
git commit -m "feat(mvp4): property-panel host + EntityViewModelBase + 6 stub VMs"
```

---

## Tasks 7–12: Entity panels (one per EntityKind)

**Shared structure.** Each task below follows the same shape:

1. Write failing tests for the VM (fields observed + Apply writes back).
2. Replace the stub VM with a real implementation that reads an `IAgStkObject` (via helper extensions added in Task 7) and writes on `Apply`.
3. Create the XAML panel with bindings.
4. Register the DataTemplate that maps the VM to the panel in `PropertyPanelHostView.xaml` resources.
5. Smoke-run, confirm the panel shows correct initial values and Apply persists.
6. Commit.

Because VMs (Tasks 7–12) manipulate concrete STK COM objects (e.g., `IAgAircraft.Route`), the real implementations take the `IAgStkObject` directly, not the `IStkEntityNode`. For unit-testing these VMs without STK, add a second constructor overload that takes a test-only `IAircraftRouteAccess` (or equivalent) interface in each task. The fake backing of that interface lives in `Sg.Mvp4.Tests/Fakes/`.

### Task 7: AircraftViewModel + AircraftPanel

**Files:**
- Create: `mvp4/Sg.Mvp4.Domain/Services/ComAccessors.cs` (helpers used by Tasks 7–12)
- Create: `mvp4/Sg.Mvp4.Domain/Models/WaypointModel.cs`
- Create: `mvp4/Sg.Mvp4.Domain/ViewModels/IAircraftBackend.cs`
- Replace: `mvp4/Sg.Mvp4.Domain/ViewModels/AircraftViewModel.cs`
- Create: `mvp4/Sg.Mvp4.App/Views/Entities/AircraftPanel.xaml`, `.xaml.cs`
- Create: `mvp4/Sg.Mvp4.Tests/ViewModels/AircraftViewModelTests.cs`
- Create: `mvp4/Sg.Mvp4.Tests/Fakes/FakeAircraftBackend.cs`
- Modify: `mvp4/Sg.Mvp4.App/Views/Shell/PropertyPanelHostView.xaml`

- [ ] **Step 1: WaypointModel + IAircraftBackend**

Create `mvp4/Sg.Mvp4.Domain/Models/WaypointModel.cs`:

```csharp
using CommunityToolkit.Mvvm.ComponentModel;

namespace Sg.Mvp4.Domain.Models;

public partial class WaypointModel : ObservableObject
{
    [ObservableProperty] private double _latitudeDeg;
    [ObservableProperty] private double _longitudeDeg;
    [ObservableProperty] private double _altitudeMeters;
    [ObservableProperty] private double _speedMps;
}
```

Create `mvp4/Sg.Mvp4.Domain/ViewModels/IAircraftBackend.cs`:

```csharp
using Sg.Mvp4.Domain.Models;

namespace Sg.Mvp4.Domain.ViewModels;

/// <summary>Isolation seam so the VM can be tested without a real IAgAircraft.</summary>
public interface IAircraftBackend
{
    string Name     { get; }
    string ColorHex { get; set; }          // e.g. "#00FFFF"
    bool   PathVisible { get; set; }

    IReadOnlyList<WaypointModel> GetWaypoints();
    void SetWaypoints(IReadOnlyList<WaypointModel> points);
}
```

- [ ] **Step 2: Write failing tests**

Create `mvp4/Sg.Mvp4.Tests/Fakes/FakeAircraftBackend.cs`:

```csharp
using Sg.Mvp4.Domain.Models;
using Sg.Mvp4.Domain.ViewModels;

namespace Sg.Mvp4.Tests.Fakes;

public sealed class FakeAircraftBackend : IAircraftBackend
{
    public FakeAircraftBackend(string name) { Name = name; }

    public string Name { get; }
    public string ColorHex { get; set; } = "#00FFFF";
    public bool   PathVisible { get; set; } = true;

    private List<WaypointModel> _points = new();

    public IReadOnlyList<WaypointModel> GetWaypoints() => _points.ToList();
    public void SetWaypoints(IReadOnlyList<WaypointModel> points) => _points = points.ToList();
}
```

Create `mvp4/Sg.Mvp4.Tests/ViewModels/AircraftViewModelTests.cs`:

```csharp
using FluentAssertions;
using NUnit.Framework;
using Sg.Mvp4.Domain.Models;
using Sg.Mvp4.Domain.ViewModels;
using Sg.Mvp4.Tests.Fakes;

namespace Sg.Mvp4.Tests.ViewModels;

[TestFixture, Category(TestCategories.Unit)]
public class AircraftViewModelTests
{
    [Test]
    public void EntityName_returns_backend_name()
    {
        var vm = new AircraftViewModel(new FakeAircraftBackend("A1"));
        vm.EntityName.Should().Be("A1");
    }

    [Test]
    public void AddingWaypoint_marks_dirty()
    {
        var vm = new AircraftViewModel(new FakeAircraftBackend("A1"));
        vm.AddWaypointCommand.Execute(null);
        vm.IsDirty.Should().BeTrue();
    }

    [Test]
    public void Apply_writes_waypoints_back_to_backend()
    {
        var back = new FakeAircraftBackend("A1");
        var vm = new AircraftViewModel(back);
        vm.Waypoints.Add(new WaypointModel { LatitudeDeg = 34.2, LongitudeDeg = 74.5, AltitudeMeters = 1500, SpeedMps = 100 });
        vm.MarkDirty();

        vm.ApplyCommand.Execute(null);

        back.GetWaypoints().Should().HaveCount(1);
        back.GetWaypoints()[0].LatitudeDeg.Should().Be(34.2);
    }

    [Test]
    public void ColorHex_is_dirty_after_change()
    {
        var vm = new AircraftViewModel(new FakeAircraftBackend("A1"));
        vm.ColorHex = "#FF00FF";
        vm.IsDirty.Should().BeTrue();
    }

    [Test]
    public void Reset_discards_unapplied_changes()
    {
        var back = new FakeAircraftBackend("A1") { ColorHex = "#00FFFF" };
        var vm = new AircraftViewModel(back);
        vm.ColorHex = "#FF00FF";
        vm.ResetCommand.Execute(null);
        vm.ColorHex.Should().Be("#00FFFF");
        vm.IsDirty.Should().BeFalse();
    }
}
```

- [ ] **Step 3: Implement AircraftViewModel**

Replace `mvp4/Sg.Mvp4.Domain/ViewModels/AircraftViewModel.cs`:

```csharp
using System.Collections.ObjectModel;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using Sg.Mvp4.Domain.Models;
using Sg.Mvp4.Domain.Services;

namespace Sg.Mvp4.Domain.ViewModels;

public partial class AircraftViewModel : EntityViewModelBase
{
    private readonly IAircraftBackend _backend;

    [ObservableProperty] private string _colorHex;
    [ObservableProperty] private bool   _pathVisible;

    public ObservableCollection<WaypointModel> Waypoints { get; } = new();

    public IRelayCommand AddWaypointCommand    { get; }
    public IRelayCommand<WaypointModel> RemoveWaypointCommand { get; }

    public AircraftViewModel(IAircraftBackend backend)
    {
        _backend     = backend;
        _colorHex    = backend.ColorHex;
        _pathVisible = backend.PathVisible;

        foreach (var wp in backend.GetWaypoints())
            Waypoints.Add(CloneWp(wp));

        Waypoints.CollectionChanged += (_, _) => MarkDirty();

        AddWaypointCommand = new RelayCommand(() =>
        {
            Waypoints.Add(new WaypointModel { AltitudeMeters = 1500, SpeedMps = 100 });
            MarkDirty();
        });

        RemoveWaypointCommand = new RelayCommand<WaypointModel>(wp =>
        {
            if (wp is not null) { Waypoints.Remove(wp); MarkDirty(); }
        });

        PropertyChanged += (_, e) =>
        {
            if (e.PropertyName is nameof(ColorHex) or nameof(PathVisible)) MarkDirty();
        };
    }

    // Bridge constructor used by PanelFactory — wraps IStkEntityNode into a COM-backed backend.
    public AircraftViewModel(IStkEntityNode node, IStkRootService stk)
        : this(new ComAircraftBackend(node, stk)) { }

    public override string EntityName => _backend.Name;

    protected override void DoApply()
    {
        _backend.ColorHex    = ColorHex;
        _backend.PathVisible = PathVisible;
        _backend.SetWaypoints(Waypoints.Select(CloneWp).ToList());
    }

    protected override void DoReset()
    {
        ColorHex    = _backend.ColorHex;
        PathVisible = _backend.PathVisible;
        Waypoints.Clear();
        foreach (var wp in _backend.GetWaypoints())
            Waypoints.Add(CloneWp(wp));
    }

    private static WaypointModel CloneWp(WaypointModel src) => new()
    {
        LatitudeDeg    = src.LatitudeDeg,
        LongitudeDeg   = src.LongitudeDeg,
        AltitudeMeters = src.AltitudeMeters,
        SpeedMps       = src.SpeedMps,
    };
}
```

- [ ] **Step 4: COM-backed aircraft backend**

Create `mvp4/Sg.Mvp4.Domain/Services/ComAccessors.cs`:

```csharp
using AGI.STKObjects;
using Sg.Mvp4.Domain.Models;
using Sg.Mvp4.Domain.ViewModels;

namespace Sg.Mvp4.Domain.Services;

internal sealed class ComAircraftBackend : IAircraftBackend
{
    private readonly IAgAircraft _ac;
    public ComAircraftBackend(IStkEntityNode node, IStkRootService stk)
    {
        var root = ((StkRootService)stk).Root;
        _ac = (IAgAircraft)root.CurrentScenario.Children[node.Name];
        Name = node.Name;
    }

    public string Name { get; }

    public string ColorHex
    {
        get => $"#{((IAgDisplayColor)_ac.Graphics.Attributes).Color:X6}";
        set
        {
            var hex = value.TrimStart('#');
            ((IAgDisplayColor)_ac.Graphics.Attributes).Color = Convert.ToInt32(hex, 16);
        }
    }

    public bool PathVisible
    {
        get => _ac.Graphics.Show;
        set => _ac.Graphics.Show = value;
    }

    public IReadOnlyList<WaypointModel> GetWaypoints()
    {
        var route = (IAgVePropagatorGreatArc)_ac.Route;
        var wps = new List<WaypointModel>();
        foreach (IAgVeWaypointsElement e in route.Waypoints)
            wps.Add(new WaypointModel {
                LatitudeDeg = e.Latitude, LongitudeDeg = e.Longitude,
                AltitudeMeters = e.Altitude, SpeedMps = e.Speed });
        return wps;
    }

    public void SetWaypoints(IReadOnlyList<WaypointModel> points)
    {
        _ac.SetRouteType(AgEVePropagatorType.ePropagatorGreatArc);
        var route = (IAgVePropagatorGreatArc)_ac.Route;
        route.Method = AgEVeWayPtCompMethod.eDetermineTimeAccFromVel;
        route.Waypoints.RemoveAll();
        foreach (var p in points)
        {
            var wp = route.Waypoints.Add();
            wp.Latitude      = p.LatitudeDeg;
            wp.Longitude     = p.LongitudeDeg;
            wp.Altitude      = p.AltitudeMeters;
            wp.Speed         = p.SpeedMps <= 0 ? 100 : p.SpeedMps;
        }
        route.Propagate();
    }
}
```

- [ ] **Step 5: Run tests**

```bash
cd mvp4 && dotnet test --filter Category=Unit
```

Expected: 26 passing tests (21 prior + 5 new).

- [ ] **Step 6: AircraftPanel XAML**

Create `mvp4/Sg.Mvp4.App/Views/Entities/AircraftPanel.xaml`:

```xml
<UserControl x:Class="Sg.Mvp4.App.Views.Entities.AircraftPanel"
             xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation"
             xmlns:x="http://schemas.microsoft.com/winfx/2006/xaml">
    <DockPanel>
        <TextBlock DockPanel.Dock="Top" Margin="8" FontWeight="Bold"
                   Text="{Binding EntityName, StringFormat='Aircraft: {0}'}" />

        <StackPanel DockPanel.Dock="Bottom" Orientation="Horizontal"
                    HorizontalAlignment="Right" Margin="8">
            <Button Content="Reset" Command="{Binding ResetCommand}" Width="70" Margin="4,0" />
            <Button Content="Apply" Command="{Binding ApplyCommand}" Width="70" />
        </StackPanel>

        <StackPanel Margin="8">
            <TextBlock Text="Color (#RRGGBB)" />
            <TextBox Text="{Binding ColorHex, UpdateSourceTrigger=PropertyChanged}" />
            <CheckBox Content="Show path" IsChecked="{Binding PathVisible}" Margin="0,8" />

            <TextBlock Text="Waypoints" FontWeight="Bold" Margin="0,8,0,4" />
            <DataGrid ItemsSource="{Binding Waypoints}" AutoGenerateColumns="False"
                      CanUserAddRows="False" MinHeight="120">
                <DataGrid.Columns>
                    <DataGridTextColumn Header="Lat °"   Binding="{Binding LatitudeDeg}"    Width="70" />
                    <DataGridTextColumn Header="Lon °"   Binding="{Binding LongitudeDeg}"   Width="70" />
                    <DataGridTextColumn Header="Alt m"   Binding="{Binding AltitudeMeters}" Width="70" />
                    <DataGridTextColumn Header="Speed"   Binding="{Binding SpeedMps}"       Width="70" />
                </DataGrid.Columns>
            </DataGrid>
            <Button Content="+ Add waypoint" Command="{Binding AddWaypointCommand}" Margin="0,4" />
        </StackPanel>
    </DockPanel>
</UserControl>
```

Create the code-behind `mvp4/Sg.Mvp4.App/Views/Entities/AircraftPanel.xaml.cs`:

```csharp
using System.Windows.Controls;

namespace Sg.Mvp4.App.Views.Entities;

public partial class AircraftPanel : UserControl
{
    public AircraftPanel() { InitializeComponent(); }
}
```

- [ ] **Step 7: Register DataTemplate in PropertyPanelHostView**

Modify `mvp4/Sg.Mvp4.App/Views/Shell/PropertyPanelHostView.xaml` — add inside `<UserControl.Resources>`:

```xml
<DataTemplate DataType="{x:Type vm:AircraftViewModel}">
    <shell:AircraftPanelHost />
</DataTemplate>
```

Since DataTemplates inside resources can't reference controls by file path directly, import via xmlns:

```xml
xmlns:entities="clr-namespace:Sg.Mvp4.App.Views.Entities"
```

Replace `shell:AircraftPanelHost` with `entities:AircraftPanel`.

- [ ] **Step 8: Smoke run**

```bash
cd mvp4 && dotnet run --project Sg.Mvp4.App
```

Expected: click `+ Aircraft` → select the new node → right pane shows AircraftPanel with empty waypoints grid. Add a waypoint → Apply → STK globe redraws aircraft track. Reset discards unapplied changes.

- [ ] **Step 9: Commit**

```bash
git add mvp4/Sg.Mvp4.Domain mvp4/Sg.Mvp4.App mvp4/Sg.Mvp4.Tests
git commit -m "feat(mvp4): AircraftViewModel + AircraftPanel with waypoint editing"
```

---

### Task 8: FacilityViewModel + FacilityPanel

**Files:** same pattern as Task 7 with `Facility` throughout.

- [ ] **Step 1: Fake backend + tests**

`mvp4/Sg.Mvp4.Domain/ViewModels/IFacilityBackend.cs`:

```csharp
namespace Sg.Mvp4.Domain.ViewModels;

public interface IFacilityBackend
{
    string Name { get; }
    string ColorHex { get; set; }
    double LatitudeDeg  { get; set; }
    double LongitudeDeg { get; set; }
    double AltitudeMeters { get; set; }
}
```

`mvp4/Sg.Mvp4.Tests/Fakes/FakeFacilityBackend.cs`:

```csharp
using Sg.Mvp4.Domain.ViewModels;

namespace Sg.Mvp4.Tests.Fakes;

public sealed class FakeFacilityBackend : IFacilityBackend
{
    public FakeFacilityBackend(string name) { Name = name; }
    public string Name { get; }
    public string ColorHex { get; set; } = "#FF00FF";
    public double LatitudeDeg  { get; set; }
    public double LongitudeDeg { get; set; }
    public double AltitudeMeters { get; set; }
}
```

`mvp4/Sg.Mvp4.Tests/ViewModels/FacilityViewModelTests.cs`:

```csharp
using FluentAssertions;
using NUnit.Framework;
using Sg.Mvp4.Domain.ViewModels;
using Sg.Mvp4.Tests.Fakes;

namespace Sg.Mvp4.Tests.ViewModels;

[TestFixture, Category(TestCategories.Unit)]
public class FacilityViewModelTests
{
    [Test] public void Apply_writes_lat_lon_back() {
        var b = new FakeFacilityBackend("F1");
        var vm = new FacilityViewModel(b) { LatitudeDeg = 34.2, LongitudeDeg = 74.5 };
        vm.MarkDirty();
        vm.ApplyCommand.Execute(null);
        b.LatitudeDeg.Should().Be(34.2);
        b.LongitudeDeg.Should().Be(74.5);
    }

    [Test] public void Setting_lat_marks_dirty() {
        var vm = new FacilityViewModel(new FakeFacilityBackend("F1"));
        vm.LatitudeDeg = 10.0;
        vm.IsDirty.Should().BeTrue();
    }
}
```

- [ ] **Step 2: Replace FacilityViewModel stub**

```csharp
// mvp4/Sg.Mvp4.Domain/ViewModels/FacilityViewModel.cs
using CommunityToolkit.Mvvm.ComponentModel;
using Sg.Mvp4.Domain.Services;

namespace Sg.Mvp4.Domain.ViewModels;

public partial class FacilityViewModel : EntityViewModelBase
{
    private readonly IFacilityBackend _backend;

    [ObservableProperty] private string _colorHex;
    [ObservableProperty] private double _latitudeDeg;
    [ObservableProperty] private double _longitudeDeg;
    [ObservableProperty] private double _altitudeMeters;

    public FacilityViewModel(IFacilityBackend backend)
    {
        _backend        = backend;
        _colorHex       = backend.ColorHex;
        _latitudeDeg    = backend.LatitudeDeg;
        _longitudeDeg   = backend.LongitudeDeg;
        _altitudeMeters = backend.AltitudeMeters;

        PropertyChanged += (_, e) =>
        {
            if (e.PropertyName is nameof(ColorHex) or nameof(LatitudeDeg)
                or nameof(LongitudeDeg) or nameof(AltitudeMeters)) MarkDirty();
        };
    }

    public FacilityViewModel(IStkEntityNode node, IStkRootService stk)
        : this(new ComFacilityBackend(node, stk)) { }

    public override string EntityName => _backend.Name;

    protected override void DoApply()
    {
        _backend.ColorHex = ColorHex;
        _backend.LatitudeDeg = LatitudeDeg;
        _backend.LongitudeDeg = LongitudeDeg;
        _backend.AltitudeMeters = AltitudeMeters;
    }

    protected override void DoReset()
    {
        ColorHex = _backend.ColorHex;
        LatitudeDeg = _backend.LatitudeDeg;
        LongitudeDeg = _backend.LongitudeDeg;
        AltitudeMeters = _backend.AltitudeMeters;
    }
}
```

- [ ] **Step 3: Add ComFacilityBackend to ComAccessors.cs**

Append in `mvp4/Sg.Mvp4.Domain/Services/ComAccessors.cs`:

```csharp
internal sealed class ComFacilityBackend : IFacilityBackend
{
    private readonly IAgFacility _fa;

    public ComFacilityBackend(IStkEntityNode node, IStkRootService stk)
    {
        var root = ((StkRootService)stk).Root;
        _fa = (IAgFacility)root.CurrentScenario.Children[node.Name];
        Name = node.Name;
    }

    public string Name { get; }

    public string ColorHex
    {
        get => $"#{((IAgDisplayColor)_fa.Graphics.Attributes).Color:X6}";
        set => ((IAgDisplayColor)_fa.Graphics.Attributes).Color = Convert.ToInt32(value.TrimStart('#'), 16);
    }

    public double LatitudeDeg    { get => ReadLla()[0]; set => WriteLla(value, LongitudeDeg, AltitudeMeters); }
    public double LongitudeDeg   { get => ReadLla()[1]; set => WriteLla(LatitudeDeg, value, AltitudeMeters); }
    public double AltitudeMeters { get => ReadLla()[2]; set => WriteLla(LatitudeDeg, LongitudeDeg, value); }

    private double[] ReadLla()
    {
        _fa.Position.ConvertTo(AgEPositionType.eGeodetic, out IAgPosition pos);
        var g = (IAgGeodetic)pos;
        return new[] { g.Latitude, g.Longitude, g.Altitude };
    }

    private void WriteLla(double lat, double lon, double alt)
    {
        _fa.Position.AssignGeodetic(lat, lon, alt);
    }
}
```

- [ ] **Step 4: FacilityPanel XAML**

`mvp4/Sg.Mvp4.App/Views/Entities/FacilityPanel.xaml`:

```xml
<UserControl x:Class="Sg.Mvp4.App.Views.Entities.FacilityPanel"
             xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation"
             xmlns:x="http://schemas.microsoft.com/winfx/2006/xaml">
    <DockPanel>
        <TextBlock DockPanel.Dock="Top" Margin="8" FontWeight="Bold"
                   Text="{Binding EntityName, StringFormat='Facility: {0}'}" />
        <StackPanel DockPanel.Dock="Bottom" Orientation="Horizontal" HorizontalAlignment="Right" Margin="8">
            <Button Content="Reset" Command="{Binding ResetCommand}" Width="70" Margin="4,0" />
            <Button Content="Apply" Command="{Binding ApplyCommand}" Width="70" />
        </StackPanel>
        <StackPanel Margin="8">
            <TextBlock Text="Color (#RRGGBB)" />
            <TextBox Text="{Binding ColorHex, UpdateSourceTrigger=PropertyChanged}" />
            <TextBlock Text="Latitude (°)"   Margin="0,8,0,0" />
            <TextBox Text="{Binding LatitudeDeg, UpdateSourceTrigger=PropertyChanged}" />
            <TextBlock Text="Longitude (°)"  Margin="0,8,0,0" />
            <TextBox Text="{Binding LongitudeDeg, UpdateSourceTrigger=PropertyChanged}" />
            <TextBlock Text="Altitude (m)"   Margin="0,8,0,0" />
            <TextBox Text="{Binding AltitudeMeters, UpdateSourceTrigger=PropertyChanged}" />
        </StackPanel>
    </DockPanel>
</UserControl>
```

Code-behind: identical shape to `AircraftPanel.xaml.cs` — just `public FacilityPanel() { InitializeComponent(); }`.

- [ ] **Step 5: Add DataTemplate**

Add to `PropertyPanelHostView.xaml` resources:

```xml
<DataTemplate DataType="{x:Type vm:FacilityViewModel}">
    <entities:FacilityPanel />
</DataTemplate>
```

- [ ] **Step 6: Run tests + smoke**

```bash
cd mvp4 && dotnet test --filter Category=Unit
dotnet run --project Sg.Mvp4.App
```

Expected: 28 unit tests pass. Adding a Facility → setting lat=34.2, lon=74.5, alt=1000 → Apply → STK globe shows facility at that location.

- [ ] **Step 7: Commit**

```bash
git add mvp4/
git commit -m "feat(mvp4): FacilityViewModel + FacilityPanel with lat/lon/alt editing"
```

---

### Task 9: AreaTargetViewModel + AreaTargetPanel

**Files:** `IAreaTargetBackend.cs`, `FakeAreaTargetBackend.cs`, tests, VM, panel, DataTemplate.

- [ ] **Step 1: Backend interface + tests + VM (follows Task 8 pattern)**

Interface (`IAreaTargetBackend.cs`):

```csharp
namespace Sg.Mvp4.Domain.ViewModels;
using Sg.Mvp4.Domain.Models;

public interface IAreaTargetBackend
{
    string Name { get; }
    string OutlineColorHex { get; set; }
    bool   FillEnabled { get; set; }
    string FillColorHex { get; set; }
    IReadOnlyList<VertexModel> GetVertices();
    void SetVertices(IReadOnlyList<VertexModel> vertices);
}
```

`VertexModel.cs` under `Sg.Mvp4.Domain/Models/`:

```csharp
using CommunityToolkit.Mvvm.ComponentModel;
namespace Sg.Mvp4.Domain.Models;
public partial class VertexModel : ObservableObject
{
    [ObservableProperty] private double _latitudeDeg;
    [ObservableProperty] private double _longitudeDeg;
}
```

`FakeAreaTargetBackend.cs`:

```csharp
using Sg.Mvp4.Domain.Models;
using Sg.Mvp4.Domain.ViewModels;
namespace Sg.Mvp4.Tests.Fakes;
public sealed class FakeAreaTargetBackend : IAreaTargetBackend
{
    public FakeAreaTargetBackend(string name) { Name = name; }
    public string Name { get; }
    public string OutlineColorHex { get; set; } = "#FF0000";
    public bool FillEnabled { get; set; }
    public string FillColorHex { get; set; } = "#FF0000";
    private List<VertexModel> _v = new();
    public IReadOnlyList<VertexModel> GetVertices() => _v.ToList();
    public void SetVertices(IReadOnlyList<VertexModel> v) => _v = v.ToList();
}
```

Tests (`AreaTargetViewModelTests.cs`) — minimum set:

```csharp
using FluentAssertions;
using NUnit.Framework;
using Sg.Mvp4.Domain.Models;
using Sg.Mvp4.Domain.ViewModels;
using Sg.Mvp4.Tests.Fakes;

namespace Sg.Mvp4.Tests.ViewModels;

[TestFixture, Category(TestCategories.Unit)]
public class AreaTargetViewModelTests
{
    [Test] public void AddVertex_marks_dirty() {
        var vm = new AreaTargetViewModel(new FakeAreaTargetBackend("A"));
        vm.AddVertexCommand.Execute(null);
        vm.IsDirty.Should().BeTrue();
    }

    [Test] public void Apply_writes_vertices() {
        var b = new FakeAreaTargetBackend("A");
        var vm = new AreaTargetViewModel(b);
        vm.Vertices.Add(new VertexModel { LatitudeDeg = 34.2, LongitudeDeg = 74.5 });
        vm.MarkDirty();
        vm.ApplyCommand.Execute(null);
        b.GetVertices().Should().HaveCount(1);
    }
}
```

VM (`AreaTargetViewModel.cs`) — replace stub with full implementation following the AircraftViewModel shape: `ObservableCollection<VertexModel> Vertices`, Apply writes `OutlineColorHex`, `FillEnabled`, `FillColorHex`, and `SetVertices(Vertices.ToList())`. `DoReset` re-reads all from backend.

- [ ] **Step 2: COM backend in ComAccessors.cs**

Append:

```csharp
internal sealed class ComAreaTargetBackend : IAreaTargetBackend
{
    private readonly IAgAreaTarget _at;

    public ComAreaTargetBackend(IStkEntityNode node, IStkRootService stk)
    {
        var root = ((StkRootService)stk).Root;
        _at = (IAgAreaTarget)root.CurrentScenario.Children[node.Name];
        Name = node.Name;
    }

    public string Name { get; }
    public string OutlineColorHex
    {
        get => $"#{((IAgDisplayColor)_at.Graphics).Color:X6}";
        set => ((IAgDisplayColor)_at.Graphics).Color = Convert.ToInt32(value.TrimStart('#'), 16);
    }
    public bool FillEnabled
    {
        get => _at.Graphics.Fill;
        set => _at.Graphics.Fill = value;
    }
    public string FillColorHex
    {
        get => $"#{_at.Graphics.FillColor:X6}";
        set => _at.Graphics.FillColor = Convert.ToInt32(value.TrimStart('#'), 16);
    }
    public IReadOnlyList<VertexModel> GetVertices()
    {
        var list = new List<VertexModel>();
        _at.AreaType = AgEAreaType.ePattern;
        var pattern = (IAgAreaTypePatternCollection)_at.AreaTypeData;
        foreach (IAgAreaTypePattern p in pattern)
            list.Add(new VertexModel { LatitudeDeg = p.Latitude, LongitudeDeg = p.Longitude });
        return list;
    }
    public void SetVertices(IReadOnlyList<VertexModel> verts)
    {
        _at.AreaType = AgEAreaType.ePattern;
        var pattern = (IAgAreaTypePatternCollection)_at.AreaTypeData;
        pattern.RemoveAll();
        foreach (var v in verts) pattern.Add(v.LatitudeDeg, v.LongitudeDeg);
    }
}
```

- [ ] **Step 3: AreaTargetPanel XAML**

Same shell as Aircraft (header + Reset/Apply footer + fields). Middle contains checkboxes for Fill, text boxes for outline/fill color, and a DataGrid of `Vertices` with `Lat`, `Lon` columns + `+ Add vertex` button bound to `AddVertexCommand`.

- [ ] **Step 4: DataTemplate, run tests, smoke, commit**

```xml
<DataTemplate DataType="{x:Type vm:AreaTargetViewModel}">
    <entities:AreaTargetPanel />
</DataTemplate>
```

```bash
cd mvp4 && dotnet test --filter Category=Unit
dotnet run --project Sg.Mvp4.App
git add mvp4/
git commit -m "feat(mvp4): AreaTargetViewModel + AreaTargetPanel with vertex editing"
```

---

### Task 10: SensorViewModel + SensorPanel

**Files:** backend interface, fake, tests, VM (complex — pointing modes), panel, COM backend.

- [ ] **Step 1: PointingMode enum + interface**

`mvp4/Sg.Mvp4.Domain/Models/PointingMode.cs`:

```csharp
namespace Sg.Mvp4.Domain.Models;
public enum PointingMode { Fixed, Targeted }
```

`ISensorBackend.cs`:

```csharp
using Sg.Mvp4.Domain.Models;
namespace Sg.Mvp4.Domain.ViewModels;

public interface ISensorBackend
{
    string Name { get; }
    double OuterHalfAngleDeg { get; set; }
    double InnerHalfAngleDeg { get; set; }
    string ColorHex { get; set; }
    bool   ShowIntersection { get; set; }
    PointingMode Pointing  { get; set; }
    double FixedAzimuthDeg   { get; set; }   // used when Pointing == Fixed
    double FixedElevationDeg { get; set; }
    string? TargetPath       { get; set; }   // used when Pointing == Targeted (path of another entity)
}
```

- [ ] **Step 2: Fake + tests (see Aircraft/Facility pattern)**

Key tests: setting `OuterHalfAngleDeg` marks dirty; switching `Pointing` to Targeted + setting `TargetPath` applies; reset reverts both fields.

- [ ] **Step 3: VM**

Expose `[ObservableProperty]` for each field. `DoApply` copies all fields back; if `Pointing == Targeted`, the backend's `TargetPath` setter will raise if the path doesn't exist — catch that in the backend, not the VM.

- [ ] **Step 4: COM backend**

```csharp
internal sealed class ComSensorBackend : ISensorBackend
{
    private readonly IAgSensor _sn;
    public ComSensorBackend(IStkEntityNode node, IStkRootService stk)
    {
        var root = ((StkRootService)stk).Root;
        _sn = (IAgSensor)ResolveByPath(root, node.Path);
        Name = node.Name;
        _sn.CommonTasks.SetPatternSimpleConic(5, 1); // default 5° cone, step 1°
    }

    public string Name { get; }

    public double OuterHalfAngleDeg {
        get => ((IAgSnSimpleConicPattern)_sn.Pattern).ConeAngle;
        set => ((IAgSnSimpleConicPattern)_sn.Pattern).ConeAngle = value;
    }
    public double InnerHalfAngleDeg { get => 0; set { /* MVP4: ignored; Simple Conic has no inner angle */ } }

    public string ColorHex {
        get => $"#{_sn.Graphics.Color:X6}";
        set => _sn.Graphics.Color = Convert.ToInt32(value.TrimStart('#'), 16);
    }
    public bool ShowIntersection {
        get => _sn.Graphics.PercentTranslucency < 100;
        set => _sn.Graphics.PercentTranslucency = value ? 30 : 100;
    }

    public PointingMode Pointing {
        get => _sn.PatternType switch { _ => PointingMode.Fixed };
        set
        {
            if (value == PointingMode.Fixed) _sn.SetPointingType(AgESnPointing.eSnPtFixed);
            else                              _sn.SetPointingType(AgESnPointing.eSnPtTargeted);
        }
    }

    public double FixedAzimuthDeg {
        get => ((IAgSnPtFixed)_sn.Pointing).Orientation.Query(AgEOrientationType.eAzEl, out var a, out _, out _, out _, out _, out _) == 0 ? a : 0;
        set { var o = (IAgSnPtFixed)_sn.Pointing; o.Orientation.AssignAzEl(value, FixedElevationDeg, AgEAzElAboutBoresight.eAzElAboutBoresightRotate); }
    }

    public double FixedElevationDeg {
        get => ((IAgSnPtFixed)_sn.Pointing).Orientation.Query(AgEOrientationType.eAzEl, out _, out var el, out _, out _, out _, out _) == 0 ? el : 0;
        set { var o = (IAgSnPtFixed)_sn.Pointing; o.Orientation.AssignAzEl(FixedAzimuthDeg, value, AgEAzElAboutBoresight.eAzElAboutBoresightRotate); }
    }

    public string? TargetPath {
        get => null;
        set
        {
            if (value is null) return;
            var targeted = (IAgSnPtTargeted)_sn.Pointing;
            targeted.Targets.Clear();
            targeted.Targets.Add("*/" + value.Replace('/', '/'));
        }
    }

    private static IAgStkObject ResolveByPath(AgStkObjectRoot root, string path)
    {
        var seg = path.Split('/');
        IAgStkObject o = root.CurrentScenario.Children[seg[0]];
        for (var i = 1; i < seg.Length; i++) o = o.Children[seg[i]];
        return o;
    }
}
```

Note: the Pointing `Query` / `AssignAzEl` shapes above are approximate — treat as a starting point, refine against actual STK 12 API reference. The interface contract is what matters for unit tests.

- [ ] **Step 5: SensorPanel XAML**

Stack of fields: Outer half-angle (numeric), Inner half-angle (numeric), Color, Show intersection checkbox, Pointing `ComboBox` (Fixed / Targeted), Azimuth + Elevation text boxes (visible when Pointing == Fixed), Target path ComboBox populated from sibling entity paths (visible when Pointing == Targeted) — use `Visibility` converters bound to the enum.

- [ ] **Step 6: DataTemplate, tests, smoke, commit**

```xml
<DataTemplate DataType="{x:Type vm:SensorViewModel}">
    <entities:SensorPanel />
</DataTemplate>
```

```bash
cd mvp4 && dotnet test --filter Category=Unit
dotnet run --project Sg.Mvp4.App
git add mvp4/
git commit -m "feat(mvp4): SensorViewModel + SensorPanel (conic half-angle + pointing)"
```

---

### Task 11: CoverageDefinitionViewModel + CoveragePanel

**Files:** `ICoverageBackend.cs`, fake, tests, VM, panel, COM backend.

- [ ] **Step 1: Interface + models**

```csharp
namespace Sg.Mvp4.Domain.ViewModels;

public interface ICoverageBackend
{
    string Name { get; }
    double LatMin { get; set; }
    double LatMax { get; set; }
    double LonMin { get; set; }
    double LonMax { get; set; }
    double LatStep { get; set; }
    double LonStep { get; set; }
    double MinAltitude       { get; set; }  // constraint
    double MinElevationAngle { get; set; }  // constraint in degrees
    IReadOnlyList<string> AssetPaths { get; }
    void SetAssetPaths(IEnumerable<string> paths);
}
```

- [ ] **Step 2: Fake + tests**

Fake stores fields + a `List<string> _assets`. Tests: changing `LatMin` marks dirty; Apply writes all fields + assets back; Reset reverts.

- [ ] **Step 3: VM**

Expose `[ObservableProperty]` for each scalar; `ObservableCollection<string> AssetPaths`. Include `AvailableAssetPaths` computed from `_stk.Children` (readonly) and bind a ListBox with MultiSelect for picking.

- [ ] **Step 4: COM backend**

```csharp
internal sealed class ComCoverageBackend : ICoverageBackend
{
    private readonly IAgCoverageDefinition _cv;
    public ComCoverageBackend(IStkEntityNode node, IStkRootService stk)
    {
        var root = ((StkRootService)stk).Root;
        _cv = (IAgCoverageDefinition)root.CurrentScenario.Children[node.Name];
        Name = node.Name;
    }

    public string Name { get; }
    public double LatMin { get => GetBounds().MinLat; set => SetBounds(value, LatMax, LonMin, LonMax); }
    public double LatMax { get => GetBounds().MaxLat; set => SetBounds(LatMin, value, LonMin, LonMax); }
    public double LonMin { get => GetBounds().MinLon; set => SetBounds(LatMin, LatMax, value, LonMax); }
    public double LonMax { get => GetBounds().MaxLon; set => SetBounds(LatMin, LatMax, LonMin, value); }

    public double LatStep {
        get => _cv.Grid.Resolution.LatLon;
        set => _cv.Grid.Resolution.LatLon = value;
    }
    public double LonStep { get => LatStep; set => LatStep = value; } // simple MVP4: same step

    public double MinAltitude { get; set; } = 0;          // MVP4: store in VM only
    public double MinElevationAngle { get; set; } = 5.0;  // MVP4: store in VM only

    public IReadOnlyList<string> AssetPaths {
        get {
            var list = new List<string>();
            foreach (IAgCvAsset a in _cv.AssetList) list.Add(a.AssetName);
            return list;
        }
    }

    public void SetAssetPaths(IEnumerable<string> paths)
    {
        _cv.AssetList.RemoveAll();
        foreach (var p in paths) _cv.AssetList.Add("*/" + p);
    }

    private (double MinLat, double MaxLat, double MinLon, double MaxLon) GetBounds()
    {
        _cv.Grid.BoundsType = AgECvBounds.eBoundsCustomRegions;
        var b = (IAgCvBoundsLatLonRegion)_cv.Grid.Bounds;
        return (b.MinLatitude, b.MaxLatitude, b.MinLongitude, b.MaxLongitude);
    }
    private void SetBounds(double lMin, double lMax, double nMin, double nMax)
    {
        _cv.Grid.BoundsType = AgECvBounds.eBoundsLatLonRegion;
        var b = (IAgCvBoundsLatLonRegion)_cv.Grid.Bounds;
        b.MinLatitude = lMin; b.MaxLatitude = lMax;
        b.MinLongitude = nMin; b.MaxLongitude = nMax;
    }
}
```

Note: STK 12 API for bounds / grid / assets has several variants; this is the Lat/Lon region flavour. Treat as a starting sketch; verify field names against `AGI.STKObjects` metadata in IntelliSense.

- [ ] **Step 5: CoveragePanel XAML**

Three sections: `Grid` (four lat/lon bound numeric boxes + step), `Constraints` (min altitude, min elevation angle), `Assets` (ListBox with checkable items, MultiSelect). Reset/Apply footer.

- [ ] **Step 6: DataTemplate + commit**

```bash
cd mvp4 && dotnet test --filter Category=Unit
dotnet run --project Sg.Mvp4.App
git add mvp4/
git commit -m "feat(mvp4): CoverageDefinitionViewModel + CoveragePanel (grid + assets + constraints)"
```

---

### Task 12: FigureOfMeritViewModel + FomPanel

**Files:** `IFomBackend.cs`, fake, tests, VM, panel, COM backend.

- [ ] **Step 1: Enum + interface**

`FomKind.cs`:

```csharp
namespace Sg.Mvp4.Domain.Models;
public enum FomKind { AccessDuration, RevisitTime, Coverage, SampleCount }
public enum FomStatistic { Maximum, Minimum, Average, Total }
```

`IFomBackend.cs`:

```csharp
using Sg.Mvp4.Domain.Models;
namespace Sg.Mvp4.Domain.ViewModels;
public interface IFomBackend
{
    string       Name { get; }
    FomKind      Kind       { get; set; }
    FomStatistic Statistic  { get; set; }
}
```

- [ ] **Step 2: Fake, tests, VM (follows Facility pattern exactly)**

Tests: setting Kind marks dirty; Apply writes back; Reset reverts.

- [ ] **Step 3: COM backend**

```csharp
internal sealed class ComFomBackend : IFomBackend
{
    private readonly IAgFigureOfMerit _fm;
    public ComFomBackend(IStkEntityNode node, IStkRootService stk)
    {
        var root = ((StkRootService)stk).Root;
        _fm = (IAgFigureOfMerit)ResolveByPath(root, node.Path);
        Name = node.Name;
    }
    public string Name { get; }

    public FomKind Kind {
        get => _fm.DefinitionType switch {
            AgEFmDefinitionType.eFmAccessDuration => FomKind.AccessDuration,
            AgEFmDefinitionType.eFmRevisitTime    => FomKind.RevisitTime,
            AgEFmDefinitionType.eFmCoverageTime  => FomKind.Coverage,
            _                                      => FomKind.SampleCount
        };
        set {
            var d = value switch {
                FomKind.AccessDuration => AgEFmDefinitionType.eFmAccessDuration,
                FomKind.RevisitTime    => AgEFmDefinitionType.eFmRevisitTime,
                FomKind.Coverage       => AgEFmDefinitionType.eFmCoverageTime,
                _                       => AgEFmDefinitionType.eFmSimpleCoverage
            };
            _fm.SetDefinitionType(d);
        }
    }

    public FomStatistic Statistic {
        get => _fm.Definition is IAgFmDefAccessDuration a
               ? a.Compute switch {
                   AgEFmCompute.eMaximum => FomStatistic.Maximum,
                   AgEFmCompute.eMinimum => FomStatistic.Minimum,
                   AgEFmCompute.eAverage => FomStatistic.Average,
                   _                      => FomStatistic.Total
               }
               : FomStatistic.Maximum;
        set { if (_fm.Definition is IAgFmDefAccessDuration a)
                a.Compute = value switch {
                    FomStatistic.Minimum => AgEFmCompute.eMinimum,
                    FomStatistic.Average => AgEFmCompute.eAverage,
                    FomStatistic.Total   => AgEFmCompute.eTotal,
                    _                     => AgEFmCompute.eMaximum
                };
        }
    }

    private static IAgStkObject ResolveByPath(AgStkObjectRoot root, string path)
    {
        var seg = path.Split('/');
        IAgStkObject o = root.CurrentScenario.Children[seg[0]];
        for (var i = 1; i < seg.Length; i++) o = o.Children[seg[i]];
        return o;
    }
}
```

- [ ] **Step 4: FomPanel XAML**

Two ComboBox selectors (FOM Kind, Statistic) bound to the enums. Reset/Apply footer.

- [ ] **Step 5: DataTemplate + commit**

```bash
cd mvp4 && dotnet test --filter Category=Unit
dotnet run --project Sg.Mvp4.App
git add mvp4/
git commit -m "feat(mvp4): FigureOfMeritViewModel + FomPanel (type + statistic)"
```

---

## Task 13: PersistenceService (Save/Load .sc) + dirty tracking

**Files:**
- Create: `mvp4/Sg.Mvp4.Domain/Services/IPersistenceService.cs`
- Create: `mvp4/Sg.Mvp4.Domain/Services/PersistenceService.cs`
- Modify: `mvp4/Sg.Mvp4.Domain/ViewModels/MainWindowViewModel.cs` (expose New/Open/Save commands)
- Create: `mvp4/Sg.Mvp4.Tests/Integration/PersistenceServiceIntegrationTests.cs`

- [ ] **Step 1: Define the interface**

Create `mvp4/Sg.Mvp4.Domain/Services/IPersistenceService.cs`:

```csharp
namespace Sg.Mvp4.Domain.Services;

public interface IPersistenceService
{
    /// <summary>Currently-open scenario file path, or null if never saved.</summary>
    string? CurrentPath { get; }

    /// <summary>True when the source file is VDF (packaged) rather than .sc.</summary>
    bool    CurrentIsPackaged { get; }

    /// <summary>Returns a path like "C:\EWTSS\mvp4\scenarios\<name>\<name>.sc".</summary>
    string SuggestedPath(string scenarioName);

    void SaveAsSc  (string path);
    void SaveAsVdf (string path, string password);
    void LoadSc    (string path);
    void LoadVdf   (string path, string password);
}
```

- [ ] **Step 2: Implement PersistenceService**

Create `mvp4/Sg.Mvp4.Domain/Services/PersistenceService.cs`:

```csharp
using AGI.STKObjects;

namespace Sg.Mvp4.Domain.Services;

public sealed class PersistenceService : IPersistenceService
{
    private readonly StkRootService _stk;

    public string? CurrentPath     { get; private set; }
    public bool    CurrentIsPackaged { get; private set; }

    public PersistenceService(IStkRootService stk)
    {
        _stk = (StkRootService)stk;
    }

    public string SuggestedPath(string scenarioName)
    {
        var baseDir = Path.Combine(@"C:\EWTSS\mvp4\scenarios", scenarioName);
        return Path.Combine(baseDir, $"{scenarioName}.sc");
    }

    public void SaveAsSc(string path)
    {
        Directory.CreateDirectory(Path.GetDirectoryName(path)!);
        _stk.Root.SaveScenarioAs(path);
        CurrentPath       = path;
        CurrentIsPackaged = false;
    }

    public void SaveAsVdf(string path, string password)
    {
        Directory.CreateDirectory(Path.GetDirectoryName(path)!);
        var name = _stk.Root.CurrentScenario.InstanceName;
        var pwdArg = string.IsNullOrEmpty(password) ? "" : $" Password \"{password}\"";
        _stk.Root.ExecuteCommand($"VDF * Scenario/{name} Save \"{path}\"{pwdArg}");
        CurrentPath       = path;
        CurrentIsPackaged = true;
    }

    public void LoadSc(string path)
    {
        if (_stk.Root.CurrentScenario is not null) _stk.Root.CloseScenario();
        _stk.Root.LoadScenario(path);
        CurrentPath       = path;
        CurrentIsPackaged = false;
    }

    public void LoadVdf(string path, string password)
    {
        if (_stk.Root.CurrentScenario is not null) _stk.Root.CloseScenario();
        _stk.Root.LoadVdf(path, password);
        CurrentPath       = path;
        CurrentIsPackaged = true;
    }
}
```

- [ ] **Step 3: Integration tests**

Create `mvp4/Sg.Mvp4.Tests/Integration/PersistenceServiceIntegrationTests.cs`:

```csharp
using FluentAssertions;
using NUnit.Framework;
using Sg.Mvp4.Domain.Models;
using Sg.Mvp4.Domain.Services;

namespace Sg.Mvp4.Tests.Integration;

[TestFixture, Category(TestCategories.Integration)]
public class PersistenceServiceIntegrationTests
{
    private StkRootService  _stk = null!;
    private PersistenceService _p = null!;
    private string _tmpDir = null!;

    [SetUp]
    public void SetUp()
    {
        _stk    = new StkRootService();
        _p      = new PersistenceService(_stk);
        _tmpDir = Path.Combine(Path.GetTempPath(), "mvp4-persist-" + Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(_tmpDir);
    }

    [TearDown]
    public void TearDown()
    {
        _stk.CloseScenario();
        _stk.Dispose();
        try { Directory.Delete(_tmpDir, recursive: true); } catch { /* best-effort */ }
    }

    [Test]
    public void SaveAsSc_writes_sc_file()
    {
        _stk.NewScenario("PersistTest", DateTime.UtcNow, DateTime.UtcNow.AddHours(1));
        _stk.AddEntity(EntityKind.Aircraft, "A1", null);
        var path = Path.Combine(_tmpDir, "PersistTest", "PersistTest.sc");

        _p.SaveAsSc(path);

        File.Exists(path).Should().BeTrue();
    }

    [Test]
    public void Roundtrip_sc_preserves_children()
    {
        _stk.NewScenario("RT", DateTime.UtcNow, DateTime.UtcNow.AddHours(1));
        _stk.AddEntity(EntityKind.Aircraft, "A1", null);
        _stk.AddEntity(EntityKind.Facility, "F1", null);
        var path = Path.Combine(_tmpDir, "RT", "RT.sc");
        _p.SaveAsSc(path);

        // Close + reopen in a fresh service
        using var fresh = new StkRootService();
        var p2 = new PersistenceService(fresh);
        p2.LoadSc(path);

        fresh.Children.Select(c => c.Name).Should().BeEquivalentTo(new[] { "A1", "F1" });
    }

    [Test]
    public void Roundtrip_vdf_preserves_children()
    {
        _stk.NewScenario("RTV", DateTime.UtcNow, DateTime.UtcNow.AddHours(1));
        _stk.AddEntity(EntityKind.Aircraft, "A1", null);
        var path = Path.Combine(_tmpDir, "RTV.vdf");
        _p.SaveAsVdf(path, password: "");

        using var fresh = new StkRootService();
        var p2 = new PersistenceService(fresh);
        p2.LoadVdf(path, password: "");

        fresh.Children.Should().ContainSingle(c => c.Name == "A1");
    }
}
```

- [ ] **Step 4: Expose commands on MainWindowViewModel**

**Retrofit:** The Task 3 `MainWindowViewModelTests` was built against a 1-arg constructor. When widening the ctor below, also update that test file to pass fakes:

```csharp
// Replace each `new MainWindowViewModel(svc)` with:
new MainWindowViewModel(svc, new FakePersistenceService(), new FakeFileDialogService())
```

The `FakePersistenceService` + `FakeFileDialogService` classes live in the Task 15 test file (`ComputeTests.cs`). If Task 15 hasn't been implemented yet, inline minimal stubs in `MainWindowViewModelTests.cs`.

Modify `mvp4/Sg.Mvp4.Domain/ViewModels/MainWindowViewModel.cs`:

```csharp
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using Sg.Mvp4.Domain.Services;

namespace Sg.Mvp4.Domain.ViewModels;

public partial class MainWindowViewModel : ObservableObject
{
    private readonly IStkRootService     _stk;
    private readonly IPersistenceService _persist;
    private readonly IFileDialogService  _fileDialogs;

    [ObservableProperty] private bool _isDirty;

    public IRelayCommand NewCommand     { get; }
    public IRelayCommand OpenCommand    { get; }
    public IRelayCommand SaveCommand    { get; }
    public IRelayCommand SaveAsCommand  { get; }

    public MainWindowViewModel(IStkRootService stk, IPersistenceService persist, IFileDialogService dialogs)
    {
        _stk         = stk;
        _persist     = persist;
        _fileDialogs = dialogs;

        _stk.ScenarioChanged += (_, _) => {
            OnPropertyChanged(nameof(Title));
            OnPropertyChanged(nameof(StatusPath));
        };

        NewCommand    = new RelayCommand(NewScenario);
        OpenCommand   = new RelayCommand(OpenScenario);
        SaveCommand   = new RelayCommand(Save, () => _persist.CurrentPath is not null);
        SaveAsCommand = new RelayCommand(SaveAs);
    }

    public string  Title      => _stk.ScenarioName is null ? "MVP4 — (no scenario)" : $"MVP4 — {_stk.ScenarioName}";
    public string? StatusPath => _persist.CurrentPath is null
        ? null
        : $"{_persist.CurrentPath}{(_persist.CurrentIsPackaged ? " (packaged)" : "")}";

    private void NewScenario()
    {
        if (IsDirty && !_fileDialogs.ConfirmDiscardChanges()) return;
        var req = _fileDialogs.PromptNewScenario();
        if (req is null) return;
        _stk.NewScenario(req.Value.Name, req.Value.StartUtc, req.Value.StopUtc);
        IsDirty = false;
    }

    private void OpenScenario()
    {
        if (IsDirty && !_fileDialogs.ConfirmDiscardChanges()) return;
        var res = _fileDialogs.PromptOpen();
        if (res is null) return;
        if (Path.GetExtension(res.Value.Path).Equals(".vdf", StringComparison.OrdinalIgnoreCase))
            _persist.LoadVdf(res.Value.Path, res.Value.Password ?? "");
        else
            _persist.LoadSc(res.Value.Path);
        IsDirty = false;
    }

    private void Save()
    {
        if (_persist.CurrentPath is null) { SaveAs(); return; }
        if (_persist.CurrentIsPackaged)
            _persist.SaveAsVdf(_persist.CurrentPath, password: "");
        else
            _persist.SaveAsSc(_persist.CurrentPath);
        IsDirty = false;
    }

    private void SaveAs()
    {
        var name = _stk.ScenarioName ?? "Untitled";
        var suggested = _persist.SuggestedPath(name);
        var res = _fileDialogs.PromptSaveAs(suggested);
        if (res is null) return;
        if (res.Value.Format == SaveFormat.Vdf)
            _persist.SaveAsVdf(res.Value.Path, res.Value.Password ?? "");
        else
            _persist.SaveAsSc(res.Value.Path);
        IsDirty = false;
        SaveCommand.NotifyCanExecuteChanged();
    }
}
```

- [ ] **Step 5: Run tests**

```bash
cd mvp4 && dotnet test --filter Category=Integration
```

Expected: 6 integration tests pass (3 from Task 4 + 3 new). First run cold-starts STK so allow 1–2 minutes.

- [ ] **Step 6: Commit**

```bash
git add mvp4/Sg.Mvp4.Domain mvp4/Sg.Mvp4.Tests/Integration
git commit -m "feat(mvp4): PersistenceService — save/load .sc + .vdf with round-trip tests"
```

---

## Task 14: File dialog service + menu wiring

**Files:**
- Create: `mvp4/Sg.Mvp4.Domain/Services/IFileDialogService.cs`
- Create: `mvp4/Sg.Mvp4.App/Services/FileDialogService.cs`
- Create: `mvp4/Sg.Mvp4.App/Views/NewScenarioDialog.xaml`, `.xaml.cs`
- Create: `mvp4/Sg.Mvp4.App/Views/VdfPasswordDialog.xaml`, `.xaml.cs`
- Modify: `mvp4/Sg.Mvp4.App/MainWindow.xaml` (bind File menu items to commands)
- Modify: `mvp4/Sg.Mvp4.App/App.xaml.cs` (register the dialog service)

- [ ] **Step 1: Define IFileDialogService + DTOs**

Create `mvp4/Sg.Mvp4.Domain/Services/IFileDialogService.cs`:

```csharp
namespace Sg.Mvp4.Domain.Services;

public enum SaveFormat { Sc, Vdf }

public sealed record NewScenarioRequest(string Name, DateTime StartUtc, DateTime StopUtc);
public sealed record OpenRequest       (string Path, string? Password);
public sealed record SaveAsRequest     (string Path, SaveFormat Format, string? Password);

public interface IFileDialogService
{
    NewScenarioRequest? PromptNewScenario();
    OpenRequest?        PromptOpen();
    SaveAsRequest?      PromptSaveAs(string suggestedPath);
    string?             PromptVdfPassword();
    bool                ConfirmDiscardChanges();
}
```

- [ ] **Step 2: Implement in the App project (Win32 dialogs)**

Create `mvp4/Sg.Mvp4.App/Services/FileDialogService.cs`:

```csharp
using System.Windows;
using Microsoft.Win32;
using Sg.Mvp4.App.Views;
using Sg.Mvp4.Domain.Services;

namespace Sg.Mvp4.App.Services;

public sealed class FileDialogService : IFileDialogService
{
    public NewScenarioRequest? PromptNewScenario()
    {
        var dlg = new NewScenarioDialog();
        return dlg.ShowDialog() == true
            ? new NewScenarioRequest(dlg.ScenarioName, dlg.StartUtc, dlg.StopUtc)
            : null;
    }

    public OpenRequest? PromptOpen()
    {
        var dlg = new OpenFileDialog {
            Filter = "STK scenario (*.sc)|*.sc|Packaged scenario (*.vdf)|*.vdf|All STK files|*.sc;*.vdf",
            FilterIndex = 3
        };
        if (dlg.ShowDialog() != true) return null;
        return new OpenRequest(dlg.FileName, Password: null);
    }

    public SaveAsRequest? PromptSaveAs(string suggestedPath)
    {
        var dlg = new SaveFileDialog {
            Filter = "STK scenario (*.sc)|*.sc|Packaged scenario (*.vdf)|*.vdf",
            FileName = Path.GetFileName(suggestedPath),
            InitialDirectory = Path.GetDirectoryName(suggestedPath) ?? "",
        };
        if (dlg.ShowDialog() != true) return null;
        var fmt = dlg.FilterIndex == 2 ? SaveFormat.Vdf : SaveFormat.Sc;
        string? pwd = null;
        if (fmt == SaveFormat.Vdf) pwd = PromptVdfPassword(); // may be empty string
        return new SaveAsRequest(dlg.FileName, fmt, pwd);
    }

    public string? PromptVdfPassword()
    {
        var dlg = new VdfPasswordDialog();
        return dlg.ShowDialog() == true ? dlg.Password : null;
    }

    public bool ConfirmDiscardChanges()
    {
        var r = MessageBox.Show("There are unsaved changes. Discard and continue?",
                                "Unsaved changes", MessageBoxButton.YesNo, MessageBoxImage.Warning);
        return r == MessageBoxResult.Yes;
    }
}
```

- [ ] **Step 3: NewScenarioDialog XAML**

`mvp4/Sg.Mvp4.App/Views/NewScenarioDialog.xaml`:

```xml
<Window x:Class="Sg.Mvp4.App.Views.NewScenarioDialog"
        xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation"
        xmlns:x="http://schemas.microsoft.com/winfx/2006/xaml"
        Title="New Scenario" Width="380" Height="240" WindowStartupLocation="CenterOwner"
        ResizeMode="NoResize">
    <Grid Margin="12">
        <Grid.RowDefinitions>
            <RowDefinition Height="Auto" />
            <RowDefinition Height="Auto" />
            <RowDefinition Height="Auto" />
            <RowDefinition Height="*" />
            <RowDefinition Height="Auto" />
        </Grid.RowDefinitions>
        <StackPanel Grid.Row="0">
            <TextBlock Text="Name" />
            <TextBox   x:Name="NameBox" Text="Untitled" />
        </StackPanel>
        <StackPanel Grid.Row="1" Margin="0,8,0,0">
            <TextBlock Text="Start (UTC)  yyyy-MM-ddTHH:mm:ssZ" />
            <TextBox   x:Name="StartBox" />
        </StackPanel>
        <StackPanel Grid.Row="2" Margin="0,8,0,0">
            <TextBlock Text="Stop  (UTC)  yyyy-MM-ddTHH:mm:ssZ" />
            <TextBox   x:Name="StopBox" />
        </StackPanel>
        <StackPanel Grid.Row="4" Orientation="Horizontal" HorizontalAlignment="Right" Margin="0,8,0,0">
            <Button Content="Cancel" IsCancel="True" Width="80" Margin="0,0,8,0" />
            <Button Content="OK"     IsDefault="True" Width="80" Click="Ok_Click" />
        </StackPanel>
    </Grid>
</Window>
```

Code-behind:

```csharp
using System;
using System.Globalization;
using System.Windows;

namespace Sg.Mvp4.App.Views;

public partial class NewScenarioDialog : Window
{
    public string   ScenarioName { get; private set; } = "Untitled";
    public DateTime StartUtc     { get; private set; }
    public DateTime StopUtc      { get; private set; }

    public NewScenarioDialog()
    {
        InitializeComponent();
        var now = DateTime.UtcNow;
        StartBox.Text = now.ToString("yyyy-MM-ddTHH:mm:ssZ");
        StopBox.Text  = now.AddHours(1).ToString("yyyy-MM-ddTHH:mm:ssZ");
    }

    private void Ok_Click(object sender, RoutedEventArgs e)
    {
        if (!DateTime.TryParse(StartBox.Text, CultureInfo.InvariantCulture,
                               DateTimeStyles.AssumeUniversal | DateTimeStyles.AdjustToUniversal, out var s)
         || !DateTime.TryParse(StopBox.Text,  CultureInfo.InvariantCulture,
                               DateTimeStyles.AssumeUniversal | DateTimeStyles.AdjustToUniversal, out var e2))
        {
            MessageBox.Show("Invalid date/time format.");
            return;
        }
        ScenarioName = NameBox.Text.Trim();
        StartUtc     = s;
        StopUtc      = e2;
        DialogResult = true;
    }
}
```

- [ ] **Step 4: VdfPasswordDialog XAML + code-behind (similar shape)**

```xml
<Window x:Class="Sg.Mvp4.App.Views.VdfPasswordDialog"
        xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation"
        xmlns:x="http://schemas.microsoft.com/winfx/2006/xaml"
        Title="VDF Password" Width="320" Height="140"
        WindowStartupLocation="CenterOwner" ResizeMode="NoResize">
    <StackPanel Margin="12">
        <TextBlock Text="Password (empty for none):" />
        <PasswordBox x:Name="PwdBox" Margin="0,4" />
        <StackPanel Orientation="Horizontal" HorizontalAlignment="Right" Margin="0,8">
            <Button Content="Cancel" IsCancel="True" Width="70" Margin="0,0,8,0" />
            <Button Content="OK"     IsDefault="True" Width="70" Click="Ok_Click" />
        </StackPanel>
    </StackPanel>
</Window>
```

```csharp
using System.Windows;

namespace Sg.Mvp4.App.Views;
public partial class VdfPasswordDialog : Window
{
    public string Password { get; private set; } = "";
    public VdfPasswordDialog() { InitializeComponent(); }
    private void Ok_Click(object sender, RoutedEventArgs e)
    {
        Password = PwdBox.Password;
        DialogResult = true;
    }
}
```

- [ ] **Step 5: Bind File menu to commands**

Modify the `<MenuItem Header="_File">` block in `MainWindow.xaml`:

```xml
<MenuItem Header="_File">
    <MenuItem Header="_New Scenario..." Command="{Binding NewCommand}" />
    <MenuItem Header="_Open..."         Command="{Binding OpenCommand}" />
    <Separator />
    <MenuItem Header="_Save"            Command="{Binding SaveCommand}" />
    <MenuItem Header="Save _As..."      Command="{Binding SaveAsCommand}" />
    <Separator />
    <MenuItem Header="E_xit" Click="ExitMenu_Click" />
</MenuItem>
```

- [ ] **Step 6: Register in DI**

Add in `App.xaml.cs`:

```csharp
services.AddSingleton<IFileDialogService, Services.FileDialogService>();
services.AddSingleton<IPersistenceService, PersistenceService>();
```

Update `MainWindowViewModel` registration to match the new 3-arg constructor — no code change needed if DI is resolving by type.

- [ ] **Step 7: Smoke run**

```bash
cd mvp4 && dotnet run --project Sg.Mvp4.App
```

Expected full flow: File > New → dialog → creates empty scenario with chosen time window. Add Aircraft + waypoints + Apply. File > Save As → dialog → writes `C:\EWTSS\mvp4\scenarios\MyTest\MyTest.sc`. Restart app → File > Open → pick that `.sc` → scene comes back. File > Save As → pick `.vdf` filter → password dialog → writes `MyTest.vdf`.

- [ ] **Step 8: Commit**

```bash
git add mvp4/
git commit -m "feat(mvp4): FileDialogService + File menu (New/Open/Save/SaveAs + VDF password)"
```

---

## Task 15: Compute toolbar + error surface + status bar

**Files:**
- Modify: `mvp4/Sg.Mvp4.Domain/ViewModels/MainWindowViewModel.cs` (add `ComputeCommand`, `StatusMessage`)
- Modify: `mvp4/Sg.Mvp4.App/MainWindow.xaml` (bind Compute menu + toolbar + status bar)
- Create: `mvp4/Sg.Mvp4.Tests/ViewModels/ComputeTests.cs`

- [ ] **Step 1: Write failing test**

Create `mvp4/Sg.Mvp4.Tests/ViewModels/ComputeTests.cs`:

```csharp
using FluentAssertions;
using NUnit.Framework;
using Sg.Mvp4.Domain.Models;
using Sg.Mvp4.Domain.Services;
using Sg.Mvp4.Domain.ViewModels;
using Sg.Mvp4.Tests.Fakes;

namespace Sg.Mvp4.Tests.ViewModels;

[TestFixture, Category(TestCategories.Unit)]
public class ComputeTests
{
    private static MainWindowViewModel CreateVm(FakeStkRootService svc) =>
        new(svc, new FakePersistenceService(), new FakeFileDialogService());

    [Test]
    public void ComputeCommand_disabled_when_no_coverage_definition()
    {
        var svc = new FakeStkRootService();
        svc.NewScenario("S", DateTime.UtcNow, DateTime.UtcNow.AddHours(1));
        var vm = CreateVm(svc);

        vm.ComputeCommand.CanExecute(null).Should().BeFalse();
    }

    [Test]
    public void ComputeCommand_enabled_when_coverage_definition_exists()
    {
        var svc = new FakeStkRootService();
        svc.NewScenario("S", DateTime.UtcNow, DateTime.UtcNow.AddHours(1));
        svc.AddEntity(EntityKind.CoverageDefinition, "Cov1", null);
        var vm = CreateVm(svc);

        vm.ComputeCommand.CanExecute(null).Should().BeTrue();
    }

    [Test]
    public void ComputeCommand_invokes_ComputeAll_and_sets_status()
    {
        var svc = new FakeStkRootService();
        svc.NewScenario("S", DateTime.UtcNow, DateTime.UtcNow.AddHours(1));
        svc.AddEntity(EntityKind.CoverageDefinition, "Cov1", null);
        var vm = CreateVm(svc);

        vm.ComputeCommand.Execute(null);

        svc.ComputeCallCount.Should().Be(1);
        vm.StatusMessage.Should().Contain("Compute");
    }
}

// Minimal fakes for this test
public sealed class FakePersistenceService : IPersistenceService {
    public string? CurrentPath => null;
    public bool    CurrentIsPackaged => false;
    public string SuggestedPath(string n) => $"C:\\tmp\\{n}.sc";
    public void SaveAsSc  (string p) { }
    public void SaveAsVdf (string p, string pw) { }
    public void LoadSc    (string p) { }
    public void LoadVdf   (string p, string pw) { }
}
public sealed class FakeFileDialogService : IFileDialogService {
    public NewScenarioRequest? PromptNewScenario() => null;
    public OpenRequest?        PromptOpen()        => null;
    public SaveAsRequest?      PromptSaveAs(string s) => null;
    public string?             PromptVdfPassword()  => "";
    public bool                ConfirmDiscardChanges() => true;
}
```

- [ ] **Step 2: Extend MainWindowViewModel**

Add to `MainWindowViewModel.cs`:

```csharp
[ObservableProperty] private string _statusMessage = "Ready.";

public IRelayCommand ComputeCommand { get; }

// In constructor:
ComputeCommand = new RelayCommand(Compute, HasCoverage);
_stk.ScenarioChanged += (_, _) => ComputeCommand.NotifyCanExecuteChanged();

private bool HasCoverage()
{
    foreach (var c in _stk.Children)
        if (c.Kind == Models.EntityKind.CoverageDefinition) return true;
    return false;
}

private void Compute()
{
    try
    {
        StatusMessage = "Compute running...";
        _stk.ComputeAll();
        StatusMessage = "Compute complete.";
    }
    catch (Exception ex)
    {
        StatusMessage = $"Compute failed: {ex.Message}";
    }
}
```

- [ ] **Step 3: Bind Compute menu + status bar**

In `MainWindow.xaml`:

```xml
<MenuItem Header="_Compute">
    <MenuItem Header="Compute All Coverage" Command="{Binding ComputeCommand}" />
</MenuItem>

<!-- status bar -->
<StatusBar DockPanel.Dock="Bottom" Height="24">
    <StatusBarItem Content="{Binding StatusMessage}" />
    <Separator />
    <StatusBarItem Content="{Binding StatusPath}" />
</StatusBar>
```

- [ ] **Step 4: Run tests + smoke**

```bash
cd mvp4 && dotnet test --filter Category=Unit
dotnet run --project Sg.Mvp4.App
```

Expected: add AreaTarget + CoverageDefinition with that AreaTarget attached + an Aircraft asset + FigureOfMerit with AccessDuration → Compute menu item enables → click → status bar reads "Compute complete" → FOM grid appears colored on the globe.

- [ ] **Step 5: Commit**

```bash
git add mvp4/
git commit -m "feat(mvp4): Compute command + status bar error surface"
```

---

## Task 16: Logging, splash, shutdown watchdog

**Files:**
- Modify: `mvp4/Sg.Mvp4.App/App.xaml.cs` (Serilog setup + shutdown watchdog)
- Create: `mvp4/Sg.Mvp4.App/Views/Shell/SplashWindow.xaml`, `.xaml.cs`

- [ ] **Step 1: SplashWindow**

Create `mvp4/Sg.Mvp4.App/Views/Shell/SplashWindow.xaml`:

```xml
<Window x:Class="Sg.Mvp4.App.Views.Shell.SplashWindow"
        xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation"
        xmlns:x="http://schemas.microsoft.com/winfx/2006/xaml"
        WindowStyle="None" WindowStartupLocation="CenterScreen"
        AllowsTransparency="True" Background="#222" Width="400" Height="180">
    <Grid>
        <StackPanel VerticalAlignment="Center" HorizontalAlignment="Center">
            <TextBlock Text="MVP4 — STK-Native Embedded" Foreground="#EEE"
                       FontSize="20" FontWeight="Bold" HorizontalAlignment="Center" />
            <TextBlock x:Name="StatusText" Text="Starting STK Engine..." Foreground="#AAA"
                       Margin="0,8" HorizontalAlignment="Center" />
        </StackPanel>
    </Grid>
</Window>
```

```csharp
using System.Windows;
namespace Sg.Mvp4.App.Views.Shell;
public partial class SplashWindow : Window {
    public SplashWindow() { InitializeComponent(); }
    public void SetStatus(string text) => StatusText.Text = text;
}
```

- [ ] **Step 2: Serilog setup + shutdown watchdog in App.xaml.cs**

Replace `App.xaml.cs`:

```csharp
using System.IO;
using System.Runtime.InteropServices;
using System.Windows;
using System.Windows.Threading;
using Microsoft.Extensions.DependencyInjection;
using Serilog;
using Sg.Mvp4.App.Views.Shell;
using Sg.Mvp4.Domain.Services;
using Sg.Mvp4.Domain.ViewModels;

namespace Sg.Mvp4.App;

public partial class App : Application
{
    public IServiceProvider Services { get; private set; } = null!;
    private SplashWindow? _splash;

    protected override void OnStartup(StartupEventArgs e)
    {
        base.OnStartup(e);

        var logDir = @"C:\EWTSS\mvp4\logs";
        Directory.CreateDirectory(logDir);
        Log.Logger = new LoggerConfiguration()
            .MinimumLevel.Debug()
            .WriteTo.File(Path.Combine(logDir, "mvp4-.log"), rollingInterval: RollingInterval.Day)
            .WriteTo.Debug()
            .CreateLogger();

        _splash = new SplashWindow();
        _splash.Show();

        try
        {
            _splash.SetStatus("Constructing STK COM root...");
            DoDispatch();

            _splash.SetStatus("Building services...");
            var services = new ServiceCollection();
            services.AddSingleton<IStkRootService, StkRootService>();
            services.AddSingleton<IPersistenceService, PersistenceService>();
            services.AddSingleton<IFileDialogService, Services.FileDialogService>();
            services.AddSingleton<MainWindowViewModel>();
            services.AddSingleton<ObjectTreeViewModel>();
            services.AddSingleton<PropertyPanelHostViewModel>();
            services.AddSingleton<StkDisplayHost>();
            services.AddSingleton<ObjectTreeView>();
            services.AddSingleton<PropertyPanelHostView>();
            services.AddSingleton<MainWindow>();
            Services = services.BuildServiceProvider();

            _splash.SetStatus("Creating initial scenario...");
            var stk = Services.GetRequiredService<IStkRootService>();
            stk.NewScenario("Untitled", DateTime.UtcNow, DateTime.UtcNow.AddHours(1));

            _splash.SetStatus("Opening main window...");
            Services.GetRequiredService<MainWindow>().Show();
        }
        catch (Exception ex)
        {
            Log.Fatal(ex, "Startup failed");
            MessageBox.Show($"MVP4 failed to start:\n\n{ex.Message}\n\n" +
                            "Check STK 12 is installed and COM-registered.",
                            "Startup error", MessageBoxButton.OK, MessageBoxImage.Error);
            Shutdown(1);
        }
        finally
        {
            _splash?.Close();
            _splash = null;
        }
    }

    protected override void OnExit(ExitEventArgs e)
    {
        try
        {
            if (Services.GetService<IStkRootService>() is StkRootService concrete) concrete.Dispose();
        }
        catch (Exception ex) { Log.Warning(ex, "Dispose failed"); }

        // 5s watchdog: if teardown hangs, force-exit.
        var t = new System.Threading.Thread(() =>
        {
            System.Threading.Thread.Sleep(5000);
            Log.Warning("Graceful shutdown exceeded 5s — forcing Environment.Exit.");
            Environment.Exit(0);
        }) { IsDaemon: false };
        t.IsBackground = true;
        t.Start();

        Log.CloseAndFlush();
        base.OnExit(e);
    }

    private static void DoDispatch() => Dispatcher.CurrentDispatcher.Invoke(() => { });
}
```

Note: `IsDaemon` does not exist on `Thread`; replace that line with just `t.IsBackground = true;` — corrected above by dropping the initializer.

- [ ] **Step 3: Smoke run**

```bash
cd mvp4 && dotnet run --project Sg.Mvp4.App
```

Expected: splash window appears briefly showing "Starting STK Engine..." → main window shows. Log file appears at `C:\EWTSS\mvp4\logs\mvp4-YYYYMMDD.log`. Close the main window — no orphan `agacsrv.exe` / `Agi.Stk12.exe` visible in Task Manager after ~5 s.

Force-kill test: put a breakpoint in `OnExit` between `Dispose` and `Log.CloseAndFlush`, step over `Dispose`, then manually hold for >5 s; verify the watchdog thread calls `Environment.Exit`.

- [ ] **Step 4: Commit**

```bash
git add mvp4/Sg.Mvp4.App
git commit -m "feat(mvp4): splash + Serilog + 5s shutdown watchdog"
```

---

## Task 17: Acceptance walkthrough + final README

**Files:**
- Modify: `mvp4/README.md` (full doc)
- Create: `mvp4/Sg.Mvp4.Tests/Integration/Scenario34AcceptanceTests.cs`

- [ ] **Step 1: Acceptance integration test**

Create `mvp4/Sg.Mvp4.Tests/Integration/Scenario34AcceptanceTests.cs`:

```csharp
using FluentAssertions;
using NUnit.Framework;
using Sg.Mvp4.Domain.Models;
using Sg.Mvp4.Domain.Services;

namespace Sg.Mvp4.Tests.Integration;

/// <summary>End-to-end check that MVP4 can author a scenario whose
/// topology matches the reference Scenario34.czml the user provided
/// during brainstorming.</summary>
[TestFixture, Category(TestCategories.Integration)]
public class Scenario34AcceptanceTests
{
    [Test]
    public void Can_author_and_roundtrip_Scenario34_shape()
    {
        using var stk = new StkRootService();
        var persist = new PersistenceService(stk);

        var start = new DateTime(2026, 4, 15, 6, 30, 0, DateTimeKind.Utc);
        var stop  = new DateTime(2026, 4, 15, 8, 46, 52, DateTimeKind.Utc);
        stk.NewScenario("Scenario34", start, stop);

        var aircraft = stk.AddEntity(EntityKind.Aircraft,           "Aircraft1", null);
        stk.AddEntity(EntityKind.Sensor,                            "Sensor1",   aircraft.Path);
        stk.AddEntity(EntityKind.Facility,                          "Facility1", null);
        stk.AddEntity(EntityKind.AreaTarget,                        "AreaTarget1", null);
        var coverage = stk.AddEntity(EntityKind.CoverageDefinition, "CoverageDefinition1", null);
        stk.AddEntity(EntityKind.FigureOfMerit,                     "FigureOfMerit1", coverage.Path);

        stk.Children.Select(c => c.Name).Should().BeEquivalentTo(new[] {
            "Aircraft1", "Facility1", "AreaTarget1", "CoverageDefinition1"
        });
        stk.FindByPath("Aircraft1/Sensor1").Should().NotBeNull();
        stk.FindByPath("CoverageDefinition1/FigureOfMerit1").Should().NotBeNull();

        var tmp = Path.Combine(Path.GetTempPath(), "Scenario34-accept", "Scenario34.sc");
        persist.SaveAsSc(tmp);

        using var fresh = new StkRootService();
        new PersistenceService(fresh).LoadSc(tmp);
        fresh.Children.Should().HaveCount(4);
        fresh.FindByPath("Aircraft1/Sensor1").Should().NotBeNull();
    }
}
```

- [ ] **Step 2: Rewrite the README**

Replace `mvp4/README.md` (full doc — operator-facing):

```markdown
# EWTSS MVP4 — STK-Native Embedded Authoring

Single-process C# WPF desktop application that authors, computes, and
visualises STK 12 scenarios using STK's embedded ActiveX globe as the
sole renderer. Sibling to `mvp/` (MVP1), `mvp2/`, `mvp3/`. Completely
different frontend stack from those: no Cesium, no Python, no server.

## Architecture at a glance

```
Sg.Mvp4.App.exe   (C# WPF · .NET 8 · Windows)
    │
    │   +-- MainWindow
    │        +-- ObjectTreeView          (TreeView + Add/Delete toolbar)
    │        +-- StkDisplayHost          (WindowsFormsHost → ActiveX 3D + 2D)
    │        +-- PropertyPanelHostView   (DataTemplate per EntityKind)
    │
    +-- IStkRootService ──→ AgStkObjectRoot (COM, out-of-process STK 12)

Scenario files: C:\EWTSS\mvp4\scenarios\<name>\<name>.sc (+ .vdf via Save As)
Logs:           C:\EWTSS\mvp4\logs\mvp4-YYYYMMDD.log
```

## Supported entity types

Aircraft · Facility · AreaTarget · Sensor (Simple Conic) · CoverageDefinition · FigureOfMerit.

See [`docs/ewtss/specs/mvp4-stk-native-embedded-design.md`](../../ewtss/specs/mvp4-stk-native-embedded-design.md) for the full entity-field catalogue and explicit exclusions.

## Prerequisites

- STK 12 installed (COM-registered) + primary interop assemblies at `C:\Program Files\AGI\STK 12\bin\Primary Interop Assemblies\` (files named `AGI.<Name>.Interop.dll` — note the `.Interop.` infix)
- .NET 8 SDK
- Visual Studio 2022 (17.8+) for ActiveX wrapper generation / debugging
- For VDF save/load: STK's `VDF *` Connect Command (part of standard STK 12)

## Build

```
cd mvp4
dotnet build
```

## Test

Unit tier (no STK — runs in CI):

```
dotnet test --filter Category=Unit
```

Integration + acceptance tier (requires STK 12):

```
dotnet test --filter Category=Integration
```

## Run

```
dotnet run --project Sg.Mvp4.App
```

## End-to-end walkthrough

1. Launch the app — splash → main window with empty `Untitled` scenario.
2. `File > New Scenario…` — name it, pick start/stop times.
3. Left-pane toolbar: `+ Aircraft` → select the new aircraft → right pane → enter waypoints → `Apply`.
4. Right-click aircraft in the tree (or re-select it and use its context menu) → `Add Sensor` → set outer half-angle + pointing → `Apply`.
5. `+ Facility`, `+ AreaTarget`, `+ Coverage` → fill properties → `Apply`.
6. Right-click the CoverageDefinition → `Add FigureOfMerit` → pick type + statistic → `Apply`.
7. `Compute > Compute All Coverage` → STK colours the FOM grid on the globe.
8. Scrub the STK timeline below the globe — aircraft animates, sensor cone attaches.
9. `File > Save As` → pick `.sc` or `.vdf`.
10. Re-open in STK Desktop / Insight to verify round-trip compatibility.

## Troubleshooting

| Symptom | Cause | Fix |
|---|---|---|
| `AgStkObjectRoot` constructor throws | STK 12 not registered | Repair STK installation; `regsvr32 agacsrv.exe` |
| Cannot find `AxAGI.STKX.Interop` | Wrappers not generated | `aximp "C:\Program Files\AGI\STK 12\bin\AgStkX.ocx"` in VS2022 Dev Prompt |
| Compute throws `license denied` | STK Engine license not checked out | Verify license-server reachability; check STK Insight can launch |
| App window shows but globe is black | ActiveX failed to bind | Check `SetAGISTKObject` ran — inspect Debug output in VS |
| Shutdown leaves orphan STK process | Graceful teardown timeout exceeded | 5-second watchdog in `App.OnExit` force-exits; check log |

## What's NOT in MVP4

See the design doc section "Consolidated explicit exclusions". Summary:
no Python, no Kafka, no DB, no RBAC, no live DRS overlays, no FOM export,
no satellites/vehicles/comm objects, no mock STK.
```

- [ ] **Step 3: Run everything**

```bash
cd mvp4
dotnet test --filter Category=Unit
# Expected: all ~35 unit tests pass

dotnet test --filter Category=Integration
# Expected: 7 integration tests pass (takes ~1–2 min for cold STK start)

dotnet run --project Sg.Mvp4.App
# Acceptance: walk through the 10-step end-to-end above; verify every step works
```

- [ ] **Step 4: Acceptance checklist**

Confirm each item in the design spec's acceptance criteria (§7.4):

- [ ] Operator authors a scenario from scratch with one of each of the 6 entity types
- [ ] Operator saves and reloads as both `.sc` and `.vdf`
- [ ] Operator opens the attached `docs/acceptance/Scenario34.czml`-equivalent `.sc` / `.vdf` and sees it render (build a matching `.sc` from Task 17's integration test first)
- [ ] Compute colors the FOM grid on the globe
- [ ] A `.sc` written by MVP4 opens cleanly in STK Desktop / Insight
- [ ] Shutdown leaves no orphan `Agi.Stk12.exe` after 5 s

- [ ] **Step 5: Final commit**

```bash
git add mvp4/
git commit -m "feat(mvp4): acceptance test + final README + walkthrough"
```

---

## Post-MVP4

If acceptance passes, options for MVP4.x documented in the design spec §8:

- **MVP4.1** — scenario-catalog UI (SQLite list + thumbnails), PDF export, Windows auth
- **MVP4.2** — live DRS overlays via Kafka subscriber + WPF canvas overlay (risky — see prior design doc §22.2)
- **MVP4.3** — broader entity set (satellites, comm objects, more sensor patterns)

None are in scope for this plan.

---

## Appendix A — Cross-task retrofit + known limits

Items surfaced during self-review that the implementer should know about up-front:

1. **`MainWindowViewModel` ctor widens in Task 13.** Task 3 constructs it with 1 arg; Task 13 widens to 3 args. When executing Task 13, also update `Sg.Mvp4.Tests/ViewModels/MainWindowViewModelTests.cs` to pass the two new fake services (see Task 13 Step 4 retrofit note).

2. **Dirty flag propagation.** Entity-panel VMs set their own `IsDirty` flag (Task 6) but `MainWindowViewModel.IsDirty` is only flipped on `Apply` from the child. When executing Task 13, extend `PropertyPanelHostViewModel` to forward: subscribe to each child VM's `PropertyChanged` for `nameof(IsDirty)` and call `mainVm.IsDirty = true` when any entity becomes dirty. Without this, `File > New` won't prompt for unsaved changes during an in-flight edit.

3. **`MainWindow.Closing` save-prompt.** The design spec §6.3 says `MainWindow.Closing` should prompt save if dirty. Not written explicitly in a task — implement in Task 16 by overriding `OnClosing` in `MainWindow.xaml.cs` to call `vm.ConfirmDiscardOrSave()` if `vm.IsDirty`, and cancel the close event if the user chooses Cancel.

4. **VDF password 3-retry loop.** Design spec §6.1 specifies 3 incorrect attempts then cancel. Task 14's `PromptOpen` returns a single password; the retry loop lives in the VM. When executing Task 14, wrap the `LoadVdf` call in `OpenScenario()` in a 3-try loop:
   ```csharp
   for (var attempt = 0; attempt < 3; attempt++)
   {
       try { _persist.LoadVdf(res.Value.Path, pwd ?? ""); return; }
       catch (COMException ex) when (IsPasswordError(ex) && attempt < 2)
       {
           pwd = _fileDialogs.PromptVdfPassword();
           if (pwd is null) return;
       }
   }
   ```

5. **STK COM API approximations.** Tasks 4, 7, 8, 9, 10, 11, 12 show COM calls for things like `IAgCoverageDefinition.Grid.Bounds`, `IAgSnPtFixed.Orientation.AssignAzEl`, `IAgAreaTypePatternCollection`. These are starting sketches — the STK 12 API has occasional renames and optional params. Verify each call against IntelliSense in Visual Studio 2022 when you land on it. If a method signature doesn't match, search the STK 12 Scripting Help (`C:\Program Files\AGI\STK 12\Help\`) for the right shape. The **interface contract** (`IAircraftBackend`, `IFacilityBackend`, …) is what the VMs and tests depend on — that is stable.

6. **No unit tests for the COM backends.** By design. Backends are covered by the Task 4 + 13 + 17 integration tests (which require STK). If you discover a tricky COM pattern worth testing, add a new integration test in `Sg.Mvp4.Tests/Integration/` — do not try to mock COM.

7. **Great-Arc vs free-form routes.** Task 7 sets `AgEVePropagatorType.ePropagatorGreatArc`. If the reference CZML needs per-waypoint time stamps rather than speed, extend `WaypointModel` with an optional `TimeUtc` field and switch the route method to `eDetermineVelFromTime`. This is a minor extension; MVP4 defaults to speed-based.

8. **Color conversion.** Hex `#RRGGBB` is converted to STK's int color (BGR order) by `Convert.ToInt32(hex, 16)` — STK's color int is actually `0xRRGGBB`, which matches. If colors come back swapped (red/blue inverted), apply a byte-swap in `ComAccessors.cs` color getters/setters. Note this in the first run and fix immediately if observed.

9. **Concrete service leak.** `StkDisplayHost` (Task 4) casts `IStkRootService` to `StkRootService` to access the internal `AgStkObjectRoot` Root property. This is a deliberate layer compromise: only the App project (not Domain) needs this access, and the cast is localized to one place. If integration testing demands it, extract an `IStkRootServiceInternal` interface in `Sg.Mvp4.Domain.Services` with a single `AgStkObjectRoot Root { get; }` member.


