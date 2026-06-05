import json
import math
from pathlib import Path
from types import SimpleNamespace

from fom_pipeline import read_fom_grid, render_heatmap_png, inject_rectangle_overlay


def _mock_fom_objs():
    def _mk_dp(cols):
        class R:
            class DataSets:
                @staticmethod
                def GetDataSetByName(name):
                    return SimpleNamespace(GetValues=lambda: list(cols.get(name, [])))
        return SimpleNamespace(Exec=lambda *a, **k: R())

    fom = SimpleNamespace(DataProviders=SimpleNamespace(
        GetItemByName=lambda n: _mk_dp({
            "Latitude":  [33.0, 34.0, 35.0],
            "Longitude": [74.0, 75.0, 76.0],
            "FOM Value": [0.1,  0.5,  0.9],
        }) if n == "Value By Point" else (_ for _ in ()).throw(KeyError(n)),
    ))
    cov = SimpleNamespace(DataProviders=SimpleNamespace(
        GetItemByName=lambda n: (_ for _ in ()).throw(KeyError(n))
    ))
    sc = SimpleNamespace(StartTime="t0", StopTime="t1")
    return cov, fom, sc


def test_read_fom_grid_returns_lat_lon_val_triples():
    cov, fom, sc = _mock_fom_objs()
    grid = read_fom_grid(cov, fom, sc)
    assert len(grid) == 3
    lat, lon, val = grid[0]
    assert abs(lat - 33.0) < 1e-9
    assert abs(lon - 74.0) < 1e-9
    assert abs(val - 0.1)  < 1e-9


def test_render_heatmap_png_writes_file(tmp_path):
    grid = [(33.0, 74.0, 0.1), (34.0, 75.0, 0.5), (35.0, 76.0, 0.9)]
    out = tmp_path / "heatmap.png"
    render_heatmap_png(grid, polygon_deg=[(74.0, 33.0), (76.0, 33.0),
                                          (76.0, 35.0), (74.0, 35.0)], out_path=out)
    assert out.exists()
    assert out.stat().st_size > 0
    assert out.read_bytes().startswith(b"\x89PNG\r\n\x1a\n")


def test_inject_rectangle_overlay_appends_packet():
    doc = [{"id": "document", "name": "t", "version": "1.0"},
           {"id": "AreaTarget/Zone1",
            "polygon": {"positions": {"cartographicRadians":
                [math.radians(74.0), math.radians(33.0), 0,
                 math.radians(76.0), math.radians(33.0), 0,
                 math.radians(76.0), math.radians(35.0), 0,
                 math.radians(74.0), math.radians(35.0), 0]}}}]
    bounds_rad = (math.radians(74.0), math.radians(33.0),
                  math.radians(76.0), math.radians(35.0))
    out = inject_rectangle_overlay(doc, "http://localhost:8003/exercises/e/fom-image",
                                   polygon_bounds_rad=bounds_rad)
    ids = [p.get("id") for p in out]
    assert "fom-overlay" in ids
    overlay = next(p for p in out if p["id"] == "fom-overlay")
    assert overlay["rectangle"]["coordinates"]["wsen"] == list(bounds_rad)
    assert overlay["rectangle"]["material"]["image"]["image"] == \
           "http://localhost:8003/exercises/e/fom-image"
