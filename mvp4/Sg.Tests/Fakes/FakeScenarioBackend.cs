using Sg.Domain.Contracts;
using Sg.Domain.Models;

namespace Sg.Tests.Fakes;

public sealed class FakeScenarioBackend : IScenarioBackend
{
    private readonly List<EntityNodeDto> _rootChildren = new();

    public string?  ScenarioName       { get; private set; }
    public DateTime StartTimeUtc       { get; private set; }
    public DateTime StopTimeUtc        { get; private set; }
    public string?  CurrentPath        { get; private set; }
    public bool     CurrentIsPackaged  { get; private set; }
    public IReadOnlyList<EntityNodeDto> Children => _rootChildren;
    public event EventHandler? ScenarioChanged;

    public Dictionary<string, AircraftDto>           Aircraft     { get; } = new();
    public Dictionary<string, FacilityDto>           Facilities   { get; } = new();
    public Dictionary<string, AreaTargetDto>         AreaTargets  { get; } = new();
    public Dictionary<string, SensorDto>             Sensors      { get; } = new();
    public Dictionary<string, CoverageDefinitionDto> Coverages    { get; } = new();
    public Dictionary<string, FigureOfMeritDto>      Foms         { get; } = new();
    public int    ComputeCallCount     { get; private set; }
    public List<(string path, string? pwd, bool packaged)> Saves { get; } = new();
    public List<(string path, string? pwd, bool packaged)> Loads { get; } = new();

    public string SuggestedPath(string scenarioName) => $@"C:\tmp\{scenarioName}.sc";

    public void NewScenario(string name, DateTime startUtc, DateTime stopUtc)
    {
        _rootChildren.Clear();
        Aircraft.Clear();   Facilities.Clear();  AreaTargets.Clear();
        Sensors.Clear();    Coverages.Clear();   Foms.Clear();
        ScenarioName  = name;
        StartTimeUtc  = startUtc;
        StopTimeUtc   = stopUtc;
        ScenarioChanged?.Invoke(this, EventArgs.Empty);
    }

    public void CloseScenario()
    {
        if (ScenarioName is null) return;
        _rootChildren.Clear();
        ScenarioName = null;
        ScenarioChanged?.Invoke(this, EventArgs.Empty);
    }

    public EntityNodeDto AddEntity(EntityKind kind, string name, string? parentPath)
    {
        ValidateParentage(kind, parentPath);
        var path = parentPath is null ? name : $"{parentPath}/{name}";
        var node = new EntityNodeDto(kind, name, path, new List<EntityNodeDto>());

        if (parentPath is null)
            _rootChildren.Add(node);
        else
        {
            var parent = FindByPath(parentPath)
                ?? throw new InvalidOperationException($"Parent '{parentPath}' not found.");
            ((List<EntityNodeDto>)parent.Children).Add(node);
        }
        ScenarioChanged?.Invoke(this, EventArgs.Empty);
        return node;
    }

    public void RemoveEntity(string path)
    {
        var seg = path.Split('/');
        var removed = false;
        if (seg.Length == 1)
        {
            var idx = _rootChildren.FindIndex(c => c.Name == seg[0]);
            if (idx >= 0) { _rootChildren.RemoveAt(idx); removed = true; }
        }
        else
        {
            var parent = FindByPath(string.Join('/', seg[..^1]));
            if (parent is not null)
            {
                var list = (List<EntityNodeDto>)parent.Children;
                var idx  = list.FindIndex(c => c.Name == seg[^1]);
                if (idx >= 0) { list.RemoveAt(idx); removed = true; }
            }
        }
        if (removed) ScenarioChanged?.Invoke(this, EventArgs.Empty);
    }

    public EntityNodeDto? FindByPath(string path)
    {
        var seg = path.Split('/');
        EntityNodeDto? cursor = _rootChildren.FirstOrDefault(c => c.Name == seg[0]);
        for (var i = 1; i < seg.Length && cursor is not null; i++)
            cursor = cursor.Children.FirstOrDefault(c => c.Name == seg[i]);
        return cursor;
    }

    private static void ValidateParentage(EntityKind kind, string? parentPath)
    {
        if (parentPath is null && (kind == EntityKind.Sensor || kind == EntityKind.FigureOfMerit))
            throw new InvalidOperationException(
                kind == EntityKind.Sensor
                    ? "Sensor must be added under Aircraft or Facility, not at the scenario root."
                    : "FigureOfMerit must be added under CoverageDefinition.");
    }

    public AircraftDto           GetAircraft  (string path) => Aircraft.TryGetValue(path, out var v) ? v : throw new InvalidOperationException($"No fake AircraftDto for '{path}'.");
    public FacilityDto           GetFacility  (string path) => Facilities.TryGetValue(path, out var v) ? v : throw new InvalidOperationException($"No fake FacilityDto for '{path}'.");
    public AreaTargetDto         GetAreaTarget(string path) => AreaTargets.TryGetValue(path, out var v) ? v : throw new InvalidOperationException($"No fake AreaTargetDto for '{path}'.");
    public SensorDto             GetSensor    (string path) => Sensors.TryGetValue(path, out var v) ? v : throw new InvalidOperationException($"No fake SensorDto for '{path}'.");
    public CoverageDefinitionDto GetCoverage  (string path) => Coverages.TryGetValue(path, out var v) ? v : throw new InvalidOperationException($"No fake CoverageDefinitionDto for '{path}'.");
    public FigureOfMeritDto      GetFom       (string path) => Foms.TryGetValue(path, out var v) ? v : throw new InvalidOperationException($"No fake FigureOfMeritDto for '{path}'.");

    public void UpdateAircraft  (string path, AircraftDto           dto) { Aircraft[path]    = dto; ScenarioChanged?.Invoke(this, EventArgs.Empty); }
    public void UpdateFacility  (string path, FacilityDto           dto) { Facilities[path]  = dto; ScenarioChanged?.Invoke(this, EventArgs.Empty); }
    public void UpdateAreaTarget(string path, AreaTargetDto         dto) { AreaTargets[path] = dto; ScenarioChanged?.Invoke(this, EventArgs.Empty); }
    public void UpdateSensor    (string path, SensorDto             dto) { Sensors[path]     = dto; ScenarioChanged?.Invoke(this, EventArgs.Empty); }
    public void UpdateCoverage  (string path, CoverageDefinitionDto dto) { Coverages[path]   = dto; ScenarioChanged?.Invoke(this, EventArgs.Empty); }
    public void UpdateFom       (string path, FigureOfMeritDto      dto) { Foms[path]        = dto; ScenarioChanged?.Invoke(this, EventArgs.Empty); }

    public ComputeResultDto ComputeAll()
    {
        ComputeCallCount++;
        return new ComputeResultDto(true, null, DateTime.UtcNow);
    }

    public void SaveAsSc(string path)              { Saves.Add((path, null, false)); CurrentPath = path; CurrentIsPackaged = false; }
    public void SaveAsVdf(string path, string pwd) { Saves.Add((path, pwd,  true));  CurrentPath = path; CurrentIsPackaged = true;  }
    public void LoadSc(string path)                { Loads.Add((path, null, false)); CurrentPath = path; CurrentIsPackaged = false; }
    public void LoadVdf(string path, string pwd)   { Loads.Add((path, pwd,  true));  CurrentPath = path; CurrentIsPackaged = true;  }
}
