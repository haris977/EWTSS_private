import { ComponentFixture, TestBed } from '@angular/core/testing';
import { of } from 'rxjs';

import { DashboardComponent } from './dashboard.component';
import { TimeSyncService, TimeSyncStatus } from '../services/time-sync.service';

describe('DashboardComponent', () => {
  let component: DashboardComponent;
  let fixture: ComponentFixture<DashboardComponent>;
  let fakeService: jasmine.SpyObj<TimeSyncService>;

  const healthy: TimeSyncStatus = {
    current_time: '2026-05-14T12:34:56.789Z',
    ntp_offset_ms: 0.4,
    ntp_jitter_ms: 0.2,
    ntp_peer: 'WS1-SG.local',
    last_sync: '2026-05-14T12:34:50.000Z',
    status: 'healthy',
  };

  beforeEach(async () => {
    fakeService = jasmine.createSpyObj<TimeSyncService>('TimeSyncService', ['poll', 'getStatus']);
    fakeService.poll.and.returnValue(of(healthy));

    await TestBed.configureTestingModule({
      imports: [DashboardComponent],
      providers: [{ provide: TimeSyncService, useValue: fakeService }],
    }).compileComponents();

    fixture = TestBed.createComponent(DashboardComponent);
    component = fixture.componentInstance;
    fixture.detectChanges();
  });

  it('creates', () => {
    expect(component).toBeTruthy();
  });

  it('renders the time-sync card in its template', () => {
    const cardEl = fixture.nativeElement.querySelector('app-time-sync-card');
    expect(cardEl).not.toBeNull();
  });

  it('wraps cards in a .dashboard-grid container', () => {
    const gridEl = fixture.nativeElement.querySelector('.dashboard-grid');
    expect(gridEl).not.toBeNull();
  });
});
