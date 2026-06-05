using System.Runtime.InteropServices;
using AGI.STKObjects;
using AGI.STKUtil;
using AGI.STKX;
using Sg.Mvp4.Domain.Contracts;
using Sg.Mvp4.Domain.Models;

namespace Sg.Mvp4.Domain.Stk;

public sealed class StkScenarioBackend : IScenarioBackend, IDisposable
{
    private object?           _stkApp;
    private AgStkObjectRoot?  _root;

    public string? CurrentPath       { get; private set; }
    public bool    CurrentIsPackaged { get; private set; }

    public event EventHandler? ScenarioChanged;

    public StkScenarioBackend()
    {
        Initialize();
    }

    private void Initialize()
    {
        if (_root != null) return;

        AgSTKXApplication app;
        try
        {
            app = new AgSTKXApplication();
            ((dynamic)app).NoGraphics = false;
        }
        catch (COMException ex)
        {
            throw new InvalidOperationException(
                ex.ErrorCode == unchecked((int)0x80040154)
                    ? "Could not initialize STK Engine. Ensure STK 12 (64-bit) is installed and registered."
                    : ex.Message,
                ex);
        }

        if (!app.IsFeatureAvailable(AgEFeatureCodes.eFeatureCodeGlobeControl))
            throw new InvalidOperationException(
                "Required STK license (eFeatureCodeGlobeControl) is not available.");

        _stkApp = app;
        _root   = new AgStkObjectRoot();

        var units = _root.UnitPreferences;
        units.ResetUnits();
        units.SetCurrentUnit("DateFormat", "UTCG");
    }

    internal AgStkObjectRoot Root => _root
        ?? throw new InvalidOperationException("StkScenarioBackend not initialized.");

    public string? ScenarioName => (Root.CurrentScenario as IAgStkObject)?.InstanceName;

    public DateTime StartTimeUtc =>
        Root.CurrentScenario is IAgScenario s
            ? DateTime.Parse((string)s.StartTime,
                             System.Globalization.CultureInfo.InvariantCulture,
                             System.Globalization.DateTimeStyles.AssumeUniversal).ToUniversalTime()
            : default;

    public DateTime StopTimeUtc =>
        Root.CurrentScenario is IAgScenario s
            ? DateTime.Parse((string)s.StopTime,
                             System.Globalization.CultureInfo.InvariantCulture,
                             System.Globalization.DateTimeStyles.AssumeUniversal).ToUniversalTime()
            : default;

    public void NewScenario(string name, DateTime startUtc, DateTime stopUtc)
    {
        if (Root.CurrentScenario != null) Root.CloseScenario();
        Root.NewScenario(name);
        Root.UnitPreferences.SetCurrentUnit("DateFormat", "UTCG");
        var sc = (IAgScenario)Root.CurrentScenario;
        sc.SetTimePeriod(ToStkTime(startUtc), ToStkTime(stopUtc));
        sc.Epoch = ToStkTime(startUtc);
        ScenarioChanged?.Invoke(this, EventArgs.Empty);
        PumpMessages();
    }

    public void CloseScenario()
    {
        if (_root == null) return;
        // Don't gate on CurrentScenario: a failed NewScenario can leave STK in a
        // half-state where CurrentScenario is null but the named scenario exists
        // and blocks subsequent NewScenario with "already exists". Closing
        // unconditionally and swallowing the no-current-scenario COMException
        // recovers cleanly from that case.
        try { _root.CloseScenario(); }
        catch (COMException) { }
        ScenarioChanged?.Invoke(this, EventArgs.Empty);
        PumpMessages();
    }

    /// <summary>
    /// Raises <see cref="ScenarioChanged"/>. Used by the desktop shell after STK
    /// mutates entity state outside our own Update calls (e.g. drag-handle edits
    /// applied via OnObjectEditingApply / OnObjectEditingStop).
    /// </summary>
    internal void RaiseScenarioChanged() => ScenarioChanged?.Invoke(this, EventArgs.Empty);

