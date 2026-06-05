using AGI.STKGraphics;
using AGI.STKObjects;
using AGI.STKX.Controls;
// AxAGI.STKX is referenced (via the Controls.Interop assembly which re-exports
// its delegate types) for the event-handler delegate types. We don't import the
// namespace here because both AGI.STKX.Controls and AxAGI.STKX define
// AxAgUiAxVOCntrl/AxAgUiAx2DCntrl and the C# importer would treat the unqualified
// names as ambiguous. The handler-delegate fields below qualify their types.
using Sg.Domain.Interaction;
using Sg.Domain.Models;
using Sg.Domain.Stk;
using WpfUserControl = System.Windows.Controls.UserControl;

namespace Sg.App.Views.Shell;

/// <summary>
/// Hosts the STK ActiveX 3D globe and 2D map controls inside WPF via WindowsFormsHost.
///
/// AGI.STKX.AgSTKXApplication is the in-process COM singleton that the ActiveX controls
/// automatically bind to when they are created.  It must be instantiated before the
/// controls are parented so the STK COM context exists on first render.
///
/// Neither AxAgUiAxVOCntrl nor AxAgUiAx2DCntrl expose a SetAGISTKObject method; the
/// binding to the STK engine root happens implicitly through the shared process-level
/// COM singleton.
/// </summary>
public partial class StkDisplayHost : WpfUserControl
{
    // The AgSTKXApplication COM singleton is bootstrapped by StkRootService.Root.
    // App.OnStartup calls stk.NewScenario(...) BEFORE resolving MainWindow, which
    // means the engine is already initialised by the time this ctor runs — so we
    // don't need a second `new AgSTKXApplication()` here. Creating it twice used
    // to roughly double cold-start time.
    private readonly AxAgUiAxVOCntrl _globe3D = new();
    private readonly AxAgUiAx2DCntrl _map2D   = new();

    // Takes the concrete StkScenarioBackend (not IScenarioBackend) so we can reach
    // AgStkObjectRoot for SceneManager access. The interface intentionally hides Root
    // to preserve a future browser-based backend's contract; this view is desktop/COM
    // only, so the dependency on the concrete type is acceptable here.
    private readonly StkScenarioBackend     _stk;
    private readonly IInteractionController _controller;

    // Track last known pixel position so DblClick (which carries no coords) can
    // call PickInfo with the last mouse position.
    private int _lastX3D;
    private int _lastY3D;
    private int _lastX2D;
    private int _lastY2D;

    // Throttle mouse-move callbacks to ~60 fps (16 ms).
    private int _lastMoveTick;

    // Tracks whether we initiated StartObjectEditing so we know to call StopObjectEditing
    // if the controller cancels before STK fires OnObjectEditingStop/Cancel.
    private bool _editingActive;

    // ── On-demand STK COM event subscriptions (perf-critical) ─────────────────
    // Each permanent subscription on AxAgUiAxVOCntrl/AxAgUiAx2DCntrl forces COM
    // marshalling overhead per render — measurable as a 1-2s delay after pan
    // mouse-release. The reference EWTSS_CSP_POC repo (smooth pan) has zero
    // permanent subscriptions on these controls. We subscribe only while the
    // controller is in Placing*/EditingEntity mode and unsubscribe on return
    // to Idle. MouseMoveEvent stays unsubscribed entirely (rubber-band cursor
    // tracking is sacrificed; reference repo behaves the same way).
    private readonly AxAGI.STKX.IAgUiAxVOCntrlEvents_MouseDownEventHandler  _onGlobe3DMouseDown;
    private readonly AxAGI.STKX.IAgUiAxVOCntrlEvents_MouseMoveEventHandler  _onGlobe3DMouseMove;
    private readonly AxAGI.STKX.IAgUiAx2DCntrlEvents_MouseDownEventHandler  _onMap2DMouseDown;
    private readonly AxAGI.STKX.IAgUiAx2DCntrlEvents_MouseMoveEventHandler  _onMap2DMouseMove;

