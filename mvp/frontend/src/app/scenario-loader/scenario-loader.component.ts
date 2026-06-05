import { Component, OnDestroy, OnInit } from '@angular/core';
import { HttpClient } from '@angular/common/http';
import { firstValueFrom } from 'rxjs';

const SG_BASE = 'http://localhost:8001';

@Component({
  selector: 'app-scenario-loader',
  templateUrl: './scenario-loader.component.html',
  styleUrls: ['./scenario-loader.component.scss'],
})
export class ScenarioLoaderComponent implements OnInit, OnDestroy {
  exerciseName   = 'MVP Test';
  exerciseId     = '';
  status         = '';
  log: string[]  = [];
  localFileName  = '';
  stkFileName    = '';

  private czmlErrorListener!: EventListener;

  constructor(private http: HttpClient) {}

  ngOnInit(): void {
    this.czmlErrorListener = (e: Event) => {
      const msg = (e as CustomEvent<{ message: string }>).detail.message;
      this.status = 'error';
      this.addLog(`CZML load failed in Cesium: ${msg}`);
    };
    window.addEventListener('czml-error', this.czmlErrorListener);
  }

  ngOnDestroy(): void {
    window.removeEventListener('czml-error', this.czmlErrorListener);
  }

  private addLog(msg: string): void {
    const ts = new Date().toISOString().slice(11, 19);
    this.log = [...this.log, `${ts}  ${msg}`];
  }

  async runFullFlow(): Promise<void> {
    this.log = [];
    this.exerciseId = '';

    try {
      // 1. Create exercise
      this.status = 'creating';
      this.addLog('POST /exercises');
      const created: any = await firstValueFrom(
        this.http.post(`${SG_BASE}/exercises`, { name: this.exerciseName })
      );
      this.exerciseId = created.exercise_id;
      this.addLog(`Created: ${this.exerciseId}`);

      // 2. Trigger compute
      this.status = 'computing';
      this.addLog('POST /exercises/{id}/compute');
      await firstValueFrom(
        this.http.post(`${SG_BASE}/exercises/${this.exerciseId}/compute`, {})
      );

      // 3. Poll until ready (30s timeout)
      this.addLog('Polling status…');
      await this.pollUntilReady();

      // 4. Dispatch load-czml event to the sibling CesiumViewerComponent
      this.status = 'loading';
      const czmlUrl = `${SG_BASE}/exercises/${this.exerciseId}/czml`;
      this.addLog(`Dispatching load-czml → ${czmlUrl}`);
      window.dispatchEvent(
        new CustomEvent('load-czml', { detail: { url: czmlUrl } })
      );

      this.status = 'ready';
      this.addLog('Done — check the globe for the moving platform.');
    } catch (err: any) {
      this.status = 'error';
      this.addLog(`Error: ${err?.message ?? String(err)}`);
    }
  }

  loadLocalCzml(event: Event): void {
    const input = event.target as HTMLInputElement;
    const file  = input.files?.[0];
    if (!file) return;

    this.log          = [];
    this.localFileName = file.name;
    this.exerciseId   = '';
    this.status       = 'loading';
    this.addLog(`Loading local file: ${file.name}`);

    const url = URL.createObjectURL(file);
    window.dispatchEvent(new CustomEvent('load-czml', { detail: { url } }));

    this.status = 'ready';
    this.addLog('Done — CZML dispatched to viewer.');

    // Free the blob URL after Cesium has had time to fetch it
    setTimeout(() => URL.revokeObjectURL(url), 60_000);

    // Reset so the same file can be reloaded
    input.value = '';
  }

  async runFromStkFile(event: Event): Promise<void> {
    const input = event.target as HTMLInputElement;
    const file  = input.files?.[0];
    if (!file) return;

    this.log        = [];
    this.stkFileName = file.name;
    this.exerciseId = '';
    input.value     = '';

    try {
      // 1. Upload the scenario file — backend saves it and starts computation.
      this.status = 'computing';
      this.addLog(`Uploading scenario file: ${file.name}`);
      const form = new FormData();
      form.append('file', file, file.name);
      form.append('name', file.name);

      const created: any = await firstValueFrom(
        this.http.post(`${SG_BASE}/exercises/from-scenario-file`, form)
      );
      this.exerciseId = created.exercise_id;
      this.addLog(`Created: ${this.exerciseId}`);

      // 2. Poll until ready
      this.addLog('Polling status…');
      await this.pollUntilReady();

      // 3. Load CZML into Cesium
      this.status = 'loading';
      const czmlUrl = `${SG_BASE}/exercises/${this.exerciseId}/czml`;
      this.addLog(`Dispatching load-czml → ${czmlUrl}`);
      window.dispatchEvent(new CustomEvent('load-czml', { detail: { url: czmlUrl } }));

      this.status = 'ready';
      this.addLog('Done — check the globe for the scenario platforms.');
    } catch (err: any) {
      this.status = 'error';
      this.addLog(`Error: ${err?.message ?? String(err)}`);
    }
  }

  private async pollUntilReady(): Promise<void> {
    // STK attach + scenario build + access compute + CZML export can take 2–3 min.
    const TIMEOUT_MS  = 3 * 60_000;
    const POLL_MS     = 2_000;
    const deadline = Date.now() + TIMEOUT_MS;
    while (Date.now() < deadline) {
      const s: any = await firstValueFrom(
        this.http.get(`${SG_BASE}/exercises/${this.exerciseId}/status`)
      );
      this.addLog(`Status: ${s.status}`);
      if (s.status === 'ready') return;
      if (s.status.startsWith('error')) throw new Error(s.status);
      await new Promise(r => setTimeout(r, POLL_MS));
    }
    throw new Error('Timed out waiting for computation (3 min)');
  }
}
