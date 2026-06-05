import { Component, inject } from '@angular/core';
import { ActivityLogService } from '../activity-log.service';

@Component({
  selector: 'app-activity-log',
  standalone: true,
  templateUrl: './activity-log.html',
  styleUrl: './activity-log.scss',
})
export class ActivityLog {
  lines = inject(ActivityLogService).lines;
}