    // STK's DblClick event fires ~12ms after EVERY MouseDown (verified via diag log),
    // not just on actual double-clicks — it's effectively a "click completed"
    // notification. We don't subscribe to it; double-click is detected from
    // MouseDown timing instead (two MDs within SystemInformation.DoubleClickTime
    // and SystemInformation.DoubleClickSize). Reference repo behaves the same way.
    private DateTime _lastClickTime3D = DateTime.MinValue;
    private int      _lastClickX3D, _lastClickY3D;
    private DateTime _lastClickTime2D = DateTime.MinValue;
    private int      _lastClickX2D2, _lastClickY2D2;
    private readonly AxAGI.STKX.IAgUiAxVOCntrlEvents_OnObjectEditingStartEventHandler  _onEditingStart;
    private readonly AxAGI.STKX.IAgUiAxVOCntrlEvents_OnObjectEditingApplyEventHandler  _onEditingApply;
    private readonly AxAGI.STKX.IAgUiAxVOCntrlEvents_OnObjectEditingStopEventHandler   _onEditingStop;
    private readonly AxAGI.STKX.IAgUiAxVOCntrlEvents_OnObjectEditingCancelEventHandler _onEditingCancel;
    // Subscribed during EditingEntity only — bridges user drag-and-release to
    // ScenarioChanged because STK's OnObjectEditingApply only fires from the
    // programmatic ApplyObjectEditing() method, not from user mouse interaction.
    private readonly AxAGI.STKX.IAgUiAxVOCntrlEvents_MouseUpEventHandler _onEditingMouseUp;
    private bool _placementEventsSubscribed;
    private bool _editingEventsSubscribed;

    // ── Placement-preview primitives (live only during PlacingXxx modes) ──────
    private IAgStkGraphicsPolylinePrimitive?   _previewPolyline;
    private IAgStkGraphicsPointBatchPrimitive? _previewPointBatch;
    // Committed vertices accumulated during the current placement gesture.
    private readonly List<(double lat, double lon, double alt)> _placedPoints = new();

    public StkDisplayHost(StkScenarioBackend stk, IInteractionController controller)
    {
        // stk is consumed by App.OnStartup before this constructor runs, so the
        // STK engine Root (AgStkObjectRoot) and the initial scenario are already
        // initialised.  The AgSTKXApplication field above ensures the STKX COM
        // singleton exists in this process so the controls can connect to it.
        _stk = stk;
        _controller = controller;

        // Bind the COM event handler delegates ONCE so we can subscribe and
        // unsubscribe with reference equality. Lambdas captured into typed
        // delegate fields stay subscribable via += / -=.
        _onGlobe3DMouseDown  = (_, e) => _forwardDown3D(e.x, e.y, e.button);
        _onGlobe3DMouseMove  = (_, e) => _forwardMove3D(e.x, e.y);
        _onMap2DMouseDown    = (_, e) => _forwardDown2D(e.x, e.y, e.button);
        _onMap2DMouseMove    = (_, e) => _forwardMove2D(e.x, e.y);
        _onEditingStart      = (_, _) => { _diagLog("[StkDisplayHost] OnObjectEditingStart fired"); _editingActive = true; };
        // OnObjectEditingApply: STK applied the drag in place but kept handles live.
        // RaiseScenarioChanged so the active panel VM re-reads the new state.
        _onEditingApply      = (_, _) => { _diagLog("[StkDisplayHost] OnObjectEditingApply fired"); _stk.RaiseScenarioChanged(); };
        // OnObjectEditingStop: fire ScenarioChanged BEFORE _controller.ApplyEdit
        // transitions out of EditingEntity (which clears EditingPath) — the panel's
        // Refresh handler is gated on EditingPath matching the displayed entity.
        _onEditingStop       = (_, _) =>
        {
            _diagLog("[StkDisplayHost] OnObjectEditingStop fired");
            _stk.RaiseScenarioChanged();
            if (_editingActive) { _editingActive = false; _controller.ApplyEdit(); }
        };
        _onEditingCancel     = (_, _) => { _diagLog("[StkDisplayHost] OnObjectEditingCancel fired"); _editingActive = false; _controller.CancelEdit(); };
        // After every mouse release in editing mode, COMMIT the in-progress edit
        // to STK's COM state via ApplyObjectEditing(). STK's drag handles only
        // update the visual position during a drag — the underlying entity COM
        // state stays at the pre-drag values until something explicitly commits.
        // Without this, GetAircraft after the drag would return the OLD waypoints
        // and Apply would silently push the old values back, undoing the visual
        // drag. ApplyObjectEditing fires OnObjectEditingApply, which our handler
        // already wires to RaiseScenarioChanged — so the Refresh + MarkDirty
        // chain runs as expected.
        _onEditingMouseUp    = (_, e) =>
        {
            _diagLog($"[StkDisplayHost] EditingMouseUp btn={e.button} screen=({e.x},{e.y}) — calling ApplyObjectEditing()");
            try { _globe3D.ApplyObjectEditing(); }
            catch (Exception ex)
            {
                _diagLog($"  ApplyObjectEditing threw: {ex.GetType().Name}: {ex.Message} — falling back to RaiseScenarioChanged");
                _stk.RaiseScenarioChanged();
            }
        };

        InitializeComponent();

        Host3D.Child = _globe3D;
        Host2D.Child = _map2D;

        // Perf pattern #1: force HWND creation NOW so STK's COM events route correctly
        // to the controls when the scenario loads.
        _globe3D.CreateControl();
        _map2D.CreateControl();

        // Perf pattern #3: explicit 2D mouse mode + standard pick mode.
        // eMouseModeAutomatic (0) keeps built-in pan/zoom behavior;
        // AdvancedPickMode = false ensures PickInfo / ScreenXYToLatLon return Lat/Lon
        // (not primitive IDs).
        try
        {
            dynamic ocx2D = ((dynamic)_map2D).GetOcx();
            ocx2D.MouseMode        = 0;
            ocx2D.AdvancedPickMode = false;
        }
        catch { /* property may not exist on every STK build; GetOcx is protected, accessed dynamically */ }

        // No permanent COM event subscriptions (see field-level comment above).
        // _onModeChanged subscribes/unsubscribes the right set per mode transition.
        _controller.ModeChanged += _onModeChanged;

        // No global view-refresh on ScenarioChanged. Earlier we did this on every
        // mutation, but that meant FinalizeAircraft fired 4 synchronous Refresh
        // calls (AddEntity ScenarioChanged → 2 Refresh + UpdateAircraft
        // ScenarioChanged → 2 Refresh) inside one finalize chain, on top of the
        // per-click Render() in _appendPlacedPoint. STK's ActiveX render loop
        // would lock up on heavy click sequences, manifesting as an apparent
        // freeze after a multi-waypoint placement. The reference repo calls
        // Refresh explicitly after specific operations (LoadVDF, etc.), not via
        // subscription — we now do the same: explicit Refresh after
        // StartObjectEditing only (see _onModeChanged), STK auto-renders the
        // rest of the time.

        // Startup ping so the user can confirm the new binary is actually running.
        _diagLog($"StkDisplayHost ctor done. Diag log path: {_diagLogPath}");
    }