    public IReadOnlyList<EntityNodeDto> Children
    {
        get
        {
            if (_root?.CurrentScenario is null) return Array.Empty<EntityNodeDto>();
            var list = new List<EntityNodeDto>();
            foreach (IAgStkObject child in Root.CurrentScenario.Children)
                list.Add(SnapshotNode(child));
            return list;
        }
    }

    public EntityNodeDto? FindByPath(string path)
    {
        var com = ResolveByPath(path);
        return com is null ? null : SnapshotNode(com);
    }

    public EntityNodeDto AddEntity(EntityKind kind, string name, string? parentPath)
    {
        IAgStkObject parent = parentPath is null
            ? (IAgStkObject)Root.CurrentScenario
            : ResolveByPath(parentPath)
                ?? throw new InvalidOperationException($"Parent '{parentPath}' not found.");

        var classType = ToClassType(kind);
        var created   = parent.Children.New(classType, name);

        ScenarioChanged?.Invoke(this, EventArgs.Empty);
        PumpMessages();
        return SnapshotNode(created);
    }

    public void RemoveEntity(string path)
    {
        var segments = path.Split('/');
        IAgStkObject parent = (IAgStkObject)Root.CurrentScenario;
        for (var i = 0; i < segments.Length - 1; i++)
            parent = parent.Children[segments[i]];
        var target    = parent.Children[segments[^1]];
        var classType = ToClassType(KindOf(target));
        parent.Children.Unload(classType, segments[^1]);
        ScenarioChanged?.Invoke(this, EventArgs.Empty);
        PumpMessages();
    }

    private IAgStkObject? ResolveByPath(string path)
    {
        if (_root?.CurrentScenario is null) return null;
        var seg = path.Split('/');
        IAgStkObject? cursor = Root.CurrentScenario.Children[seg[0]];
        for (var i = 1; i < seg.Length && cursor is not null; i++)
            cursor = cursor.Children[seg[i]];
        return cursor;
    }

