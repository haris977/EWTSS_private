import { Component, inject, signal } from '@angular/core';
import * as Cesium from 'cesium';
import { ViewerService } from '../viewer.service';

@Component({
  selector: 'app-canvas-controls',
  standalone: true,
  templateUrl: './canvas-controls.html',
  styleUrl: './canvas-controls.scss',
})
export class CanvasControls {
  private viewerSvc = inject(ViewerService);
  readonly is3D = signal(true);

  private _cam() { return this.viewerSvc.viewer()?.camera; }

  setMode(threeD: boolean): void {
    const v = this.viewerSvc.viewer();
    if (!v || this.is3D() === threeD) return;
    this.is3D.set(threeD);
    if (threeD) v.scene.morphTo3D(1.0);
    else        v.scene.morphTo2D(1.0);
  }

  zoomIn():  void { this._zoom(0.4); }
  zoomOut(): void { this._zoom(-0.8); }

  private _zoom(factor: number): void {
    const cam = this._cam();
    if (!cam) return;
    const height = cam.positionCartographic?.height ?? 1_000_000;
    const amount = height * factor;
    if (factor > 0) cam.zoomIn(amount);
    else            cam.zoomOut(-amount);
  }

  resetView(): void {
    this._cam()?.flyTo({
      destination: Cesium.Cartesian3.fromDegrees(74.5, 34.2, 500_000),
    });
  }
}