    // ─────────────────────────────────────────────────────────────────────────
    //  On-demand STK COM event subscribe/unsubscribe
    // ─────────────────────────────────────────────────────────────────────────

    private void _ensurePlacementEventsSubscribed()
    {
        if (_placementEventsSubscribed) return;
        // MouseMoveEvent during placement only (NOT during idle/pan): drives
        // rubber-band cursor preview between clicks AND keeps STK's COM bridge
        // delivering MouseDownEvent reliably — observed empirically that slow
        // MDs misfire if MouseMove isn't also subscribed.
        // No DblClick subscription: STK fires DblClick on EVERY click (verified
        // via diag log — fires 12ms after every MD), not just real double-clicks.
        // We detect real double-clicks from MD timing in _forwardDown*.
        _globe3D.MouseDownEvent += _onGlobe3DMouseDown;
        _globe3D.MouseMoveEvent += _onGlobe3DMouseMove;
        _map2D.MouseDownEvent   += _onMap2DMouseDown;
        _map2D.MouseMoveEvent   += _onMap2DMouseMove;
        _placementEventsSubscribed = true;
    }

    private void _ensurePlacementEventsUnsubscribed()
    {
        if (!_placementEventsSubscribed) return;
        _globe3D.MouseDownEvent -= _onGlobe3DMouseDown;
        _globe3D.MouseMoveEvent -= _onGlobe3DMouseMove;
        _map2D.MouseDownEvent   -= _onMap2DMouseDown;
        _map2D.MouseMoveEvent   -= _onMap2DMouseMove;
        _placementEventsSubscribed = false;
    }

    private void _ensureEditingEventsSubscribed()
    {
        if (_editingEventsSubscribed) return;
        _globe3D.OnObjectEditingStart  += _onEditingStart;
        _globe3D.OnObjectEditingApply  += _onEditingApply;
        _globe3D.OnObjectEditingStop   += _onEditingStop;
        _globe3D.OnObjectEditingCancel += _onEditingCancel;
        _globe3D.MouseUpEvent          += _onEditingMouseUp;
        _editingEventsSubscribed = true;
    }

