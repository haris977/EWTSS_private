# Metadata-Driven Entity Editor — Design Note

**Status:** **Evaluated 2026-05-02 and deferred — does not fit EWTSS scope.**
The hybrid typed + metadata architecture below is sound for systems with a large
or unbounded entity catalog. EWTSS has a small, fixed vocabulary (~10 entity
types total). The complexity, debugging, performance, and testability costs of a
metadata-driven layer outweigh the savings of avoiding ~16 files of typed boilerplate
for the 3–4 remaining types we actually need (Transmitter, Receiver, Antenna,
maybe Satellite). Forward path is to extend the typed MVP4.5 pattern. Document
preserved for revisiting if the entity catalog ever genuinely explodes (40+
types, customer-driven additions, or domain-user JSON authoring become real
requirements).

**Decision rationale:** see [§ Why deferred (2026-05-02 evaluation)](#why-deferred-2026-05-02-evaluation) below.

**Author of context:** Discussion during MVP4.5 smoke testing.

## Why this exists

MVP4.5 hardcodes a typed DTO + view-model + view per supported entity kind. To add Satellite, GroundStation, Antenna, etc., a developer must touch:

- `Sg.Domain.Contracts.<Entity>Dto.cs`
- `Sg.Domain.Contracts.IScenarioBackend.cs` (new `Get<Entity>` / `Update<Entity>` pair)
- `Sg.Domain.Stk.StkScenarioBackend.cs` (read/write COM via the entity's PIA)
- `Sg.Domain.ViewModels.<Entity>ViewModel.cs`
- `Sg.App.Views.<Entity>View.xaml/.cs`
- `Sg.Tests.Fakes.FakeScenarioBackend.cs` (new dictionary)
- `PanelFactory` switch
- New tests

That's eight files for one entity. The 6 supported today represent maybe 5% of STK's catalog (Aircraft, Satellite, Missile, ShipVehicle, GroundVehicle, Facility, Place, Target, AreaTarget, LineTarget, Sensor, Antenna, Receiver, Transmitter, GroundStation, Comm, Constellation, Chain, CoverageDefinition, FigureOfMerit, …). The hardcoded path doesn't scale to "STK desktop UI parity."

## Why deferred (2026-05-02 evaluation)

After feasibility-sketching the approach against Transmitter / Receiver (the most likely first long-tail target for EWTSS) and weighing complexity, debugging, performance, and testability against the typed pattern proven in MVP4.5:

**Entity-count math doesn't justify the framework.** The case FOR metadata-driven editors is "many entity types beyond a small core." EWTSS's actual vocabulary is small and finite:

- Done: Aircraft, Facility, AreaTarget, Sensor, Coverage, FOM (6)
- Likely needed: Transmitter, Receiver, Antenna (3)
- Maybe: Satellite (1, if space ISR enters scope)

Total ~10 types — not 30+. Adding 3–4 typed entities using the proven MVP4.5 template costs ~3–6 weeks of straightforward work. Building the metadata framework first costs ~3 weeks for the framework alone (schema + generic editor + Connect-command translator + unit-pref orchestration), plus per-descriptor work that isn't free for the harder types, plus an open-ended debugging tail. **Break-even is well past EWTSS's actual scope.**

**Debugging cost compounds the silent-failure surface.** MVP4.5 surfaced multiple silent STK failure modes: `Position.Assign` no-oping, `OnObjectEditingApply` not firing on user drag, `DblClick` firing on every click, unit-preference state affecting reads/writes globally, `_globe3D.Refresh()` requirements after `StartObjectEditing`. Each was diagnosable because the failing call lived in typed C# code that could be `grep`'d, set breakpoints on, and reasoned about with the type system. With a metadata layer, the failing call is a string assembled by a generic dispatcher; integration tests against real STK become essential to verify each descriptor actually does what it claims. The debugging surface gets larger, not smaller.

**Performance is tolerably worse but real.** Each property field becomes a Connect or DataProvider round-trip (read 20 fields = 20 trips). Typed COM is one call returning a struct. Acceptable for property panels (low frequency), but not free.

**Testability splits in two.** Generic engine + per-entity JSON descriptor = two independent things to test. JSON validation tests don't catch behavioural mismatches; only real-STK integration does. Typed code is C#-compiler-validated and `FakeScenarioBackend`-coverable.

**Audit / certification narrative is cleaner with typed code.** Defense-grade traceability: a reviewer greps `UpdateTransmitter` and follows the data flow. Metadata-driven means the reviewer reads JSON, then reads a generic engine that assembles strings at runtime. Available either way, but typed is shorter.

**Browser-future doubles the contract surface.** The DTO boundary already gives us browser-readiness for the 6 typed entities. Adding metadata-driven entities introduces a second API style (property bags); the browser frontend handles both shapes. More frontend work, more contract testing, more failure modes.

**Where metadata-driven would make sense (none apply to EWTSS):**

- Unbounded or customer-driven entity vocabulary (we have a fixed ~10)
- Site-specific customisation by domain users authoring JSON (ops teams won't author JSON; recompile-and-deploy is the EWTSS norm)
- 30+ entity types where typed code becomes genuinely unmaintainable

**Forward path:** add Transmitter, Receiver, Antenna (and Satellite if it enters scope) as **typed entities** following the MVP4.5 template — `<Entity>Dto`, `IScenarioBackend.Get/Update<Entity>`, `StkScenarioBackend` impl using `tx.Model.Power = …` etc. per the reference repo, plus `<Entity>ViewModel` + WPF panel.

**Revisit triggers** (any of these should reopen this design):

- Customer requirement to author scenarios with arbitrary STK entity types (not the EWTSS-fixed set)
- Site-specific entity-type customisation by ops or integrator teams
- Entity catalogue grows past ~25 types
- A separate product line emerges where the EWTSS authoring shell is reused with a different domain vocabulary

If any of those becomes real, the rest of this document is the design starting point.

## STK APIs that enable a generic approach

STK exposes two introspection layers usable from our COM PIA:

### 1. `IAgStkObject.DataProviders`

Every STK object exposes a hierarchical collection of named *data providers*. Each provider has:

- `Name` (e.g. `"LLA Position"`, `"Cartesian Position"`, `"Vehicle Properties"`)
- `Type` (`Fixed` / `TimeVarying` / `Group`)
- `Elements` — list of named, unit-typed columns (`Lat[deg]`, `Lon[deg]`, `Alt[m]`)
- `Exec(start, stop, step)` — returns rows of values

This is what STK desktop's **Report & Graph Manager** uses. It works for any object class, including types we've never seen.

### 2. STK Connect text commands

A line-oriented command surface that reads and writes properties on any object by string path:

```
SetPosition  */Facility/F1 Geodetic 12.34 56.78 0
ColorRGB     */Facility/F1 255 0 255
SetState     */Aircraft/A1 ClassicalNumber UTCG "1 Jan 2026 00:00:00.000" 6378 0 28.5 0 90 0
GetReport    */Aircraft/A1 "LLA Position"
```

Connect commands are the workhorse behind much of STK desktop's property dialogs and all of STK's automation tutorials. They cover essentially every operation the COM Object Model exposes plus some that the Object Model doesn't.

### What's NOT available

There is no canonical "list every settable property of class X with its type and constraints" endpoint. STK's PIA gives this implicitly via static C# typing on `IAgAircraft`, `IAgFacility`, etc., but no runtime reflection over those typed surfaces. Treaty: **read** is generic via DataProviders; **write** is generic via Connect, but we provide the curation.

## The naive trap: "just show every DataProvider field"

Building a generic editor that surfaces every DataProvider element of an object produces a UI nobody wants:

- Hundreds of fields per object, most irrelevant to the authoring task
- No grouping, no validation, no domain-meaningful labels
- Time-varying providers don't fit a property-page metaphor
- Read-only computed fields mixed with editable inputs

STK desktop's per-type property pages are **hand-curated** for exactly this reason. The right pattern is metadata-driven, not raw introspection.

## Proposed architecture: hybrid typed + metadata

Keep the existing typed DTOs for the **core authoring entities** where strong typing and tight UX earn their keep. Layer a metadata-driven editor alongside for the **long tail** of STK types we want to support without writing C# per type.

### Two surfaces in `IScenarioBackend`

```csharp
// Existing — typed, audited, browser-future-ready
AircraftDto GetAircraft(string path);
void UpdateAircraft(string path, AircraftDto dto);
// ... 5 more typed pairs for Facility, AreaTarget, Sensor, Coverage, FOM

// NEW — generic property bag for long-tail types
PropertyBagDto GetProperties(string path);
void UpdateProperties(string path, PropertyBagDto dto);
ConnectResultDto SendConnect(string command);
IReadOnlyList<DataProviderInfoDto> GetDataProviders(string path);
ReportDto ExecuteReport(string path, string providerName, DateTime start, DateTime stop, TimeSpan step);
```

`PropertyBagDto` would be `IReadOnlyDictionary<string, PropertyValueDto>` where `PropertyValueDto` is a tagged union (string, double, bool, enum-as-string, list-of-vertex, etc.).

### JSON metadata per entity type

For every long-tail STK kind we want to expose, add one descriptor file:

```
mvp4/Sg.Mvp4.App/EntityMetadata/Satellite.json
```

```jsonc
{
  "kind": "Satellite",
  "stkClassType": "eSatellite",
  "displayName": "Satellite",
  "icon": "satellite-16.png",
  "groups": [
    {
      "label": "Basic",
      "fields": [
        { "key": "Color", "type": "color", "connectGet": "ColorRGB",
          "connectSet": "ColorRGB {r} {g} {b}" },
        { "key": "Visible", "type": "bool",
          "connectGet": "VisibleObject GetState",
          "connectSet": "VisibleObject {value}" }
      ]
    },
    {
      "label": "Orbit",
      "fields": [
        { "key": "Epoch", "type": "datetime-utc",
          "connectGet": "GetState OrbitEpoch",
          "connectSet": "SetState ClassicalNumber UTCG \"{value}\" {sma} {ecc} {inc} {raan} {argp} {ta}" },
        { "key": "SemiMajorAxis", "type": "double", "unit": "km" },
        { "key": "Eccentricity",  "type": "double", "min": 0, "max": 0.99999 },
        { "key": "Inclination",   "type": "double", "unit": "deg" }
      ]
    }
  ]
}
```

Adding a new STK type is then **one JSON file, zero C# changes**. The generic editor reads the descriptor and builds:

- A property panel form (group → field) at runtime
- Connect commands for read (on panel open) and write (on Apply)
- Validation from `min`/`max`/`pattern`/`enum` annotations

### What stays hardcoded vs goes metadata

| Path | Why |
|------|-----|
| **Hardcoded:** Aircraft, Facility, AreaTarget | Map-driven authoring (click-to-place, drag handles, vertex collections) needs custom UI that doesn't fit a property-page metaphor. |
| **Hardcoded:** Sensor, Coverage, FOM | Tightly tied to scenario authoring flow + parent/child semantics that warrant explicit code. |
| **Metadata:** Satellite, Missile, GroundVehicle, Place, Target, Antenna, Receiver, Transmitter, GroundStation, Constellation, Chain, etc. | Standard property-page UX; gain breadth quickly. |

## UI generation strategy

WinForms (post-rewrite) gets a `MetadataPropertyPanel` user control that:

1. Takes `EntityNodeDto` + a loaded `EntityMetadata` JSON
2. Builds a `TableLayoutPanel` with one row per field, two columns (label + editor)
3. Picks editor by `field.type`: `string` → TextBox, `double` → NumericUpDown, `bool` → CheckBox, `enum` → ComboBox, `color` → color button + dialog, `datetime-utc` → DateTimePicker, `vertex-list` → grid
4. On panel open: issues each field's `connectGet`, populates editors
5. On Apply: collects dirty editors, issues each field's `connectSet`, then `IScenarioBackend.RaiseScenarioChanged()` so the tree/map refresh

Loading the metadata at startup (scan `EntityMetadata/*.json`) means hot-adding a new entity type can be as simple as dropping a file in the install dir. Useful for site-specific customizations.

## Browser-future implications

The browser-future plan (per `mvp4.5-dto-boundary-and-perf-design.md`) sends typed JSON DTOs over HTTP. Adding a property-bag surface alongside it means:

- Browser has **two API styles** to handle: typed (current 6 entities) and dynamic (long tail). Frontend grid/form components have to support both shapes.
- The `EntityMetadata/*.json` descriptors become a **shared schema** between desktop and browser — both consume the same files to drive their UI.
- JSON contract tests (`DtoJsonRoundTripTests`) extend to property-bag round-trip.

This is workable but adds frontend complexity. Worth budgeting an extra week of frontend work when the browser frontend is built.

## Tradeoffs

| Aspect | Pure typed (today) | Hybrid typed + metadata | Pure metadata |
|--------|--------------------|------------------------|----------------|
| Adding 1 entity type | ~8 files, 1-2 days | 1 JSON file, 1 hour | 1 JSON file, 1 hour |
| Compile-time safety | Strong | Strong for core, weak for long tail | Weak everywhere |
| Browser contract | Clean | Two styles | Single dynamic shape |
| Audit clarity (defense) | High | High for core, medium for long tail | Low |
| Map-driven authoring (drag handles, click-to-place) | Trivial | Trivial for core | Hard — requires per-type UI hooks anyway |
| Long-tail breadth | Slow to grow | Fast | Fast |

The hybrid is the recommended shape: typed where authoring is rich and audit matters, metadata for everything else.

## Implementation outline (when we pursue this)

1. **Add the property-bag types to `Sg.Domain.Contracts`**
   - `PropertyValueDto` (tagged union via `Kind` discriminator + value field)
   - `PropertyBagDto`
   - `DataProviderInfoDto`, `ReportDto`
   - `ConnectResultDto`
2. **Extend `IScenarioBackend` with the 5 new methods** above. Round-trip tests for each.
3. **Implement in `StkScenarioBackend`**
   - `SendConnect(cmd)` → `Root.ExecuteCommand(cmd)`
   - `GetDataProviders(path)` → enumerate `IAgStkObject.DataProviders` recursively
   - `ExecuteReport(...)` → DataProvider exec
   - `GetProperties(path)` / `UpdateProperties(path, bag)` → driven by metadata for the entity's class type
4. **Metadata loader**
   - `EntityMetadataRegistry.Load("EntityMetadata/")`
   - Validation at load time (referenced units exist, command templates parse, etc.)
5. **`MetadataPropertyPanel` user control** in the WinForms App project
6. **Wire `PanelFactory`** to dispatch to either the typed view (Aircraft, Facility, etc.) or `MetadataPropertyPanel` (everything else)
7. **First three long-tail JSON descriptors** (Satellite, GroundStation, Antenna) as proof-of-coverage
8. **Tests**
   - Round-trip tests for property-bag DTOs
   - Connect-command template parser tests
   - Metadata loader validation tests
   - Integration test: load Satellite metadata, create satellite, edit via panel, verify COM state

Estimated effort: ~1 week of focused work for the framework + first 3 long-tail types, post-WinForms rewrite.

## Out of scope for this note

- Time-varying property editing (e.g. waypoint sequences over time) — stays on typed surface
- Custom validators beyond `min`/`max`/`enum`/`pattern` — defer until we hit a use case
- I18n of metadata `displayName` / `groups[].label` — defer
- Auto-generating metadata files from the STK PIA — possible (System.Reflection over `AGI.STKObjects`), not pursued because the hand-curated subset is small and the auto-generated output would still need pruning

## When to revisit

See [Why deferred (2026-05-02 evaluation)](#why-deferred-2026-05-02-evaluation) above for the explicit revisit triggers. Short version: only if the entity vocabulary becomes unbounded, customer-customisable, or much larger (~25+ types). For everything inside EWTSS scope, extending the typed MVP4.5 pattern is the cheaper and safer path.
