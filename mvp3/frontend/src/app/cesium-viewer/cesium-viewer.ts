import {
  AfterViewInit, Component, ElementRef, Input, OnChanges, OnDestroy,
  SimpleChanges, inject,
} from '@angular/core';
import {
  Ion, ArcGisMapServerImageryProvider, ImageryLayer,
  EllipsoidTerrainProvider, Cartesian3, CzmlDataSource,
} from '@cesium/engine';
// ContextLimits is exported from @cesium/engine's index.js but not the
// shipped .d.ts. Cast via namespace import so we can monkey-patch GL caps
// this ANGLE/D3D11 driver mis-reports.
import * as CesiumEngine from '@cesium/engine';
const ContextLimits = (CesiumEngine as any).ContextLimits as Record<string, any>;
import { Viewer } from '@cesium/widgets';
// initializeSensors is exported at runtime from index.js but not declared
// in the shipped .d.ts. Cast through any to satisfy tsc.
import * as IonSensors from '@cesiumgs/ion-sdk-sensors';
const initializeSensors = (IonSensors as any).initializeSensors as (viewer: unknown) => void;

// Measurement widget (distance / area / height / horizontal / vertical).
// viewerMeasureMixin extends a Viewer with a toolbar + click-to-use modes.
import * as IonMeasurements from '@cesiumgs/ion-sdk-measurements';
const viewerMeasureMixin = (IonMeasurements as any).viewerMeasureMixin as
  (viewer: unknown, options?: unknown) => void;

@Component({
  selector: 'app-cesium-viewer',
  standalone: true,
  templateUrl: './cesium-viewer.html',
  styleUrl: './cesium-viewer.scss',
})
export class CesiumViewer implements AfterViewInit, OnDestroy, OnChanges {
  @Input() czmlUrl: string | null = null;

  private viewer!: Viewer;
  private resizeObserver: ResizeObserver | null = null;
  private creditContainer: HTMLElement | null = null;
  private host: ElementRef<HTMLElement> = inject(ElementRef);

  ngAfterViewInit(): void {
    // Defer one frame so the parent flex cell has a final size before Cesium
    // reads offsetWidth/offsetHeight.
    requestAnimationFrame(() => this._init());
  }

  private _init(): void {
    // No Ion cloud: silence the default-token warning and keep all assets local.
    Ion.defaultAccessToken = '';

    this.creditContainer = document.createElement('div');
    this.creditContainer.style.display = 'none';
    document.body.appendChild(this.creditContainer);

    const container = this.host.nativeElement.querySelector('#cesiumContainer') as HTMLElement;

    this.viewer = new Viewer(container, {
      animation:        true,
      timeline:         true,
      baseLayerPicker:  false,
      geocoder:         false,
      homeButton:       false,
      sceneModePicker:  false,
      fullscreenButton: false,
      navigationHelpButton:                   false,
      navigationInstructionsInitiallyVisible: false,
      infoBox:            false,
      selectionIndicator: false,
      terrainProvider: new EllipsoidTerrainProvider(),
      // Skip default baseLayer; add imagery manually below once the WebGL
      // context has a sized drawing buffer. Passing baseLayer here can race
      // with the first texture upload on ANGLE/D3D11 and yield
      // "Width must be less than or equal to the maximum texture size (0)".
      baseLayer: false as unknown as ImageryLayer,
      creditContainer: this.creditContainer,
      contextOptions: {
        webgl: {
          alpha:                        false,
          preserveDrawingBuffer:        false,
          failIfMajorPerformanceCaveat: false,
        },
      },
    });

    // --- ANGLE/D3D11 driver quirk workaround -----------------------------
    // On some AMD Radeon drivers (observed on 890M via ANGLE D3D11), several
    // GL caps captured by Cesium's Context constructor read as zero even
    // though the real WebGL context reports sane values when queried fresh.
    // In Cesium v22 these caps live on the static `ContextLimits` singleton
    // (not the Context instance). If any field is still 0 or missing after
    // viewer construction, force it to a conservative default so validation
    // in RenderState.fromCache() and the Texture constructor passes.
    if (ContextLimits) {
      const defaults: Record<string, number | boolean> = {
        _maximumCombinedTextureImageUnits: 8,
        _maximumCubeMapSize:               4096,
        _maximumFragmentUniformVectors:    224,
        _maximumTextureImageUnits:         8,
        _maximumRenderbufferSize:          4096,
        _maximumTextureSize:               4096,
        _maximum3DTextureSize:             256,
        _maximumVaryingVectors:            8,
        _maximumVertexAttributes:          8,
        _maximumVertexTextureImageUnits:   4,
        _maximumVertexUniformVectors:      128,
        _minimumAliasedLineWidth:          1,
        _maximumAliasedLineWidth:          1,
        _minimumAliasedPointSize:          1,
        _maximumAliasedPointSize:          64,
        _maximumViewportWidth:             4096,
        _maximumViewportHeight:            4096,
        _maximumTextureFilterAnisotropy:   2,
        _maximumDrawBuffers:               1,
        _maximumColorAttachments:          1,
        _maximumSamples:                   0,
      };
      for (const [key, fallback] of Object.entries(defaults)) {
        const current = ContextLimits[key];
        if (typeof current !== 'number' || current < (typeof fallback === 'number' ? 1 : 0)) {
          ContextLimits[key] = fallback;
        }
      }
    }

    // Add imagery after the viewer is live so the GL context is fully
    // initialised before the first tile tries to upload.
    ArcGisMapServerImageryProvider.fromUrl(
      'https://services.arcgisonline.com/ArcGIS/rest/services/World_Imagery/MapServer',
    ).then(p => {
      this.viewer.imageryLayers.addImageryProvider(p);
    });

    // Register Ion SDK sensor visualizers on this viewer — enables native
    // rendering of agi_conicSensor / agi_rectangularSensor / agi_customPatternSensor
    // CZML packets via CzmlDataSource's built-in processors.
    initializeSensors(this.viewer);

    // Add the Ion SDK measurement toolbar (distance, area, height, horizontal,
    // vertical, point). Appears as an icon button in the viewer's top-right.
    viewerMeasureMixin(this.viewer, {
      units: (IonMeasurements as any).MeasureUnits?.METERS,
    });

    this.viewer.camera.flyTo({
      destination: Cartesian3.fromDegrees(74.5, 34.2, 500_000),
    });

    this.resizeObserver = new ResizeObserver(() => this.viewer.resize());
    this.resizeObserver.observe(container);

    if (this.czmlUrl) this._loadCzml(this.czmlUrl);
  }

  ngOnChanges(changes: SimpleChanges): void {
    if (changes['czmlUrl'] && this.czmlUrl && this.viewer) {
      this._loadCzml(this.czmlUrl);
    }
  }

  ngOnDestroy(): void {
    this.resizeObserver?.disconnect();
    this.viewer?.destroy();
    this.creditContainer?.remove();
  }

  private _loadCzml(url: string): void {
    const src = new CzmlDataSource();
    src.load(url).then(ds => {
      this.viewer.dataSources.add(ds);
      this.viewer.zoomTo(ds);
    });
  }
}