    private void _ensureEditingEventsUnsubscribed()
    {
        if (!_editingEventsSubscribed) return;
        _globe3D.OnObjectEditingStart  -= _onEditingStart;
        _globe3D.OnObjectEditingApply  -= _onEditingApply;
        _globe3D.OnObjectEditingStop   -= _onEditingStop;
        _globe3D.OnObjectEditingCancel -= _onEditingCancel;
        _globe3D.MouseUpEvent          -= _onEditingMouseUp;
        _editingEventsSubscribed = false;
    }

    private void _forwardDown3D(int x, int y, short button)
    {
        _lastX3D = x; _lastY3D = y;
        _diagLog($"[StkDisplayHost] 3D MouseDown ENTER screen=({x},{y}) btn={button} — about to call PickInfo");
        var pi = _globe3D.PickInfo(x, y);

        // Right-click (button == 2 per ActiveX convention) finalises the placement
        // gesture — same effect as a double-click, but unambiguous and matches the
        // standard GIS "finish polygon/route" UX.
        // Left-click (button == 1) is a normal waypoint add, with MD-timing
        // double-click detection as a secondary path for users who do rapid DCs.
        var now = DateTime.UtcNow;
        var dx  = x - _lastClickX3D;
        var dy  = y - _lastClickY3D;
        var dcSize = System.Windows.Forms.SystemInformation.DoubleClickSize;
        var isFastDoubleClick =
            (now - _lastClickTime3D).TotalMilliseconds <= System.Windows.Forms.SystemInformation.DoubleClickTime
            && Math.Abs(dx) <= dcSize.Width
            && Math.Abs(dy) <= dcSize.Height;
        _lastClickTime3D = now;
        _lastClickX3D    = x;
        _lastClickY3D    = y;

        var isFinalize = button == 2 || isFastDoubleClick;

        _diagLog(
            $"[StkDisplayHost] 3D MouseDown screen=({x},{y}) btn={button} → lat={pi.Lat:F6}, lon={pi.Lon:F6}, alt={pi.Alt:F1}, valid={pi.IsLatLonAltValid}, mode={_controller.Mode}, finalize={isFinalize}");

        if (isFinalize)
        {
            _controller.OnMapMouseDblClick(pi.Lat, pi.Lon, pi.Alt, pi.IsLatLonAltValid);
            _lastClickTime3D = DateTime.MinValue;  // reset so the next click isn't chain-detected as DC
        }
        else
        {
            _controller.OnMapMouseDown(pi.Lat, pi.Lon, pi.Alt, pi.IsLatLonAltValid);
            if (_isPlacingMode()) _appendPlacedPoint(pi.Lat, pi.Lon, pi.Alt, pi.IsLatLonAltValid);
        }
    }

    private void _forwardMove3D(int x, int y)
    {
        _lastX3D = x; _lastY3D = y;
        // PickInfo is a per-call COM round-trip. Skip it entirely when we're not in
        // a placement mode — pan/zoom fires MouseMove constantly and every PickInfo
        // call makes the globe feel laggy. During placement modes, we still need the
        // lat/lon every frame to update the placement preview.
        if (!_isPlacingMode()) return;
        if (Environment.TickCount - _lastMoveTick < 16) return;
        _lastMoveTick = Environment.TickCount;
        var pi = _globe3D.PickInfo(x, y);
        _controller.OnMapMouseMove(pi.Lat, pi.Lon, pi.Alt, pi.IsLatLonAltValid);
        _updatePlacementPreview(pi.Lat, pi.Lon, pi.Alt, pi.IsLatLonAltValid);
    }

    private void _forwardDblClick3D()
    {
        // DblClick carries no x/y — reuse the last known mouse position.
        var pi = _globe3D.PickInfo(_lastX3D, _lastY3D);
        _diagLog(
            $"[StkDisplayHost] 3D DblClick screen=({_lastX3D},{_lastY3D}) → lat={pi.Lat:F6}, lon={pi.Lon:F6}, valid={pi.IsLatLonAltValid}, mode={_controller.Mode}");
        _controller.OnMapMouseDblClick(pi.Lat, pi.Lon, pi.Alt, pi.IsLatLonAltValid);
        // DblClick finalises aircraft/area-target routes; controller transitions to Idle,
        // so _isPlacingMode() is false here — _disposePlacementPreview handles cleanup.
    }

