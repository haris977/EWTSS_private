import { CommonModule } from '@angular/common';
import { Component, OnDestroy, OnInit, inject } from '@angular/core';
import { Subscription } from 'rxjs';

import { TimeSyncService, TimeSyncStatus } from '../../services/time-sync.service';

@Component({
  selector: 'app-time-sync-card',
  standalone: true,
  imports: [CommonModule],
  templateUrl: './time-sync-card.component.html',
  styleUrls: ['./time-sync-card.component.scss'],
})
export class TimeSyncCardComponent implements OnInit, OnDestroy {
  private readonly timeSync = inject(TimeSyncService);

  status: TimeSyncStatus | null = null;
  private sub?: Subscription;

  ngOnInit(): void {
    this.sub = this.timeSync.poll(5000).subscribe((s) => (this.status = s));
  }

  ngOnDestroy(): void {
    this.sub?.unsubscribe();
  }

  get statusColor(): string {
    if (!this.status) return 'grey';
    switch (this.status.status) {
      case 'healthy':     return 'green';
      case 'drift_warn':  return 'gold';
      case 'drift_alert': return 'orangered';
      case 'sync_lost':   return 'darkred';
      case 'warming':     return 'grey';
      default:            return 'grey';
    }
  }
}
