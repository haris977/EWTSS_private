import { HttpClient } from '@angular/common/http';
import { Injectable, inject } from '@angular/core';
import { Observable, interval, switchMap, shareReplay, startWith } from 'rxjs';

export type SyncStatus =
  | 'healthy'
  | 'warming'
  | 'drift_warn'
  | 'drift_alert'
  | 'sync_lost';

export interface TimeSyncStatus {
  current_time: string;
  ntp_offset_ms: number;
  ntp_jitter_ms: number;
  ntp_peer: string | null;
  last_sync: string;
  status: SyncStatus;
}

@Injectable({ providedIn: 'root' })
export class TimeSyncService {
  private readonly http = inject(HttpClient);

  /** One-shot fetch. */
  getStatus(): Observable<TimeSyncStatus> {
    return this.http.get<TimeSyncStatus>('/time/status');
  }

  /**
   * Continuous polling every `intervalMs` (default 5 s).
   *
   * Emits the first value immediately on subscribe (via `startWith` triggering
   * an initial `switchMap`), then on each interval tick. Replayable so multiple
   * subscribers share one HTTP stream.
   */
  poll(intervalMs = 5000): Observable<TimeSyncStatus> {
    return interval(intervalMs).pipe(
      startWith(0),
      switchMap(() => this.getStatus()),
      shareReplay({ bufferSize: 1, refCount: true }),
    );
  }
}