    // Click-events use PickInfo (carries IsLatLonAltValid) so off-map clicks
    // don't slip through as 0,0. ScreenXYToLatLon stays on the throttled MouseMove
    // path where the per-call cost matters and we tolerate occasional bad coords.
    private void _forwardDown2D(int x, int y, short button)
    {
        _lastX2D = x; _lastY2D = y;
        _diagLog($"[StkDisplayHost] 2D MouseDown ENTER screen=({x},{y}) btn={button} — about to call PickInfo");
        var pi = _map2D.PickInfo(x, y);

        // Same right-click-or-fast-DC finalize path as 3D. See _forwardDown3D.
        var now = DateTime.UtcNow;
        var dx  = x - _lastClickX2D2;
        var dy  = y - _lastClickY2D2;
        var dcSize = System.Windows.Forms.SystemInformation.DoubleClickSize;
        var isFastDoubleClick =
            (now - _lastClickTime2D).TotalMilliseconds <= System.Windows.Forms.SystemInformation.DoubleClickTime
            && Math.Abs(dx) <= dcSize.Width
            && Math.Abs(dy) <= dcSize.Height;
        _lastClickTime2D = now;
        _lastClickX2D2   = x;
        _lastClickY2D2   = y;

        var isFinalize = button == 2 || isFastDoubleClick;

        _diagLog(
            $"[StkDisplayHost] 2D MouseDown screen=({x},{y}) btn={button} → lat={pi.Lat:F6}, lon={pi.Lon:F6}, valid={pi.IsLatLonAltValid}, mode={_controller.Mode}, finalize={isFinalize}");

        if (isFinalize)
        {
            _controller.OnMapMouseDblClick(pi.Lat, pi.Lon, pi.Alt, pi.IsLatLonAltValid);
            _lastClickTime2D = DateTime.MinValue;
        }
        else
        {
            _controller.OnMapMouseDown(pi.Lat, pi.Lon, pi.Alt, pi.IsLatLonAltValid);
            if (_isPlacingMode()) _appendPlacedPoint(pi.Lat, pi.Lon, pi.Alt, pi.IsLatLonAltValid);
        }
    }

    private void _forwardMove2D(int x, int y)
    {
        _lastX2D = x; _lastY2D = y;
        if (!_isPlacingMode()) return;
        if (Environment.TickCount - _lastMoveTick < 16) return;
        _lastMoveTick = Environment.TickCount;
        var (lat, lon, alt, valid) = _screenToLatLon2D(x, y);
        _controller.OnMapMouseMove(lat, lon, alt, valid);
        _updatePlacementPreview(lat, lon, alt, valid);
    }

    private void _forwardDblClick2D()
    {
        var pi = _map2D.PickInfo(_lastX2D, _lastY2D);
        _diagLog(
            $"[StkDisplayHost] 2D DblClick screen=({_lastX2D},{_lastY2D}) → lat={pi.Lat:F6}, lon={pi.Lon:F6}, valid={pi.IsLatLonAltValid}, mode={_controller.Mode}");
        _controller.OnMapMouseDblClick(pi.Lat, pi.Lon, pi.Alt, pi.IsLatLonAltValid);
        // DblClick finalises placement; controller transitions to Idle.
    }

    private (double lat, double lon, double alt, bool valid) _screenToLatLon2D(int x, int y)
    {
        try
        {
            double lat = 0, lon = 0;
            dynamic ocx2D = ((dynamic)_map2D).GetOcx();
            ocx2D.ScreenXYToLatLon(x, y, ref lat, ref lon);
            // ScreenXYToLatLon doesn't expose a validity flag — bounds-check + reject
            // exact (0,0), the canonical "off-map" return for the 2D control. This
            // costs a real Gulf-of-Guinea click but those are rare and the alternative
            // (silently creating entities at lat/lon 0,0) is worse.
            if (lat is < -90 or > 90 || lon is < -180 or > 180 || (lat == 0 && lon == 0))
                return (0, 0, 0, false);
            return (lat, lon, 0.0, true);
        }
        catch
        {
            return (0, 0, 0, false);
        }
    }

