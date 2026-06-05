# sg-app — production v2 Sg.App scaffold

WPF .NET 8 client for the EWTSS v2 product. Mirrors what `mvp4/Sg.App`
verified, but with full production-grade infrastructure:

| Concern | mvp4/Sg.App (reference) | sg-app/Sg.App (this) |
|---|---|---|
| Composition root | manual `ServiceCollection` in `OnStartup` | `IHost` via `Microsoft.Extensions.Hosting` |
| Configuration | env vars | `appsettings.json` + `IConfiguration` |
| HTTP client | n/a | `IHttpClientFactory` (`AddHttpClient<T>`) |
| Logging | Serilog file+debug | Serilog file+debug, driven by config |
| Exercise control | n/a | `IExerciseStateService` + `MainWindowViewModel` commands |
| STK integration | yes (COM in-process) | not here — production v2 uses drs-server/drs-bridge |

mvp4 stays as the reference codebase for STK invariants and isolated
verifications. New v2 product work lands here.

## Layout

```
sg-app/
  sg-app.sln
  Sg.App/
    App.xaml(.cs)             # IHost composition root
    MainWindow.xaml(.cs)      # banner host + exercise control bar
    appsettings.json          # DrsServer:Url, Logging
    appsettings.Development.json
    Contracts/
      TimeSyncStatusDto.cs    # B1.3 Task 15 (ported from mvp4)
    Converters/
      BannerLevelToBrushConverter.cs
      BannerLevelToVisibilityConverter.cs
    Models/
      ExerciseState.cs
      IExerciseStateService.cs
    Services/
      ExerciseStateService.cs
      SyncBannerService.cs    # B1.3 Task 16 (ported from mvp4)
      TimeSyncClient.cs       # B1.3 Task 15 (ported from mvp4)
    ViewModels/
      MainWindowViewModel.cs
    Views/
      Admin/
        AdminShellView.xaml(.cs)
  Sg.App.Tests/
    Sg.App.Tests.csproj       # NUnit 4 + FluentAssertions
    TestCategories.cs
    SmokeTests.cs
```

## Prerequisites

- Windows 11
- **.NET 8 SDK** (`dotnet --version` should report 8.0.x)
- A running `drs-server` instance (default `http://localhost:8000`) — otherwise the time-sync banner will flag `sync_lost` and the exercise-control bar will refuse to start

## Setup

```
git clone https://github.com/mohit-m_constell/ewtss-v2-design.git
cd ewtss-v2-design
```

No package install step — `dotnet build` restores NuGet packages on first run.

## Configuration

Edit `sg-app/Sg.App/appsettings.json` (or override with `appsettings.Development.json` for dev). Key settings:

```json
{
  "DrsServer": {
    "Url": "http://localhost:8000",
    "PollSeconds": 10
  },
  "Serilog": { "MinimumLevel": { "Default": "Information" } }
}
```

The `DrsServer:Url` must point at a reachable `drs-server` (default `http://localhost:8000`). Override via env var prefix `SGAPP_` (e.g. `SGAPP_DrsServer__Url=http://10.0.0.5:8000`).

Logs are written to `C:\EWTSS\sg-app\logs\sg-app-<date>.log` and to the debug console.

## Build

```
dotnet build sg-app/sg-app.sln
```

## Test

```
dotnet test sg-app/sg-app.sln --filter Category=Unit          # 17 unit tests
dotnet test sg-app/sg-app.sln                                 # all (today: same set)
```

## Run

```
dotnet run --project sg-app/Sg.App/Sg.App.csproj
```

On startup the banner host begins polling `drs-server`'s `/time/status` endpoint every `DrsServer:PollSeconds`. The banner reflects the three-tier SyncStateEngine state:

| Banner | Meaning |
|---|---|
| (none) | HEALTHY — offset within tolerance |
| amber | DRIFT_WARN or DRIFT_ALERT |
| red + exercise auto-paused | SYNC_LOST |

See [B1.3 design spec](../../docs/ewtss/specs/time-sync-design.md) for the state-machine details.
