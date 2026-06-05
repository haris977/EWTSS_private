import { Injectable, computed, signal } from '@angular/core';
import { DrawingMode, PlanningEntity } from './types';

@Injectable({ providedIn: 'root' })
export class DrawingStateService {
  readonly mode     = signal<DrawingMode>('none');
  readonly entities = signal<PlanningEntity[]>([]);
  readonly inProgressCoords = signal<[number, number, number][]>([]);
  readonly selectedName = signal<string | null>(null);

  readonly selectedEntity = computed<PlanningEntity | null>(() => {
    const name = this.selectedName();
    if (!name) return null;
    return this.entities().find(e => e.properties.name === name) ?? null;
  });

  setMode(mode: DrawingMode): void {
    if (this.mode() === mode) {
      this.mode.set('none');
      this.inProgressCoords.set([]);
    } else {
      this.mode.set(mode);
      this.inProgressCoords.set([]);
    }
  }

  addCoord(lon: number, lat: number, altM: number = 0): void {
    this.inProgressCoords.update(c => [...c, [lon, lat, altM]]);
  }

  commitEntity(entity: PlanningEntity): void {
    this.entities.update(es => [...es, entity]);
    this.selectedName.set(entity.properties.name);
    this.mode.set('none');
    this.inProgressCoords.set([]);
  }

  updateProperties(name: string, patch: Partial<PlanningEntity['properties']>): void {
    this.entities.update(es => es.map(e =>
      e.properties.name === name
        ? { ...e, properties: { ...e.properties, ...patch } as PlanningEntity['properties'] }
        : e
    ));
    // Keep selection in sync if the entity was renamed
    if (patch.name && this.selectedName() === name) this.selectedName.set(patch.name);
  }

  updateGeometry(name: string, geometry: PlanningEntity['geometry']): void {
    this.entities.update(es => es.map(e =>
      e.properties.name === name ? { ...e, geometry } : e
    ));
  }

  selectEntity(name: string | null): void { this.selectedName.set(name); }

  deleteEntity(name: string): void {
    this.entities.update(es => es.filter(e => e.properties.name !== name));
    if (this.selectedName() === name) this.selectedName.set(null);
  }

  clearAll(): void {
    this.entities.set([]);
    this.mode.set('none');
    this.inProgressCoords.set([]);
    this.selectedName.set(null);
  }
}