    // ─────────────────────────────────────────────────────────────────────────
    //  Placement preview primitives (Step 3)
    // ─────────────────────────────────────────────────────────────────────────

    private void _createPlacementPreview(InteractionMode mode)
    {
        // Defensive: dispose any leftover preview from a prior mode transition.
        _disposePlacementPreview();
        _placedPoints.Clear();

        try
        {
            var manager = _sceneManager();
            if (manager is null)
            {
                _diagLog($"[StkDisplayHost] _createPlacementPreview({mode}): SceneManager is null, no preview created.");
                return;
            }

            // Rubber-band polyline — great-arc interpolation follows Earth curvature.
            var interp = manager.Initializers.GreatArcInterpolator.Initialize();
            _previewPolyline = manager.Initializers.PolylinePrimitive
                .InitializeWithInterpolator((IAgStkGraphicsPositionInterpolator)interp);
            ((IAgStkGraphicsPrimitive)_previewPolyline).Color = System.Drawing.Color.Yellow;
            _previewPolyline.Width = 2;
            manager.Primitives.Add((IAgStkGraphicsPrimitive)_previewPolyline);

            // Point batch — shows each committed vertex.
            _previewPointBatch = manager.Initializers.PointBatchPrimitive.Initialize();
            _previewPointBatch.PixelSize = 6;
            ((IAgStkGraphicsPrimitive)_previewPointBatch).Color = System.Drawing.Color.OrangeRed;
            manager.Primitives.Add((IAgStkGraphicsPrimitive)_previewPointBatch);
            _diagLog($"[StkDisplayHost] _createPlacementPreview({mode}): polyline + pointBatch created OK.");
        }
        catch (Exception ex)
        {
            _diagLog($"[StkDisplayHost] _createPlacementPreview({mode}) THREW: {ex.GetType().Name}: {ex.Message}");
            // Fall back to no-preview — placement still works.
            _previewPolyline    = null;
            _previewPointBatch  = null;
        }
    }

    private void _updatePlacementPreview(double lat, double lon, double alt, bool valid)
    {
        if (_previewPolyline is null && _previewPointBatch is null) return;
        // Don't extend the rubber-band to an off-globe position.
        if (!valid) return;

        try
        {
            // SetCartographic on Graphics primitives expects radians for lat/lon and
            // metres for alt — STK's PolygonDrawing/Form1.cs feeds WindowToCartographic
            // output (radians) directly. PickInfo gives us degrees, so convert here.
            const double DegToRad = Math.PI / 180.0;

            // Build array: all committed points + current cursor as the trailing point.
            var allPts = new object[(_placedPoints.Count + 1) * 3];
            for (var i = 0; i < _placedPoints.Count; i++)
            {
                allPts[i * 3]     = _placedPoints[i].lat * DegToRad;
                allPts[i * 3 + 1] = _placedPoints[i].lon * DegToRad;
                allPts[i * 3 + 2] = _placedPoints[i].alt;
            }
            var last = _placedPoints.Count;
            allPts[last * 3]     = lat * DegToRad;
            allPts[last * 3 + 1] = lon * DegToRad;
            allPts[last * 3 + 2] = alt;

            Array posArray = allPts;

            if (_previewPolyline is not null)
            {
                try { _previewPolyline.SetCartographic("Earth", ref posArray); }
                catch (ArgumentException) { /* geodesic undefined for degenerate input */ }
            }

            // Point batch shows only committed vertices (not the cursor point).
            if (_previewPointBatch is not null && _placedPoints.Count > 0)
            {
                var ptArr = new object[_placedPoints.Count * 3];
                for (var i = 0; i < _placedPoints.Count; i++)
                {
                    ptArr[i * 3]     = _placedPoints[i].lat * DegToRad;
                    ptArr[i * 3 + 1] = _placedPoints[i].lon * DegToRad;
                    ptArr[i * 3 + 2] = _placedPoints[i].alt;
                }
                Array ptArray = ptArr;
                _previewPointBatch.SetCartographic("Earth", ref ptArray);
            }
        }
        catch (Exception ex)
        {
            _diagLog($"[StkDisplayHost] placement preview update failed: {ex.Message}");
        }

        // Force immediate redraw so the rubber-band tracks the cursor without
        // waiting for STK's animation-timer frame — PolygonDrawing/Form1.cs:208.
        _renderScene();
    }

