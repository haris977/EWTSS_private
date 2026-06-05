import { Injectable, signal } from '@angular/core';

@Injectable({ providedIn: 'root' })
export class ActivityLogService {
  readonly lines = signal<string[]>([]);

  append(msg: string): void { this.lines.update(l => [...l, msg]); }
  clear(): void { this.lines.set([]); }
}
