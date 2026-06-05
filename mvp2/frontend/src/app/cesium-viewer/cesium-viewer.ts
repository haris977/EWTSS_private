import {
  Component, OnDestroy, AfterViewInit, Input, OnChanges, SimpleChanges,
  ElementRef, effect, inject,
} from '@angular/core';
import { DrawingStateService } from '../drawing-state.service';
import { ViewerService } from '../viewer.service';
import { DrawingMode, PlanningEntity } from '../types';
import * as Cesium from 'cesium';

@Component({
  selector: 'app-cesium-viewer',
  standalone: true,
  templateUrl: './cesium-viewer.html',
  styleUrl: './cesium-viewer.scss',
})
export class CesiumViewer implements AfterViewInit, OnDestroy, OnChanges {
  @Input() czmlUrl: string | null = null;

  private viewer!: Cesium.Viewer;
  private handler: Cesium.ScreenSpaceEventHandler | null = null;
  private tempPolyline: Cesium.Entity | null = null;
  private modePoller: any;
  private creditContainer: HTMLElement | null = null;
  private resizeObserver: ResizeObserver | null = null;
  private viewerSvc = inject(ViewerService);
  private host: ElementRef<HTMLElement> = inject(ElementRef);

  private entityMap = new Map<string, Cesium.Entity>();

  constructor(private drawState: DrawingStateService) {
    // Reactive render: whenever the entities signal changes, reconcile the
    // Cesium entity collection. Fires once on subscribe (viewer may be null
    // — guarded inside _syncEntities). Fires again after each commit/edit.
    effect(() => {
      const list  = this.drawState.entities();
      const picked = this.drawState.selectedName();
      if (this.viewer) this._syncEntities(list, picked);
    });
  }

  ngAfterViewInit(): void {
    // Defer one frame so the grid cell has finished its layout pass. Cesium
    // reads container.offsetWidth/Height at construction and won't grow later
    // unless we also attach a ResizeObserver (below).
    requestAnimationFrame(() => this._initViewer());
  }

  private _initViewer(): void {
    Cesium.Ion.defaultAccessToken = '';

    this.creditContainer = document.createElement('div');
    this.creditContainer.style.display = 'none';
    document.body.appendChild(this.creditContainer);

    const container = this.host.nativeElement.querySelector('#cesiumContainer') as HTMLElement;

    this.viewer = new Cesium.Viewer(container, {
      animation:        false,
      timeline:         false,
      baseLayerPicker:  false,
      geocoder:         false,
      homeButton:       false,
      sceneModePicker:  false,
      fullscreenButton: false,
      navigationHelpButton:                   false,
      navigationInstructionsInitiallyVisible: false,
      infoBox:            false,
      selectionIndicator: false,
      terrainProvider: new Cesium.EllipsoidTerrainProvider(),
      baseLayer: Cesium.ImageryLayer.fromProviderAsync(
        Cesium.ArcGisMapServerImageryProvider.fromUrl(
          'https://services.arcgisonline.com/ArcGIS/rest/services/World_Imagery/MapServer',
        ),
        {},
      ),
      creditContainer: this.creditContainer,
    });
    this.viewer.camera.flyTo({
      destination: Cesium.Cartesian3.fromDegrees(74.5, 34.2, 500_000),
    });

    this.resizeObserver = new ResizeObserver(() => this.viewer.resize());
    this.resizeObserver.observe(container);

    this.viewerSvc.viewer.set(this.viewer);

    // Seed the current state into the viewer now that it's ready.
    this._syncEntities(this.drawState.entities(), this.drawState.selectedName());

    let lastMode: DrawingMode = 'none';
    this.modePoller = setInterval(() => {
      const m = this.drawState.mode();
      if (m !== lastMode) { lastMode = m; this._attachHandler(m); }
    }, 100);
  }

  ngOnChanges(changes: SimpleChanges): void {
    if (changes['czmlUrl'] && this.czmlUrl && this.viewer) {
      this._loadCzml(this.czmlUrl);
    }
  }

  ngOnDestroy(): void {
    if (this.modePoller) clearInterval(this.modePoller);
    this.resizeObserver?.disconnect();
    this.handler?.destroy();
    this.viewerSvc.viewer.set(null);
    this.viewer?.destroy();
    this.creditContainer?.remove();
  }

  // ------- State → Cesium reconciliation -------

  private _syncEntities(list: PlanningEntity[], selected: string | null): void {
    const names = new Set(list.map(e => e.properties.name));

    // Remove entities no longer in state.
    for (const [name, ent] of Array.from(this.entityMap.entries())) {
      if (!names.has(name)) {
        this.viewer.entities.remove(ent);
        this.entityMap.delete(name);
      }
    }

    // Add or rebuild every in-state entity. Rebuilding is simpler than
    // mutating individual property bags when geometry changes.
    for (const e of list) {
      const existing = this.entityMap.get(e.properties.name);
      if (existing) this.viewer.entities.remove(existing);
      const cesEnt = this._createCesiumEntity(e, e.properties.name === selected);
      if (cesEnt) this.entityMap.set(e.properties.name, cesEnt);
    }
  }