    private void _disposePlacementPreview()
    {
        try
        {
            var manager = _sceneManager();
            if (manager is not null)
            {
                if (_previewPolyline is not null)
                    manager.Primitives.Remove((IAgStkGraphicsPrimitive)_previewPolyline);
                if (_previewPointBatch is not null)
                    manager.Primitives.Remove((IAgStkGraphicsPrimitive)_previewPointBatch);
            }
        }
        catch (Exception ex)
        {
            _diagLog($"[StkDisplayHost] placement preview dispose failed: {ex.Message}");
        }
        finally
        {
            _previewPolyline   = null;
            _previewPointBatch = null;
            _placedPoints.Clear();
        }

        // Force immediate redraw so removed primitives disappear without waiting for
        // the next animation-timer frame.
        _renderScene();
    }

    /// <summary>
    /// Called by mouse-down and dbl-click forwarders while a placement mode is active.
    /// Adds the click position to <see cref="_placedPoints"/> and refreshes the vertex
    /// point-batch so committed points are visible immediately.
    /// </summary>
    private void _appendPlacedPoint(double lat, double lon, double alt, bool valid)
    {
        if (!valid || _previewPointBatch is null) return;
        _placedPoints.Add((lat, lon, alt));

        try
        {
            const double DegToRad = Math.PI / 180.0;
            var ptArr = new object[_placedPoints.Count * 3];
            for (var i = 0; i < _placedPoints.Count; i++)
            {
                ptArr[i * 3]     = _placedPoints[i].lat * DegToRad;
                ptArr[i * 3 + 1] = _placedPoints[i].lon * DegToRad;
                ptArr[i * 3 + 2] = _placedPoints[i].alt;
            }
            Array ptArray = ptArr;
            _previewPointBatch.SetCartographic("Earth", ref ptArray);
        }
        catch (Exception ex)
        {
            _diagLog($"[StkDisplayHost] _appendPlacedPoint failed: {ex.Message}");
        }

        // Immediate redraw so the committed vertex dot appears without delay.
        _renderScene();
    }

    /// <summary>
    /// Returns the STK scene manager for the current scenario, or null if unavailable.
    /// Accesses <see cref="StkScenarioBackend.Root"/> (internal) to reach AgStkObjectRoot.
    /// </summary>
    private IAgStkGraphicsSceneManager? _sceneManager()
    {
        var scenario = _stk.Root.CurrentScenario as IAgScenario;
        return scenario?.SceneManager;
    }

    /// <summary>
    /// Forces an immediate redraw of the 3-D scene after primitive updates.
    /// Mirrors <c>manager.Scenes[0].Render()</c> from PolygonDrawing/Form1.cs:165,209,274
    /// which the sample calls after every SetCartographic to keep the rubber-band
    /// preview smooth rather than waiting for the animation-timer frame.
    /// </summary>
    private void _renderScene()
    {
        try
        {
            var manager = _sceneManager();
            if (manager is not null && manager.Scenes.Count > 0)
                manager.Scenes[0].Render();
        }
        catch (Exception ex)
        {
            _diagLog($"[StkDisplayHost] Render failed: {ex.Message}");
        }
    }

    // ─────────────────────────────────────────────────────────────────────────
    //  StartObjectEditing bridge (Step 4)
    // ─────────────────────────────────────────────────────────────────────────

