import { CommonModule } from '@angular/common';
import { Component, Input, OnDestroy, OnInit, inject } from '@angular/core';
import { Subscription } from 'rxjs';

import { TimeSyncService, TimeSyncStatus } from '../../services/time-sync.service';

@Component({
  selector: 'app-variant-time-sync-row',
  standalone: true,
  imports: [CommonModule],
  template: `
    <div class="row" *ngIf="status">
      <span>Time Sync</span>
      <span>Configured precision: {{ precisionRequiredMs }} ms</span>
      <span>Current offset: {{ status.ntp_offset_ms | number:'1.2-2' }} ms</span>
      <span class="badge" [class.healthy]="effectiveStatus === 'HEALTHY'"
                          [class.alert]="effectiveStatus !== 'HEALTHY'">
        {{ effectiveStatus }}
      </span>
    </div>
  `,
  styles: [`
    .row { display: flex; gap: 16px; padding: 8px 0; }
    .badge.healthy { color: green; }
    .badge.alert   { color: red; }
  `],
})
export class TimeSyncRowComponent implements OnInit, OnDestroy {
  private readonly timeSync = inject(TimeSyncService);

  @Input() variant!: string;
  @Input() precisionRequiredMs = 10;

  status: TimeSyncStatus | null = null;
  private sub?: Subscription;

  ngOnInit(): void {
    this.sub = this.timeSync.poll(5000).subscribe((s) => (this.status = s));
  }

  ngOnDestroy(): void {
    this.sub?.unsubscribe();
  }

  get effectiveStatus(): 'HEALTHY' | 'ALERT' | 'WARMING' {
    if (!this.status) return 'WARMING';
    return Math.abs(this.status.ntp_offset_ms) <= this.precisionRequiredMs
      ? 'HEALTHY'
      : 'ALERT';
  }
}
