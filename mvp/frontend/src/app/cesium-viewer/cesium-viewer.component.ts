import {
  AfterViewInit,
  ChangeDetectionStrategy,
  ChangeDetectorRef,
  Component,
  NgZone,
  OnDestroy,
} from '@angular/core';
import {
  Viewer,
  ImageryLayer,
  OpenStreetMapImageryProvider,
  EllipsoidTerrainProvider,
  ArcGISTiledElevationTerrainProvider,
  ArcGisMapServerImageryProvider,
  CzmlDataSource,
  ClockRange,
  ClockStep,
  JulianDate,
} from 'cesium';

type ImageryMode = 'street' | 'satellite';
type TerrainMode  = 'flat'   | '3d';

const CANVAS_W = 1000;
const CANVAS_H = 500;

@Component({
  selector: 'app-cesium-viewer',
  templateUrl: './cesium-viewer.component.html',
  styleUrls: ['./cesium-viewer.component.scss'],
  changeDetection: ChangeDetectionStrategy.OnPush,
})
export class CesiumViewerComponent implements AfterViewInit, OnDestroy {
  private viewer!: Viewer;
  private czmlListener!: EventListener;
  // eslint-disable-next-line @typescript-eslint/no-explicit-any
  private _tickUnsubscribe: (() => void) | null = null;
  private _lastDisplayedSec = -1;

  imageryMode: ImageryMode = 'street';
  terrainMode: TerrainMode  = 'flat';

  // Timeline scrubber state (bound in template)
  scrubberValue = 0;
  scrubberMax   = 3600;
  simTimeLabel  = '00:00:00';
  isPlaying     = false;

  constructor(private ngZone: NgZone, private cdr: ChangeDetectorRef) {}

  private _sizeContainer(): void {
    const container = document.getElementById('cesiumContainer');
    if (!container) return;
    container.style.width  = `${CANVAS_W}px`;
    container.style.height = `${CANVAS_H}px`;
  }

  ngAfterViewInit(): void {
    this.ngZone.runOutsideAngular(() => {
      requestAnimationFrame(() => {
        this._sizeContainer();

        const hiddenCredits = document.createElement('div');
        this.viewer = new Viewer('cesiumContainer', {
          animation:            false,
          timeline:             false,   // replaced by custom scrubber
          baseLayerPicker:      false,
          geocoder:             false,
          homeButton:           true,
          sceneModePicker:      true,
          navigationHelpButton: false,
          fullscreenButton:     false,
          infoBox:              false,
          selectionIndicator:   false,
          creditContainer:      hiddenCredits,
          baseLayer: new ImageryLayer(
            new OpenStreetMapImageryProvider({ url: 'https://tile.openstreetmap.org/' })
          ),
          terrainProvider: new EllipsoidTerrainProvider(),
        });

        this.viewer.clock.multiplier    = 30;
        this.viewer.clock.clockStep     = ClockStep.SYSTEM_CLOCK_MULTIPLIER;
        this.viewer.clock.shouldAnimate = false;

        this.czmlListener = (e: Event) => {
          const url = (e as CustomEvent<{ url: string }>).detail.url;
          this.loadCzml(url).catch(err => {
            console.error('CZML load error:', err);
            window.dispatchEvent(new CustomEvent('czml-error', { detail: { message: String(err) } }));
          });
        };
        window.addEventListener('load-czml', this.czmlListener);
      });
    });
  }

  // ── Map / terrain controls ───────────────────────────────────────────────

  setImagery(mode: ImageryMode): void {
    if (!this.viewer) return;
    this.imageryMode = mode;
    this.cdr.markForCheck();
    this.ngZone.runOutsideAngular(async () => {
      this.viewer.imageryLayers.removeAll();
      if (mode === 'street') {
        this.viewer.imageryLayers.add(
          new ImageryLayer(
            new OpenStreetMapImageryProvider({ url: 'https://tile.openstreetmap.org/' })
          )
        );
      } else {
        const provider = await ArcGisMapServerImageryProvider.fromUrl(
          'https://server.arcgisonline.com/ArcGIS/rest/services/World_Imagery/MapServer'
        );
        this.viewer.imageryLayers.add(new ImageryLayer(provider));
      }
    });
  }

  setTerrain(mode: TerrainMode): void {
    if (!this.viewer) return;
    this.terrainMode = mode;
    this.cdr.markForCheck();
    this.ngZone.runOutsideAngular(async () => {
      if (mode === 'flat') {
        this.viewer.terrainProvider = new EllipsoidTerrainProvider();
      } else {
        try {
          const tp = await ArcGISTiledElevationTerrainProvider.fromUrl(
            'https://elevation3d.arcgis.com/arcgis/rest/services/WorldElevation3D/Terrain3D/ImageServer'
          );
          this.viewer.terrainProvider = tp;
        } catch {
          this.terrainMode = 'flat';
          this.cdr.markForCheck();
        }
      }
    });
  }

  // ── Playback controls ────────────────────────────────────────────────────

