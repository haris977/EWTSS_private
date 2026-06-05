import { Component, OnDestroy, OnInit, inject, signal } from '@angular/core';
import * as Cesium from 'cesium';
import { ViewerService } from '../viewer.service';

@Component({
  selector: 'app-playback-bar',
  standalone: true,
  templateUrl: './playback-bar.html',
  styleUrl: './playback-bar.scss',
})
export class PlaybackBar implements OnInit, OnDestroy {
  private viewerSvc = inject(ViewerService);

  readonly playing      = signal(false);
  readonly progress     = signal(0);           // 0..1000
  readonly currentLabel = signal('--:--:--');
  readonly stopLabel    = signal('--:--:--');
  readonly hasRange     = signal(false);

  private rafId: number | null = null;
  private seeking = false;

  ngOnInit(): void {
    const tick = () => {
      const v = this.viewerSvc.viewer();
      if (v) {
        const c = v.clock;
        const total = Cesium.JulianDate.secondsDifference(c.stopTime, c.startTime);
        this.hasRange.set(total > 0.5);
        this.playing.set(c.shouldAnimate);

        if (!this.seeking && total > 0) {
          const elapsed = Cesium.JulianDate.secondsDifference(c.currentTime, c.startTime);
          this.progress.set(Math.max(0, Math.min(1000, (elapsed / total) * 1000)));
        }

        this.currentLabel.set(this._fmt(c.currentTime));
        this.stopLabel.set(this._fmt(c.stopTime));
      }
      this.rafId = requestAnimationFrame(tick);
    };
    this.rafId = requestAnimationFrame(tick);
  }

  ngOnDestroy(): void {
    if (this.rafId) cancelAnimationFrame(this.rafId);
  }

  togglePlay(): void {
    const v = this.viewerSvc.viewer();
    if (!v) return;
    v.clock.shouldAnimate = !v.clock.shouldAnimate;
  }

  onSeekStart(): void { this.seeking = true; }
  onSeekEnd():   void { this.seeking = false; }

  onSeekInput(event: Event): void {
    const v = this.viewerSvc.viewer();
    if (!v) return;
    const value = Number((event.target as HTMLInputElement).value);
    this.progress.set(value);
    const c = v.clock;
    const total = Cesium.JulianDate.secondsDifference(c.stopTime, c.startTime);
    if (total <= 0) return;
    const targetSeconds = (value / 1000) * total;
    c.currentTime = Cesium.JulianDate.addSeconds(
      c.startTime, targetSeconds, new Cesium.JulianDate(),
    );
  }

  private _fmt(t: Cesium.JulianDate): string {
    const d = Cesium.JulianDate.toDate(t);
    const hh = String(d.getUTCHours()).padStart(2, '0');
    const mm = String(d.getUTCMinutes()).padStart(2, '0');
    const ss = String(d.getUTCSeconds()).padStart(2, '0');
    return `${hh}:${mm}:${ss}`;
  }
}
