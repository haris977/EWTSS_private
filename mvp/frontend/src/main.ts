// CESIUM_BASE_URL MUST be set before any Cesium import.
// If this assignment comes after a Cesium import, WebWorkers silently fail —
// the symptom is a blank canvas with no console error.
(window as any).CESIUM_BASE_URL = '/cesium';

import { platformBrowserDynamic } from '@angular/platform-browser-dynamic';
import { AppModule } from './app/app.module';

platformBrowserDynamic()
  .bootstrapModule(AppModule)
  .catch(err => console.error(err));
