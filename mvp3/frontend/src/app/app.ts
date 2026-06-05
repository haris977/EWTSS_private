import { Component, signal } from '@angular/core';
import { CesiumViewer } from './cesium-viewer/cesium-viewer';
import { ScenarioLoader } from './scenario-loader/scenario-loader';

@Component({
  selector: 'app-root',
  standalone: true,
  imports: [CesiumViewer, ScenarioLoader],
  template: `
    <div class="shell">
      <header class="header">
        <div class="brand">EWTSS · MVP3 (Ion SDK)</div>
        <app-scenario-loader (czmlReady)="czmlUrl.set($event)"/>
      </header>
      <section class="stage">
        <app-cesium-viewer [czmlUrl]="czmlUrl()"/>
      </section>
    </div>`,
  styles: [`
    :host  { display: block; height: 100vh; }
    .shell { display: flex; flex-direction: column; height: 100%; }
    .header{ flex: 0 0 56px; display: flex; align-items: center; justify-content: space-between;
             padding: 0 20px; background: var(--bg-panel);
             border-bottom: 1px solid var(--border); gap: 20px; }
    .brand { font-weight: 600; font-size: 14px; letter-spacing: .3px; }
    .stage { flex: 1 1 auto; position: relative; background: var(--bg-deep);
             overflow: hidden; min-height: 0; }
  `]
})
export class App {
  czmlUrl = signal<string | null>(null);
}
