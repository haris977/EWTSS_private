def test_app_has_lifespan_attached():
    from drs_server.main import app
    assert app.router.lifespan_context is not None


def test_app_has_time_status_route():
    from drs_server.main import app
    route_paths = {getattr(r, "path", None) for r in app.routes}
    assert "/time/status" in route_paths
    assert "/health" in route_paths


def test_main_module_runs_uvicorn(monkeypatch):
    """`python -m drs_server` should call uvicorn.run with the app + host + port."""
    called = {}

    def fake_run(app_or_path, host, port, **kwargs):
        called["args"] = (app_or_path, host, port)

    import drs_server.__main__ as entry
    monkeypatch.setattr(entry, "uvicorn", type("U", (), {"run": fake_run}))
    entry.main()
    assert called["args"][1] == "0.0.0.0"
    assert called["args"][2] == 8000
