import { TestBed } from '@angular/core/testing';
import { DrawingStateService } from './drawing-state.service';

describe('DrawingStateService', () => {
  let svc: DrawingStateService;
  beforeEach(() => { TestBed.configureTestingModule({}); svc = TestBed.inject(DrawingStateService); });

  it('starts in none mode', () => expect(svc.mode()).toBe('none'));
  it('sets mode on setMode', () => { svc.setMode('Aircraft'); expect(svc.mode()).toBe('Aircraft'); });
  it('toggles back to none when same mode set again', () => {
    svc.setMode('Aircraft'); svc.setMode('Aircraft');
    expect(svc.mode()).toBe('none');
  });
  it('accumulates coords', () => {
    svc.addCoord(74.0, 34.0); svc.addCoord(75.0, 34.0);
    expect(svc.inProgressCoords().length).toBe(2);
  });
  it('commitEntity appends entity and resets mode', () => {
    svc.setMode('Facility');
    svc.commitEntity({ type: 'Feature',
      geometry: { type: 'Point', coordinates: [74.0, 34.0, 0] },
      properties: { entityType: 'Facility', name: 'Site1' } });
    expect(svc.entities().length).toBe(1);
    expect(svc.mode()).toBe('none');
  });
  it('deleteEntity removes by name', () => {
    svc.commitEntity({ type: 'Feature',
      geometry: { type: 'Point', coordinates: [74.0, 34.0, 0] },
      properties: { entityType: 'Facility', name: 'Site1' } });
    svc.deleteEntity('Site1');
    expect(svc.entities().length).toBe(0);
  });
});
