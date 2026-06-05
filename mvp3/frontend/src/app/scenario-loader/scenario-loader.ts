import { Component, EventEmitter, Output, inject, signal } from '@angular/core';
import { FormsModule } from '@angular/forms';
import { HttpClient } from '@angular/common/http';

const SG_BASE = 'http://localhost:8003';

@Component({
  selector: 'app-scenario-loader',
  standalone: true,
  imports: [FormsModule],
  templateUrl: './scenario-loader.html',
  styleUrl: './scenario-loader.scss',
})
export class ScenarioLoader {
  @Output() czmlReady = new EventEmitter<string>();

  private http = inject(HttpClient);

  exerciseName = 'MVP3 Test';
  startTime    = '1 Jan 2025 00:00:00.000';
  stopTime     = '1 Jan 2025 01:00:00.000';
  running      = signal(false);
  status       = signal('');
  log          = signal<string[]>([]);

  async runFullFlow(): Promise<void> {
    this.running.set(true);
    this.status.set('creating');
    this.log.set([]);

    try {
      const create = await fetch(`${SG_BASE}/exercises`, { method: 'POST' });
      if (!create.ok) throw new Error(`POST /exercises -> ${create.status}`);
      const { exercise_id } = await create.json();
      this._log(`Exercise created: ${exercise_id}`);

      this.status.set('computing');
      const compute = await fetch(`${SG_BASE}/exercises/${exercise_id}/compute`, {
        method:  'POST',
        headers: { 'Content-Type': 'application/json' },
        body:    JSON.stringify({ start_time: this.startTime, stop_time: this.stopTime }),
      });
      if (!compute.ok) throw new Error(`POST /compute -> ${compute.status}`);
      this._log('Computation started — polling status');

      for (let attempt = 0; attempt < 120; attempt++) {
        await new Promise(r => setTimeout(r, 1000));
        const s = await fetch(`${SG_BASE}/exercises/${exercise_id}/status`).then(r => r.json());
        this._log(`Status: ${s.status}`);
        if (s.status === 'ready') {
          this.status.set('ready');
          this.czmlReady.emit(`${SG_BASE}/exercises/${exercise_id}/czml`);
          this._log('Done — loading CZML');
          return;
        }
        if (s.status.startsWith('error')) throw new Error(s.status);
      }
      throw new Error('timed out after 120s');
    } catch (err: any) {
      this.status.set('error');
      this._log(`ERROR: ${err?.message ?? String(err)}`);
    } finally {
      this.running.set(false);
    }
  }

  private _log(msg: string): void {
    const ts = new Date().toISOString().slice(11, 19);
    this.log.update(lines => [...lines, `${ts}  ${msg}`]);
  }
}
