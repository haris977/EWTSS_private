import { Component } from '@angular/core';
import { FormsModule } from '@angular/forms';
import { DrawingStateService } from '../drawing-state.service';
import { EntityForm } from '../entity-form/entity-form';

@Component({
  selector: 'app-entity-sidebar',
  standalone: true,
  imports: [FormsModule, EntityForm],
  templateUrl: './entity-sidebar.html',
  styleUrl: './entity-sidebar.scss',
})
export class EntitySidebar {
  sensorName      = '';
  sensorParent    = '';
  sensorHalfAngle = 25;

  entities;
  selectedName;

  constructor(public drawState: DrawingStateService) {
    this.entities     = this.drawState.entities;
    this.selectedName = this.drawState.selectedName;
  }

  select(name: string): void { this.drawState.selectEntity(name); }

  delete(name: string, ev: Event): void {
    ev.stopPropagation();
    this.drawState.deleteEntity(name);
  }

  addSensor(): void {
    if (!this.sensorName || !this.sensorParent) return;
    const parent = this.drawState.entities()
      .find(e => e.properties.name === this.sensorParent);
    const coords: [number, number, number] = parent
      ? (parent.geometry.type === 'LineString'
          ? parent.geometry.coordinates[0]
          : parent.geometry.type === 'Point'
            ? parent.geometry.coordinates
            : [0, 0, 0])
      : [0, 0, 0];
    this.drawState.commitEntity({
      type: 'Feature',
      geometry: { type: 'Point', coordinates: coords },
      properties: {
        entityType: 'Sensor', name: this.sensorName,
        parentEntity: this.sensorParent, halfAngleDeg: this.sensorHalfAngle,
      },
    });
    this.sensorName = ''; this.sensorParent = '';
  }
}