  private _createCesiumEntity(e: PlanningEntity, isSelected: boolean): Cesium.Entity | null {
    const name = e.properties.name;
    const accent = Cesium.Color.fromCssColorString('#4a7ddb');

    if (e.properties.entityType === 'AreaTarget' && e.geometry.type === 'Polygon') {
      const ring = e.geometry.coordinates[0] as [number, number][];
      const flat = ring.flatMap(c => [c[0], c[1]]);
      return this.viewer.entities.add({
        name,
        polygon: {
          hierarchy: new Cesium.PolygonHierarchy(Cesium.Cartesian3.fromDegreesArray(flat)),
          material:  isSelected ? accent.withAlpha(0.35) : Cesium.Color.ORANGE.withAlpha(0.3),
          outline:   true,
          outlineColor: isSelected ? accent : Cesium.Color.ORANGE,
        },
      });
    }

    if (e.properties.entityType === 'Aircraft' && e.geometry.type === 'LineString') {
      const positions = Cesium.Cartesian3.fromDegreesArrayHeights(e.geometry.coordinates.flat());
      return this.viewer.entities.add({
        name,
        polyline: { positions, width: isSelected ? 5 : 3,
                    material: isSelected ? accent : Cesium.Color.CYAN },
      });
    }

    if (e.properties.entityType === 'Facility' && e.geometry.type === 'Point') {
      const [lon, lat, alt = 0] = e.geometry.coordinates;
      return this.viewer.entities.add({
        name,
        position: Cesium.Cartesian3.fromDegrees(lon, lat, alt),
        point: { pixelSize: isSelected ? 16 : 12,
                 color: isSelected ? accent : Cesium.Color.YELLOW,
                 outlineColor: Cesium.Color.BLACK, outlineWidth: 1 },
      });
    }

    // Sensors have no standalone geometry — they attach to a parent.
    return null;
  }

  // ------- Drawing handlers (canvas input only) -------

  private _attachHandler(mode: DrawingMode): void {
    this.handler?.destroy();
    this.handler = null;
    if (this.tempPolyline) {
      this.viewer.entities.remove(this.tempPolyline);
      this.tempPolyline = null;
    }
    if (mode === 'none') return;

    this.handler = new Cesium.ScreenSpaceEventHandler(this.viewer.scene.canvas);

    if (mode === 'Facility') {
      this.handler.setInputAction((e: Cesium.ScreenSpaceEventHandler.PositionedEvent) => {
        const pos = this._screenToLonLat(e.position);
        if (!pos) return;
        this.drawState.commitEntity({
          type: 'Feature',
          geometry: { type: 'Point', coordinates: [pos.lon, pos.lat, 0] },
          properties: { entityType: 'Facility', name: `Facility_${Date.now()}` },
        });
      }, Cesium.ScreenSpaceEventType.LEFT_CLICK);
      return;
    }

    if (mode === 'Aircraft' || mode === 'AreaTarget') {
      this.handler.setInputAction((e: Cesium.ScreenSpaceEventHandler.PositionedEvent) => {
        const pos = this._screenToLonLat(e.position);
        if (!pos) return;
        this.drawState.addCoord(pos.lon, pos.lat, 0);
        this._refreshTempPolyline();
      }, Cesium.ScreenSpaceEventType.LEFT_CLICK);

      this.handler.setInputAction(() => {
        const coords = this.drawState.inProgressCoords();
        if (coords.length < 2) return;
        const name = `${mode}_${Date.now()}`;
        if (mode === 'Aircraft') {
          this.drawState.commitEntity({
            type: 'Feature',
            geometry: { type: 'LineString', coordinates: coords },
            properties: { entityType: 'Aircraft', name, speedMs: 150 },
          });
        } else {
          const ring: [number, number][] = [
            ...coords.map(c => [c[0], c[1]] as [number, number]),
            [coords[0][0], coords[0][1]],
          ];
          this.drawState.commitEntity({
            type: 'Feature',
            geometry: { type: 'Polygon', coordinates: [ring] },
            properties: { entityType: 'AreaTarget', name },
          });
        }
      }, Cesium.ScreenSpaceEventType.LEFT_DOUBLE_CLICK);
    }
  }

  private _refreshTempPolyline(): void {
    const coords = this.drawState.inProgressCoords();
    if (this.tempPolyline) this.viewer.entities.remove(this.tempPolyline);
    if (coords.length < 2) return;
    this.tempPolyline = this.viewer.entities.add({
      polyline: {
        positions: Cesium.Cartesian3.fromDegreesArray(coords.flatMap(c => [c[0], c[1]])),
        width: 2, material: Cesium.Color.WHITE.withAlpha(0.6),
      },
    });
  }

  private _screenToLonLat(pos: Cesium.Cartesian2): { lon: number; lat: number } | null {
    const cart = this.viewer.camera.pickEllipsoid(pos, this.viewer.scene.globe.ellipsoid);
    if (!cart) return null;
    const carto = Cesium.Cartographic.fromCartesian(cart);
    return { lon: Cesium.Math.toDegrees(carto.longitude),
             lat: Cesium.Math.toDegrees(carto.latitude) };
  }

  private _loadCzml(url: string): void {
    const source = new Cesium.CzmlDataSource();
    source.load(url).then(ds => {
      this.viewer.dataSources.add(ds);
      this.viewer.zoomTo(ds);
    });
  }
}
