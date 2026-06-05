import { TestBed } from '@angular/core/testing';
import { provideHttpClient } from '@angular/common/http';
import { HttpTestingController, provideHttpClientTesting } from '@angular/common/http/testing';

import { TimeSyncService, TimeSyncStatus } from './time-sync.service';

describe('TimeSyncService', () => {
  let service: TimeSyncService;
  let http: HttpTestingController;

  beforeEach(() => {
    TestBed.configureTestingModule({
      providers: [
        provideHttpClient(),
        provideHttpClientTesting(),
        TimeSyncService,
      ],
    });
    service = TestBed.inject(TimeSyncService);
    http = TestBed.inject(HttpTestingController);
  });

  afterEach(() => http.verify());

  it('fetches /time/status and exposes parsed result', (done) => {
    service.getStatus().subscribe((status: TimeSyncStatus) => {
      expect(status.status).toBe('healthy');
      expect(status.ntp_offset_ms).toBeCloseTo(0.4);
      expect(status.ntp_peer).toBe('WS1-SG.local');
      done();
    });
    const req = http.expectOne('/time/status');
    expect(req.request.method).toBe('GET');
    req.flush({
      current_time: '2026-05-14T12:34:56.789Z',
      ntp_offset_ms: 0.4,
      ntp_jitter_ms: 0.2,
      ntp_peer: 'WS1-SG.local',
      last_sync: '2026-05-14T12:34:50.000Z',
      status: 'healthy',
    });
  });

  it('exposes status type narrowing for the five known states', () => {
    const states: TimeSyncStatus['status'][] = [
      'healthy', 'warming', 'drift_warn', 'drift_alert', 'sync_lost',
    ];
    expect(states.length).toBe(5);
  });
});
