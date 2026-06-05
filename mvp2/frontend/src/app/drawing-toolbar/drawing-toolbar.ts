import { Component } from '@angular/core';
import { DrawingStateService } from '../drawing-state.service';
import { DrawingMode } from '../types';

@Component({
  selector: 'app-drawing-toolbar',
  standalone: true,
  templateUrl: './drawing-toolbar.html',
  styleUrl: './drawing-toolbar.scss',
})
export class DrawingToolbar {
  mode;
  constructor(private drawState: DrawingStateService) {
    this.mode = this.drawState.mode;
  }
  setMode(m: DrawingMode): void { this.drawState.setMode(m); }
}