    private void _onModeChanged(object? sender, EventArgs e)
    {
        // Lifecycle of editing events per 3DObjectEditing/Form1.cs:
        //   StartObjectEditing(path)   → OnObjectEditingStart  (drag handles appear)
        //   ApplyObjectEditing()       → OnObjectEditingApply  (apply, keep handles live)
        //   StopObjectEditing(false)   → OnObjectEditingStop   (OK/Accept — committed)
        //   StopObjectEditing(true)    → OnObjectEditingCancel (Cancel — reverted)
        var mode = _controller.Mode;
        _diagLog($"[StkDisplayHost] ModeChanged → {mode} (editingPath={_controller.EditingPath ?? "null"})");

        // Placement subscriptions + preview primitives lifecycle.
        if (mode is InteractionMode.PlacingFacility
                 or InteractionMode.PlacingAircraft
                 or InteractionMode.PlacingAreaTarget)
        {
            _ensurePlacementEventsSubscribed();
            _createPlacementPreview(mode);
        }
        else
        {
            _ensurePlacementEventsUnsubscribed();
            _disposePlacementPreview();
        }

        // Editing subscriptions + StartObjectEditing lifecycle. Subscribe BEFORE
        // calling StartObjectEditing so OnObjectEditingStart fires to our handler.
        if (mode == InteractionMode.EditingEntity && _controller.EditingPath is string mvpPath)
        {
            _ensureEditingEventsSubscribed();
            var node = _stk.FindByPath(mvpPath);
            if (node is null)
            {
                _diagLog(
                    $"[StkDisplayHost] EditingEntity requested for '{mvpPath}' but FindByPath returned null.");
            }
            else
            {
                var stkPath = _buildStkPath(mvpPath, node.Kind);
                try
                {
                    _globe3D.StartObjectEditing(stkPath);
                    // Force a redraw so the drag handles actually paint — without
                    // this, STK has the handles internally but the WindowsFormsHost
                    // doesn't repaint until the next pan/zoom and the user can't
                    // see them to grab.
                    try { _globe3D.Refresh(); _map2D.Refresh(); } catch { }
                    _diagLog($"[StkDisplayHost] StartObjectEditing('{stkPath}') OK + Refresh.");
                }
                catch (Exception ex)
                {
                    _diagLog(
                        $"[StkDisplayHost] StartObjectEditing('{stkPath}') threw: {ex.GetType().Name}: {ex.Message}");
                }
            }
        }
        else
        {
            // Mode left EditingEntity. If STK still has drag handles active (app-initiated
            // cancel — Esc in the UI before STK fired Stop/Cancel), revert via
            // StopObjectEditing(true). That fires OnObjectEditingCancel synchronously
            // through our still-subscribed handler, which clears _editingActive.
            if (_editingActive)
            {
                _editingActive = false;
                try { _globe3D.StopObjectEditing(true); } catch { /* STK may have already stopped */ }
            }
            // Now safe to unsubscribe — no further editing-event traffic expected.
            _ensureEditingEventsUnsubscribed();
        }
    }

    /// <summary>
    /// Converts an MVP4 entity path (e.g. "Aircraft1") to a full STK registry path
    /// (e.g. "/Application/STK/Scenario/Untitled/Aircraft/Aircraft1") for use with
    /// StartObjectEditing. Mirrors Form1.cs from the 3DObjectEditing reference sample,
    /// which uses full paths rather than the "*/" wildcard shorthand.
    /// </summary>
    private string _buildStkPath(string mvpPath, EntityKind kind)
    {
        var className = kind switch
        {
            EntityKind.Aircraft   => "Aircraft",
            EntityKind.Facility   => "Facility",
            EntityKind.AreaTarget => "AreaTarget",
            _ => throw new NotSupportedException($"Edit on map not supported for {kind}.")
        };
        var scenarioName = _stk.ScenarioName ?? "Untitled";
        return $"/Application/STK/Scenario/{scenarioName}/{className}/{mvpPath}";
    }

    private bool _isPlacingMode() =>
        _controller.Mode is InteractionMode.PlacingFacility
                         or InteractionMode.PlacingAircraft
                         or InteractionMode.PlacingAreaTarget;

    // Diagnostic logging gated by the MVP4_DIAG environment variable.
    // Set MVP4_DIAG=1 (or true) before launching the app to enable verbose logs of
    // mouse events, mode transitions, and editing-event firings. Writes to
    // Desktop\stk-debug.log so users can share without hunting for log paths.
    // Default off — zero overhead when not enabled.
    private static readonly bool   _diagEnabled =
        string.Equals(Environment.GetEnvironmentVariable("MVP4_DIAG"), "1", StringComparison.OrdinalIgnoreCase) ||
        string.Equals(Environment.GetEnvironmentVariable("MVP4_DIAG"), "true", StringComparison.OrdinalIgnoreCase);
    private static readonly object _diagLogLock = new();
    private static readonly string _diagLogPath = System.IO.Path.Combine(
        Environment.GetFolderPath(Environment.SpecialFolder.Desktop),
        "stk-debug.log");
    private static void _diagLog(string message)
    {
        if (!_diagEnabled) return;
        System.Diagnostics.Debug.WriteLine(message);
        try
        {
            lock (_diagLogLock)
            {
                System.IO.File.AppendAllText(_diagLogPath,
                    $"{DateTime.Now:HH:mm:ss.fff} {message}{Environment.NewLine}");
            }
        }
        catch { /* swallow — logging must never break the app */ }
    }
}
