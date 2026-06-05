import { Component, Output, EventEmitter, inject } from '@angular/core';
import { FormsModule } from '@angular/forms';
import { HttpClient } from '@angular/common/http';
import { DrawingStateService } from '../drawing-state.service';
import { ActivityLogService }  from '../activity-log.service';

const API = 'http://localhost:8002';

@Component({
  selector: 'app-scenario-planner',
  standalone: true,
  imports: [FormsModule],
  templateUrl: './scenario-planner.html',
  styleUrl: './scenario-planner.scss',
})
export class ScenarioPlanner {
  @Output() czmlReady = new EventEmitter<string>();

  private http      = inject(HttpClient);
  private drawState = inject(DrawingStateService);
  private activity  = inject(ActivityLogService);

  startTime = '1 Jan 2025 00:00:00.000';
  stopTime  = '1 Jan 2025 01:00:00.000';
  running   = false;

  submit(): void {
    this.running = true;
    this.activity.clear();
    const entities = this.drawState.entities();

    this.http.post<{exercise_id: string}>(`${API}/exercises`, {}).subscribe({
      next: ({ exercise_id }) => {
        this.activity.append(`Exercise created: ${exercise_id}`);
        const plan = {
          exerciseId:   exercise_id,
          scenarioTime: { start: this.startTime, stop: this.stopTime },
          entities,
        };
        this.http.post<{status: string}>(`${API}/exercises/${exercise_id}/compute`, plan)
          .subscribe({
            next: () => {
              this.activity.append('Computation started - polling...');
              this._poll(exercise_id);
            },
            error: e => this._fail(`compute failed: ${e.message}`),
          });
      },
      error: e => this._fail(`create failed: ${e.message}`),
    });
  }

  private _poll(eid: string, attempts = 0): void {
    if (attempts > 120) { this._fail('timed out after 120s'); return; }
    setTimeout(() => {
      this.http.get<{status: string}>(`${API}/exercises/${eid}/status`).subscribe({
        next: ({ status }) => {
          this.activity.append(`Status: ${status}`);
          if (status === 'ready') {
            this.running = false;
            this.czmlReady.emit(`${API}/exercises/${eid}/czml`);
          } else if (status.startsWith('error')) {
            this._fail(status);
          } else {
            this._poll(eid, attempts + 1);
          }
        },
        error: e => this._fail(e.message),
      });
    }, 1000);
  }

  clearScenario(): void {
    this.drawState.clearAll();
    this.activity.clear();
  }

  private _fail(msg: string): void {
    this.running = false;
    this.activity.append(`ERROR: ${msg}`);
  }
}