    private static EntityNodeDto SnapshotNode(IAgStkObject obj)
    {
        var children = new List<EntityNodeDto>();
        foreach (IAgStkObject child in obj.Children)
            children.Add(SnapshotNode(child));
        return new EntityNodeDto(KindOf(obj), obj.InstanceName, BuildPath(obj), children);
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

    public AircraftDto GetAircraft(string path)
    {
        if (ResolveByPath(path) is not IAgAircraft ac)
            throw new InvalidOperationException($"Aircraft not found: '{path}'.");

        string colorHex = "#00FFFF";
        bool   pathVis  = false;
        try
        {
            ac.Graphics.SetAttributesType(AgEVeGfxAttributes.eAttributesBasic);
            if (ac.Graphics.Attributes is IAgVeGfxAttributesBasic basic)
            {
                var c = basic.Color;
                colorHex = $"#{c.R:X2}{c.G:X2}{c.B:X2}";
            }
            pathVis = ac.Graphics.IsObjectGraphicsVisible;
        }
        catch { }

        var wps = new List<WaypointDto>();
        try
        {
            if (ac.Route is IAgVePropagatorGreatArc route)
            {
                foreach (IAgVeWaypointsElement e in route.Waypoints)
                    wps.Add(new WaypointDto(
                        LatitudeDeg:    (double)e.Latitude,
                        LongitudeDeg:   (double)e.Longitude,
                        AltitudeMeters: e.Altitude,
                        SpeedMps:       e.Speed));
            }
        }
        catch { }

        return new AircraftDto(((IAgStkObject)ac).InstanceName, colorHex, pathVis, wps);
    }

    public FacilityDto GetFacility(string path)
    {
        if (ResolveByPath(path) is not IAgFacility fa)
            throw new InvalidOperationException($"Facility not found: '{path}'.");

        string colorHex = "#FF00FF";
        try
        {
            var c = fa.Graphics.Color;
            colorHex = $"#{c.R:X2}{c.G:X2}{c.B:X2}";
        }
        catch { }

        var pd = (IAgPlanetodetic)fa.Position.ConvertTo(AgEPositionType.ePlanetodetic);
        return new FacilityDto(
            ((IAgStkObject)fa).InstanceName,
            colorHex,
            Convert.ToDouble(pd.Lat),
            Convert.ToDouble(pd.Lon),
            Convert.ToDouble(pd.Alt));
    }

    public AreaTargetDto GetAreaTarget(string path)
    {
        if (ResolveByPath(path) is not IAgAreaTarget at)
            throw new InvalidOperationException($"AreaTarget not found: '{path}'.");

        string outline = "#FF0000", fill = "#FF0000";
        bool   fillEnabled = false;
        try
        {
            outline     = $"#{at.Graphics.BoundaryColor.R:X2}{at.Graphics.BoundaryColor.G:X2}{at.Graphics.BoundaryColor.B:X2}";
            fillEnabled = at.Graphics.BoundaryFill;
            fill        = $"#{at.Graphics.Color.R:X2}{at.Graphics.Color.G:X2}{at.Graphics.Color.B:X2}";
        }
        catch { }

        var verts = new List<VertexDto>();
        try
        {
            if (at.AreaType == AgEAreaType.ePattern &&
                at.AreaTypeData is IAgAreaTypePatternCollection pattern)
            {
                for (int i = 0; i < pattern.Count; i++)
                {
                    var p = pattern[i];
                    verts.Add(new VertexDto(
                        LatitudeDeg:  Convert.ToDouble(p.Lat),
                        LongitudeDeg: Convert.ToDouble(p.Lon)));
                }
            }
        }
        catch { }

        return new AreaTargetDto(((IAgStkObject)at).InstanceName, outline, fillEnabled, fill, verts);
    }

    public SensorDto GetSensor(string path)
    {
        if (ResolveByPath(path) is not IAgSensor sn)
            throw new InvalidOperationException($"Sensor not found: '{path}'.");

        string colorHex = "#00FFFF";
        bool   showInt  = false;
        double outerHa  = 5.0;
        try
        {
            var c = sn.Graphics.Color;
            colorHex = $"#{c.R:X2}{c.G:X2}{c.B:X2}";
            showInt  = sn.Graphics.Enable;
        }
        catch { }
        try
        {
            if (sn.Pattern is IAgSnSimpleConicPattern conic)
                outerHa = Convert.ToDouble(conic.ConeAngle);
        }
        catch { }

        string parent = path.Contains('/')
            ? path.Substring(0, path.LastIndexOf('/'))
            : "";

        return new SensorDto(
            ((IAgStkObject)sn).InstanceName, parent, colorHex, showInt,
            OuterHalfAngleDeg: outerHa,
            InnerHalfAngleDeg: 0.0,
            Pointing:          PointingMode.Fixed,
            FixedAzimuthDeg:   0.0,
            FixedElevationDeg: 0.0,
            TargetPath:        null);
    }

    public CoverageDefinitionDto GetCoverage(string path)
    {
        if (ResolveByPath(path) is not IAgCoverageDefinition cv)
            throw new InvalidOperationException($"CoverageDefinition not found: '{path}'.");

        double latMin = 0, latMax = 0, lonMin = 0, lonMax = 0, latStep = 1;
        try
        {
            cv.Grid.BoundsType = AgECvBounds.eBoundsLatLonRegion;
            var b = (IAgCvBoundsLatLonRegion)cv.Grid.Bounds;
            latMin = Convert.ToDouble(b.MinLatitude);
            latMax = Convert.ToDouble(b.MaxLatitude);
            lonMin = Convert.ToDouble(b.MinLongitude);
            lonMax = Convert.ToDouble(b.MaxLongitude);
            cv.Grid.ResolutionType = AgECvResolution.eResolutionLatLon;
            latStep = Convert.ToDouble(((IAgCvResolutionLatLon)cv.Grid.Resolution).LatLon);
        }
        catch { }

        var assets = new List<string>();
        try
        {
            for (var i = 0; i < cv.AssetList.Count; i++)
                assets.Add(cv.AssetList[i].ObjectName);
        }
        catch { }

        return new CoverageDefinitionDto(
            ((IAgStkObject)cv).InstanceName,
            latMin, latMax, lonMin, lonMax,
            latStep, latStep,
            MinAltitude:        0.0,
            MinElevationAngle:  5.0,
            AssetPaths:         assets);
    }

    public FigureOfMeritDto GetFom(string path)
    {
        if (ResolveByPath(path) is not IAgFigureOfMerit fm)
            throw new InvalidOperationException($"FigureOfMerit not found: '{path}'.");

        var kind = fm.DefinitionType switch
        {
            AgEFmDefinitionType.eFmAccessDuration   => FomKind.AccessDuration,
            AgEFmDefinitionType.eFmRevisitTime      => FomKind.RevisitTime,
            AgEFmDefinitionType.eFmCoverageTime     => FomKind.Coverage,
            AgEFmDefinitionType.eFmNumberOfAccesses => FomKind.SampleCount,
            _ => FomKind.AccessDuration
        };

        FomStatistic stat = FomStatistic.Maximum;
        try
        {
            if (fm.Definition is IAgFmDefCompute computeDef)
            {
                stat = computeDef.ComputeType switch
                {
                    AgEFmCompute.eMinimum => FomStatistic.Minimum,
                    AgEFmCompute.eAverage => FomStatistic.Average,
                    AgEFmCompute.eTotal   => FomStatistic.Total,
                    _                      => FomStatistic.Maximum
                };
            }
        }
        catch { }

        string parent = path.Contains('/')
            ? path.Substring(0, path.LastIndexOf('/'))
            : "";

        return new FigureOfMeritDto(((IAgStkObject)fm).InstanceName, parent, kind, stat);
    }

    public void UpdateAircraft(string path, AircraftDto dto)
    {
        if (ResolveByPath(path) is not IAgAircraft ac)
            throw new InvalidOperationException($"Aircraft not found: '{path}'.");

        Root.BeginUpdate();
        try
        {
            ac.Graphics.SetAttributesType(AgEVeGfxAttributes.eAttributesBasic);
            if (ac.Graphics.Attributes is IAgVeGfxAttributesBasic basic)
                basic.Color = System.Drawing.ColorTranslator.FromHtml(dto.ColorHex);

            ac.Graphics.IsObjectGraphicsVisible = dto.PathVisible;

            ac.SetRouteType(AgEVePropagatorType.ePropagatorGreatArc);
            var route = (IAgVePropagatorGreatArc)ac.Route;
            route.Method = AgEVeWayPtCompMethod.eDetermineTimeAccFromVel;
            route.Waypoints.RemoveAll();
            foreach (var p in dto.Waypoints)
            {
                var wp = route.Waypoints.Add();
                wp.Latitude  = p.LatitudeDeg;
                wp.Longitude = p.LongitudeDeg;
                wp.Altitude  = p.AltitudeMeters;
                wp.Speed     = p.SpeedMps <= 0 ? 100 : p.SpeedMps;
            }
            route.Propagate();
        }
        finally { Root.EndUpdate(); }

        ScenarioChanged?.Invoke(this, EventArgs.Empty);
        PumpMessages();
    }

    public void UpdateFacility(string path, FacilityDto dto)
    {
        if (ResolveByPath(path) is not IAgFacility fa)
            throw new InvalidOperationException($"Facility not found: '{path}'.");

        Root.BeginUpdate();
        try
        {
            fa.Graphics.Color = System.Drawing.ColorTranslator.FromHtml(dto.ColorHex);

            // Position.AssignGeodetic is the canonical single-shot setter — matches
            // the reference repo and is equivalent to Connect's "SetPosition Geodetic".
            // The earlier ConvertTo+Assign pattern silently no-ops on this STK build,
            // leaving the facility at AGI's default HQ position in Exton, PA.
            fa.Position.AssignGeodetic(dto.LatitudeDeg, dto.LongitudeDeg, dto.AltitudeMeters);
        }
        finally { Root.EndUpdate(); }

        ScenarioChanged?.Invoke(this, EventArgs.Empty);
        PumpMessages();
    }

    public void UpdateAreaTarget(string path, AreaTargetDto dto)
    {
        if (ResolveByPath(path) is not IAgAreaTarget at)
            throw new InvalidOperationException($"AreaTarget not found: '{path}'.");

        Root.BeginUpdate();
        try
        {
            at.Graphics.BoundaryColor = System.Drawing.ColorTranslator.FromHtml(dto.OutlineColorHex);
            at.Graphics.BoundaryFill  = dto.FillEnabled;
            at.Graphics.Color         = System.Drawing.ColorTranslator.FromHtml(dto.FillColorHex);

            at.AreaType = AgEAreaType.ePattern;
            var pattern = (IAgAreaTypePatternCollection)at.AreaTypeData;
            pattern.RemoveAll();
            foreach (var v in dto.Vertices)
                pattern.Add(v.LatitudeDeg, v.LongitudeDeg);
        }
        finally { Root.EndUpdate(); }

        ScenarioChanged?.Invoke(this, EventArgs.Empty);
        PumpMessages();
    }

    public void UpdateSensor(string path, SensorDto dto)
    {
        if (ResolveByPath(path) is not IAgSensor sn)
            throw new InvalidOperationException($"Sensor not found: '{path}'.");

        Root.BeginUpdate();
        try
        {
            sn.Graphics.Color  = System.Drawing.ColorTranslator.FromHtml(dto.ColorHex);
            sn.Graphics.Enable = dto.ShowIntersection;

            sn.CommonTasks.SetPatternSimpleConic(dto.OuterHalfAngleDeg, 1);

            if (dto.Pointing == PointingMode.Fixed)
            {
                sn.CommonTasks.SetPointingFixedAzEl(
                    dto.FixedAzimuthDeg, dto.FixedElevationDeg,
                    AgEAzElAboutBoresight.eAzElAboutBoresightRotate);
            }
            else
            {
                sn.SetPointingType(AgESnPointing.eSnPtTargeted);
                if (sn.Pointing is IAgSnPtTargeted targeted && dto.TargetPath is not null)
                {
                    targeted.Targets.RemoveAll();
                    targeted.Targets.Add($"*/{dto.TargetPath}");
                }
            }
        }
        finally { Root.EndUpdate(); }

        ScenarioChanged?.Invoke(this, EventArgs.Empty);
        PumpMessages();
    }

    public void UpdateCoverage(string path, CoverageDefinitionDto dto)
    {
        if (ResolveByPath(path) is not IAgCoverageDefinition cv)
            throw new InvalidOperationException($"CoverageDefinition not found: '{path}'.");

        Root.BeginUpdate();
        try
        {
            cv.Grid.BoundsType = AgECvBounds.eBoundsLatLonRegion;
            var b = (IAgCvBoundsLatLonRegion)cv.Grid.Bounds;
            b.MinLatitude  = dto.LatMin;
            b.MaxLatitude  = dto.LatMax;
            b.MinLongitude = dto.LonMin;
            b.MaxLongitude = dto.LonMax;

            cv.Grid.ResolutionType = AgECvResolution.eResolutionLatLon;
            ((IAgCvResolutionLatLon)cv.Grid.Resolution).LatLon = dto.LatStep;
            // LonStep is in-memory only (PIA exposes single combined step)

            cv.AssetList.RemoveAll();
            foreach (var p in dto.AssetPaths)
                cv.AssetList.Add($"*/{p}");
            // MinAltitude / MinElevationAngle are in-memory only (require Connect commands)
        }
        finally { Root.EndUpdate(); }

        ScenarioChanged?.Invoke(this, EventArgs.Empty);
        PumpMessages();
    }

    public void UpdateFom(string path, FigureOfMeritDto dto)
    {
        if (ResolveByPath(path) is not IAgFigureOfMerit fm)
            throw new InvalidOperationException($"FigureOfMerit not found: '{path}'.");

        Root.BeginUpdate();
        try
        {
            var defType = dto.Kind switch
            {
                FomKind.AccessDuration => AgEFmDefinitionType.eFmAccessDuration,
                FomKind.RevisitTime    => AgEFmDefinitionType.eFmRevisitTime,
                FomKind.Coverage       => AgEFmDefinitionType.eFmCoverageTime,
                FomKind.SampleCount    => AgEFmDefinitionType.eFmNumberOfAccesses,
                _ => AgEFmDefinitionType.eFmAccessDuration
            };
            fm.SetDefinitionType(defType);

            if (fm.Definition is IAgFmDefCompute computeDef)
            {
                var comType = dto.Statistic switch
                {
                    FomStatistic.Minimum => AgEFmCompute.eMinimum,
                    FomStatistic.Average => AgEFmCompute.eAverage,
                    FomStatistic.Total   => AgEFmCompute.eTotal,
                    _                     => AgEFmCompute.eMaximum
                };
                computeDef.SetComputeType(comType);
            }
        }
        finally { Root.EndUpdate(); }

        ScenarioChanged?.Invoke(this, EventArgs.Empty);
        PumpMessages();
    }

    public ComputeResultDto ComputeAll()
    {
        if (_root?.CurrentScenario is null)
            return new ComputeResultDto(false, "No scenario loaded.", DateTime.UtcNow);

        var errors = new List<string>();
        foreach (IAgStkObject child in Root.CurrentScenario.Children)
        {
            if (KindOf(child) != EntityKind.CoverageDefinition) continue;
            try { ((IAgCoverageDefinition)child).ComputeAccesses(); }
            catch (Exception ex) { errors.Add($"{child.InstanceName}: {ex.Message}"); }
        }

        PumpMessages();
        return errors.Count == 0
            ? new ComputeResultDto(true,  null,                                  DateTime.UtcNow)
            : new ComputeResultDto(false, string.Join("; ", errors),             DateTime.UtcNow);
    }

    public void SaveAsSc(string path)
    {
        System.IO.Directory.CreateDirectory(System.IO.Path.GetDirectoryName(path)!);
        Root.SaveScenarioAs(path);
        CurrentPath       = path;
        CurrentIsPackaged = false;
    }

    public void SaveAsVdf(string path, string password)
    {
        System.IO.Directory.CreateDirectory(System.IO.Path.GetDirectoryName(path)!);
        Root.SaveVDFAs(path, password ?? "", "", "");
        CurrentPath       = path;
        CurrentIsPackaged = true;
    }

    public void LoadSc(string path)
    {
        if (Root.CurrentScenario is not null) Root.CloseScenario();
        Root.LoadScenario(path);
        CurrentPath       = path;
        CurrentIsPackaged = false;
        ScenarioChanged?.Invoke(this, EventArgs.Empty);
        PumpMessages();
    }

    public void LoadVdf(string path, string password)
    {
        if (Root.CurrentScenario is not null) Root.CloseScenario();
        Root.LoadVDF(path, password ?? "");
        CurrentPath       = path;
        CurrentIsPackaged = true;
        ScenarioChanged?.Invoke(this, EventArgs.Empty);
        PumpMessages();
    }
    public string SuggestedPath(string scenarioName)
        => System.IO.Path.Combine(@"C:\EWTSS\mvp4\scenarios", scenarioName, $"{scenarioName}.sc");

    internal static string ToStkTime(DateTime utc)
        => utc.ToString("dd MMM yyyy HH:mm:ss.fff",
                        System.Globalization.CultureInfo.InvariantCulture);

    internal static void PumpMessages()
        => System.Windows.Forms.Application.DoEvents();

    public void Dispose()
    {
        if (_root is not null)
        {
            try { if (_root.CurrentScenario != null) _root.CloseScenario(); } catch { }
            Marshal.FinalReleaseComObject(_root);
            _root = null;
        }
        if (_stkApp is not null)
        {
            try { Marshal.FinalReleaseComObject(_stkApp); } catch { }
            _stkApp = null;
        }
        GC.SuppressFinalize(this);
    }
}
