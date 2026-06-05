import { Injectable, signal } from '@angular/core';
import * as Cesium from 'cesium';

@Injectable({ providedIn: 'root' })
export class ViewerService {
  readonly viewer = signal<Cesium.Viewer | null>(null);
}
