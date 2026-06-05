using Sg.Domain.Models;

namespace Sg.Domain.Contracts;

public interface IScenarioBackend
{
    string?  ScenarioName { get; }
    DateTime StartTimeUtc { get; }
    DateTime StopTimeUtc  { get; }
    void NewScenario(string name, DateTime startUtc, DateTime stopUtc);
    void CloseScenario();

    IReadOnlyList<EntityNodeDto> Children { get; }
    EntityNodeDto? FindByPath(string path);

    EntityNodeDto AddEntity(EntityKind kind, string name, string? parentPath);
    void RemoveEntity(string path);

    AircraftDto           GetAircraft         (string path);
    FacilityDto           GetFacility         (string path);
    AreaTargetDto         GetAreaTarget       (string path);
    SensorDto             GetSensor           (string path);
    CoverageDefinitionDto GetCoverage         (string path);
    FigureOfMeritDto      GetFom              (string path);

    void UpdateAircraft  (string path, AircraftDto           dto);
    void UpdateFacility  (string path, FacilityDto           dto);
    void UpdateAreaTarget(string path, AreaTargetDto         dto);
    void UpdateSensor    (string path, SensorDto             dto);
    void UpdateCoverage  (string path, CoverageDefinitionDto dto);
    void UpdateFom       (string path, FigureOfMeritDto      dto);

    ComputeResultDto ComputeAll();
    void SaveAsSc (string path);
    void SaveAsVdf(string path, string password);
    void LoadSc   (string path);
    void LoadVdf  (string path, string password);
    string? CurrentPath { get; }
    bool    CurrentIsPackaged { get; }
    string  SuggestedPath(string scenarioName);

    event EventHandler? ScenarioChanged;
}
