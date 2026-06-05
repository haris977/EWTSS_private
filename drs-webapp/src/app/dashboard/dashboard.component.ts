import { Component } from '@angular/core';

import { TimeSyncCardComponent } from './time-sync-card/time-sync-card.component';

@Component({
  selector: 'app-dashboard',
  standalone: true,
  imports: [TimeSyncCardComponent],
  templateUrl: './dashboard.component.html',
  styleUrls: ['./dashboard.component.scss'],
})
export class DashboardComponent {}
