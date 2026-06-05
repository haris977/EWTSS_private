import { Component, inject } from '@angular/core';
import { FormsModule } from '@angular/forms';
import { DrawingStateService } from '../drawing-state.service';
import { PlanningEntity } from '../types';

@Component({
  selector: 'app-entity-form',
  standalone: true,
  imports: [FormsModule],
  templateUrl: './entity-form.html',
  styleUrl: './entity-form.scss',
})
export class EntityForm {
  drawState = inject(DrawingStateService);
  entity = this.drawState.selectedEntity;

  close(): void { this.drawState.selectEntity(null); }

  // ---- Name ----
  setName(e: PlanningEntity, newName: string): void {
    const trimmed = newName.trim();
    if (!trimmed || trimmed === e.properties.name) return;
    // Reject duplicates
    if (this.drawState.entities().some(x => x.properties.name === trimmed)) return;
    this.drawState.updateProperties(e.properties.name, { name: trimmed });
  }

  // ---- Facility ----
  facilityLat(e: PlanningEntity): number {
    return (e.geometry.type === 'Point') ? e.geometry.coordinates[1] : 0;
  }
  facilityLon(e: PlanningEntity): number {
    return (e.geometry.type === 'Point') ? e.geometry.coordinates[0] : 0;
  }
  facilityAlt(e: PlanningEntity): number {
    return (e.geometry.type === 'Point') ? (e.geometry.coordinates[2] ?? 0) : 0;
  }
  setFacilityCoord(e: PlanningEntity, idx: 0 | 1 | 2, value: number): void {
    if (e.geometry.type !== 'Point' || Number.isNaN(value)) return;
    const c = [...e.geometry.coordinates] as [number, number, number];
    c[idx] = value;
    this.drawState.updateGeometry(e.properties.name, {
      type: 'Point', coordinates: c,
    });
  }

  // ---- Aircraft ----
  aircraftSpeed(e: PlanningEntity): number { return e.properties.speedMs ?? 150; }
  setAircraftSpeed(e: PlanningEntity, v: number): void {
    if (Number.isNaN(v)) return;
    this.drawState.updateProperties(e.properties.name, { speedMs: v });
  }
  aircraftAlt(e: PlanningEntity): number {
    return (e.geometry.type === 'LineString' && e.geometry.coordinates[0])
      ? e.geometry.coordinates[0][2] ?? 0 : 0;
  }
  setAircraftAlt(e: PlanningEntity, v: number): void {
    if (e.geometry.type !== 'LineString' || Number.isNaN(v)) return;
    const coords = e.geometry.coordinates.map(c => [c[0], c[1], v] as [number, number, number]);
    this.drawState.updateGeometry(e.properties.name, { type: 'LineString', coordinates: coords });
  }
  aircraftWaypointCount(e: PlanningEntity): number {
    return e.geometry.type === 'LineString' ? e.geometry.coordinates.length : 0;
  }

  // ---- AreaTarget ----
  areaVertexCount(e: PlanningEntity): number {
    if (e.geometry.type !== 'Polygon') return 0;
    const ring = e.geometry.coordinates[0];
    // Ring is closed (first === last). User cares about unique vertices.
    return Math.max(0, ring.length - 1);
  }

  // ---- Sensor ----
  setSensorParent(e: PlanningEntity, v: string): void {
    this.drawState.updateProperties(e.properties.name, { parentEntity: v.trim() || undefined });
  }
  setSensorHalfAngle(e: PlanningEntity, v: number): void {
    if (Number.isNaN(v)) return;
    const clamped = Math.max(1, Math.min(90, v));
    this.drawState.updateProperties(e.properties.name, { halfAngleDeg: clamped });
  }
}
