import { ComponentFixture, TestBed } from '@angular/core/testing';
import { of } from 'rxjs';

import { TimeSyncCardComponent } from './time-sync-card.component';
import { TimeSyncService, TimeSyncStatus } from '../../services/time-sync.service';

describe('TimeSyncCardComponent', () => {
  let component: TimeSyncCardComponent;
  let fixture: ComponentFixture<TimeSyncCardComponent>;
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
      imports: [TimeSyncCardComponent],
      providers: [{ provide: TimeSyncService, useValue: fakeService }],
    }).compileComponents();

    fixture = TestBed.createComponent(TimeSyncCardComponent);
    component = fixture.componentInstance;
    fixture.detectChanges();
  });

  it('subscribes to the service on init', () => {
    expect(fakeService.poll).toHaveBeenCalledWith(5000);
  });

  it('reflects the latest status', () => {
    expect(component.status).toEqual(healthy);
  });

  it('maps healthy status to green', () => {
    expect(component.statusColor).toBe('green');
  });

  it('maps null status to grey', () => {
    component.status = null;
    expect(component.statusColor).toBe('grey');
  });

  it('maps drift_warn to gold', () => {
    component.status = { ...healthy, status: 'drift_warn' };
    expect(component.statusColor).toBe('gold');
  });

  it('maps drift_alert to orangered', () => {
    component.status = { ...healthy, status: 'drift_alert' };
    expect(component.statusColor).toBe('orangered');
  });

  it('maps sync_lost to darkred', () => {
    component.status = { ...healthy, status: 'sync_lost' };
    expect(component.statusColor).toBe('darkred');
  });

  it('unsubscribes on destroy without throwing', () => {
    expect(() => component.ngOnDestroy()).not.toThrow();
  });
});
