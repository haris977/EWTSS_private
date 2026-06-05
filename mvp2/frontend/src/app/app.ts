import { Component, signal } from '@angular/core';
import { DrawingToolbar }  from './drawing-toolbar/drawing-toolbar';
import { EntitySidebar }   from './entity-sidebar/entity-sidebar';
import { ScenarioPlanner } from './scenario-planner/scenario-planner';
import { ActivityLog }     from './activity-log/activity-log';
import { CesiumViewer }    from './cesium-viewer/cesium-viewer';
import { CanvasControls }  from './canvas-controls/canvas-controls';
import { PlaybackBar }     from './playback-bar/playback-bar';

@Component({
  selector: 'app-root',
  standalone: true,
  imports: [DrawingToolbar, EntitySidebar, ScenarioPlanner, ActivityLog,
            CesiumViewer, CanvasControls, PlaybackBar],
  template: `
    <div class="shell">
      <header class="header">
        <div class="brand">EWTSS · Scenario Planner</div>
        <app-scenario-planner (czmlReady)="czmlUrl.set($event)"/>
      </header>
      <div class="main">
        <aside class="rail rail-left"><app-drawing-toolbar/></aside>
        <section class="stage">
          <app-cesium-viewer [czmlUrl]="czmlUrl()"/>
          <app-canvas-controls/>
          <app-playback-bar/>
        </section>
        <aside class="rail rail-right">
          <app-entity-sidebar/>
          <app-activity-log/>
        </aside>
      </div>
    </div>`,
  styles: [`
    :host    { display: block; height: 100vh; }
    .shell   { display: flex; flex-direction: column; height: 100%; }
    .header  { flex: 0 0 56px;
               display: flex; align-items: center; justify-content: space-between;
               padding: 0 20px; background: var(--bg-panel);
               border-bottom: 1px solid var(--border); }
    .brand   { font-weight: 600; font-size: 14px; letter-spacing: .3px; }
    .main    { flex: 1 1 auto; display: flex; overflow: hidden; min-height: 0; }
    .rail    { flex: 0 0 auto;
               background: var(--bg-panel); display: flex; flex-direction: column;
               overflow: hidden; }
    .rail-left  { width: 260px; border-right: 1px solid var(--border); }
    .rail-right { width: 300px; border-left:  1px solid var(--border); }
    .stage   { flex: 1 1 auto; position: relative; background: var(--bg-deep);
               overflow: hidden; min-width: 0; }
  `]
})
export class App {
  czmlUrl = signal<string | null>(null);
}