  async loadCzml(url: string): Promise<void> {
    return this.ngZone.runOutsideAngular(async () => {
      this.viewer.dataSources.removeAll();

      // Remove previous tick listener before loading new CZML
      if (this._tickUnsubscribe) {
        this._tickUnsubscribe();
        this._tickUnsubscribe = null;
      }

      const ds = await CzmlDataSource.load(url);
      await this.viewer.dataSources.add(ds);

      if (ds.clock) {
        this.viewer.clock.startTime   = ds.clock.startTime.clone();
        this.viewer.clock.stopTime    = ds.clock.stopTime.clone();
        this.viewer.clock.currentTime = ds.clock.startTime.clone();
        this.viewer.clock.clockRange  = ClockRange.LOOP_STOP;
        this.viewer.clock.multiplier  = ds.clock.multiplier ?? 30;
        this.viewer.clock.clockStep   = ClockStep.SYSTEM_CLOCK_MULTIPLIER;

        const totalSec = JulianDate.secondsDifference(
          ds.clock.stopTime, ds.clock.startTime
        );
        this.scrubberMax        = Math.round(totalSec);
        this.scrubberValue      = 0;
        this._lastDisplayedSec  = -1;

        // Sync scrubber + play state on every clock tick (runs outside Angular zone)
        const tickFn = (clock: { currentTime: JulianDate; startTime: JulianDate; shouldAnimate: boolean }) => {
          const elapsed   = JulianDate.secondsDifference(clock.currentTime, clock.startTime);
          const sec       = Math.max(0, Math.min(Math.round(elapsed), this.scrubberMax));
          const playing   = clock.shouldAnimate;
          const needsSync = sec !== this._lastDisplayedSec || playing !== this.isPlaying;

          if (needsSync) {
            this._lastDisplayedSec = sec;
            this.scrubberValue = sec;
            this.isPlaying     = playing;
            const h = Math.floor(sec / 3600);
            const m = Math.floor((sec % 3600) / 60);
            const s = sec % 60;
            this.simTimeLabel =
              `${String(h).padStart(2, '0')}:${String(m).padStart(2, '0')}:${String(s).padStart(2, '0')}`;
            this.cdr.markForCheck();
          }
        };

        this.viewer.clock.onTick.addEventListener(tickFn);
        this._tickUnsubscribe = () => this.viewer.clock.onTick.removeEventListener(tickFn);
      }

      await this.viewer.zoomTo(ds);
      this.viewer.clock.shouldAnimate = true;
      this.isPlaying = true;
      this.cdr.markForCheck();
    });
  }

  onScrub(event: Event): void {
    if (!this.viewer) return;
    const sec = Number((event.target as HTMLInputElement).value);
    this.ngZone.runOutsideAngular(() => {
      this.viewer.clock.currentTime = JulianDate.addSeconds(
        this.viewer.clock.startTime, sec, new JulianDate()
      );
    });
  }

  togglePause(): void {
    if (!this.viewer) return;
    this.ngZone.runOutsideAngular(() => {
      const clock = this.viewer.clock;
      if (JulianDate.compare(clock.currentTime, clock.stopTime) >= 0) {
        clock.currentTime = clock.startTime.clone();
      }
      clock.shouldAnimate = !clock.shouldAnimate;
      this.isPlaying = clock.shouldAnimate;
      this.cdr.markForCheck();
    });
  }

  stepBack(): void {
    if (!this.viewer) return;
    this.ngZone.runOutsideAngular(() => {
      this.viewer.clock.shouldAnimate = false;
      this.isPlaying = false;
      this.cdr.markForCheck();
      const cur = JulianDate.clone(this.viewer.clock.currentTime);
      this.viewer.clock.currentTime = JulianDate.addSeconds(cur, -60, cur);
    });
  }

  stepForward(): void {
    if (!this.viewer) return;
    this.ngZone.runOutsideAngular(() => {
      this.viewer.clock.shouldAnimate = false;
      this.isPlaying = false;
      this.cdr.markForCheck();
      const cur = JulianDate.clone(this.viewer.clock.currentTime);
      this.viewer.clock.currentTime = JulianDate.addSeconds(cur, 60, cur);
    });
  }

  setSpeed(multiplier: number): void {
    if (!this.viewer) return;
    this.ngZone.runOutsideAngular(() => {
      this.viewer.clock.multiplier = multiplier;
    });
  }

  rewind(): void {
    if (!this.viewer) return;
    this.ngZone.runOutsideAngular(() => {
      this.viewer.clock.currentTime   = JulianDate.clone(this.viewer.clock.startTime);
      this.viewer.clock.shouldAnimate = true;
    });
  }

  ngOnDestroy(): void {
    this.ngZone.runOutsideAngular(() => {
      if (this._tickUnsubscribe) this._tickUnsubscribe();
      window.removeEventListener('load-czml', this.czmlListener);
      if (this.viewer && !this.viewer.isDestroyed()) {
        this.viewer.destroy();
      }
    });
  }
}
