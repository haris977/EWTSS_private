import { ComponentFixture, TestBed } from '@angular/core/testing';
import { of } from 'rxjs';

import { TimeSyncRowComponent } from './time-sync-row.component';
import { TimeSyncService, TimeSyncStatus } from '../../services/time-sync.service';

describe('TimeSyncRowComponent', () => {
  let component: TimeSyncRowComponent;
  let fixture: ComponentFixture<TimeSyncRowComponent>;
  let fakeService: jasmine.SpyObj<TimeSyncService>;

  const sample = (offset_ms: number): TimeSyncStatus => ({
    current_time: '2026-05-14T12:34:56.789Z',
    ntp_offset_ms: offset_ms,
    ntp_jitter_ms: 0.2,
    ntp_peer: 'WS1-SG.local',
    last_sync: '2026-05-14T12:34:50.000Z',
    status: 'healthy',
  });

  beforeEach(async () => {
    fakeService = jasmine.createSpyObj<TimeSyncService>('TimeSyncService', ['poll', 'getStatus']);
    fakeService.poll.and.returnValue(of(sample(0.4)));

    await TestBed.configureTestingModule({
      imports: [TimeSyncRowComponent],
      providers: [{ provide: TimeSyncService, useValue: fakeService }],
    }).compileComponents();

    fixture = TestBed.createComponent(TimeSyncRowComponent);
    component = fixture.componentInstance;
    component.variant = 'rdfs';
    fixture.detectChanges();
  });

  it('subscribes to poll on init', () => {
    expect(fakeService.poll).toHaveBeenCalledWith(5000);
  });

  it('reports WARMING when status is null', () => {
    component.status = null;
    expect(component.effectiveStatus).toBe('WARMING');
  });

  it('reports HEALTHY when offset is within precision', () => {
    component.status = sample(5);
    component.precisionRequiredMs = 10;
    expect(component.effectiveStatus).toBe('HEALTHY');
  });

  it('reports HEALTHY at exactly the precision threshold', () => {
    component.status = sample(10);
    component.precisionRequiredMs = 10;
    expect(component.effectiveStatus).toBe('HEALTHY');
  });

  it('reports ALERT when offset exceeds precision', () => {
    component.status = sample(15);
    component.precisionRequiredMs = 10;
    expect(component.effectiveStatus).toBe('ALERT');
  });

  it('treats negative offsets symmetrically (uses Math.abs)', () => {
    component.status = sample(-15);
    component.precisionRequiredMs = 10;
    expect(component.effectiveStatus).toBe('ALERT');
  });

  it('honours a stricter per-variant precision (1 ms)', () => {
    component.status = sample(2);
    component.precisionRequiredMs = 1;
    expect(component.effectiveStatus).toBe('ALERT');
  });

  it('unsubscribes on destroy without throwing', () => {
    expect(() => component.ngOnDestroy()).not.toThrow();
  });
});
