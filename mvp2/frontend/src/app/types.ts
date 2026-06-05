export type EntityType = 'AreaTarget' | 'Aircraft' | 'Facility' | 'Sensor';
export type DrawingMode = 'none' | EntityType;

export interface PlanningEntity {
  type: 'Feature';
  geometry:
    | { type: 'Polygon';    coordinates: [number, number][][]; }
    | { type: 'LineString'; coordinates: [number, number, number][]; }
    | { type: 'Point';      coordinates: [number, number, number]; };
  properties: {
    entityType:    EntityType;
    name:          string;
    speedMs?:      number;
    halfAngleDeg?: number;
    parentEntity?: string;
  };
}

export interface ScenarioTime {
  start: string;
  stop:  string;
}

export interface PlanningResult {
  exerciseId:   string;
  scenarioTime: ScenarioTime;
  entities:     PlanningEntity[];
}
