# drs-webapp — DRS Engineer surface (Angular)

Browser-based frontend for the **DRS Engineer** persona on WS2, served by
`drs-server` per [ADR-018](../docs/ewtss/decision-record.md). Companion to
the `Sg.App` (WPF) surface on WS1 used by the SG Operator.

| Concern | Choice |
|---|---|
| Framework | **Angular 18** preferred per [ADR-018](../docs/ewtss/decision-record.md); React acceptable fallback (not exercised here). |
| Styling | SCSS |
| Routing | Standalone routes via `app.routes.ts` |
| Tests | Karma + Jasmine (default Angular CLI test rig) |
| Strict mode | TypeScript `strict: true` + Angular `strictTemplates`. |

## Layout (after `ng new` scaffold)

```
drs-webapp/
  angular.json
  package.json
  tsconfig.json
  src/
    index.html
    main.ts
    styles.scss
    app/
      app.component.{ts,html,scss,spec.ts}
      app.config.ts          # provideRouter, provideHttpClient, etc.
      app.routes.ts          # top-level Routes[]
      services/              # added per B1.3 Task 21
        time-sync.service.ts
      dashboard/             # added per B1.3 Task 22
        time-sync-card/
      variants/health/       # added per B1.3 Task 23
        time-sync-row.component.{ts,html,scss}
```

## Dev

```
cd drs-webapp
npm install        # first time only; pulls Angular + zone.js + rxjs
npm run start      # dev server at http://localhost:4200/
npm test           # Karma + Jasmine (locally interactive; CI headless — currently paused, see ../.github/disabled/README.md)
npm run build      # production build into dist/
```

In production, the build artefact under `dist/drs-webapp/` is served
statically by `drs-server` per ADR-018; the DRS Engineer opens
`http://localhost:<drs-server-port>/` on WS2.

## Status

- Scaffolded 2026-05-20 via `npx -p @angular/cli@18 ng new`.
- Configuration: `--routing --style=scss --skip-install --skip-git`. `--skip-install`
  was used at scaffold time; `npm install` runs separately when needed.
- B1.3 Phase 6 implementation (Time Sync surfaces) lands per the
  [B1.3 implementation plan §Phase 6](../docs/ewtss/plans/time-sync-plan.md).
