from __future__ import annotations

import asyncio
import logging
import os
import uuid
from concurrent.futures import ThreadPoolExecutor
from contextlib import asynccontextmanager
from pathlib import Path
from typing import Any

from fastapi import BackgroundTasks, FastAPI, File, HTTPException, UploadFile
from fastapi.middleware.cors import CORSMiddleware
from fastapi.responses import FileResponse
from pydantic import BaseModel

# Uvicorn's --log-level flag only controls uvicorn's own loggers; the Python
# root logger stays at WARNING by default.  Configure it here so that
# application loggers (stk_com_service, computation_job, etc.) honour the
# same level.  Override with LOG_LEVEL env var if needed.
_log_level = os.environ.get("LOG_LEVEL", "INFO").upper()
logging.basicConfig(
    level=getattr(logging, _log_level, logging.INFO),
    format="%(levelname)-8s %(name)s: %(message)s",
)

@asynccontextmanager
async def lifespan(app: FastAPI):  # noqa: ARG001
    yield
    # Explicit STK shutdown before Python interpreter teardown.
    # STK Engine registers its own atexit handler; if we don't terminate first
    # it fires after sys.meta_path is None and produces an access-violation crash.
    from stk_com_service import _service_instance
    if _service_instance is not None:
        _service_instance.shutdown()


app = FastAPI(title="EWTSS MVP sg-service", lifespan=lifespan)

app.add_middleware(
    CORSMiddleware,
    allow_origins=["http://localhost:4200", "http://127.0.0.1:4200"],
    allow_methods=["*"],
    allow_headers=["*"],
)

# STK 12 installs .glb files across subdirectories under STKData/VO/Models/
# (Air/, Ground/, Sea/, Space/, …).  ExportCZML writes only the bare filename
# (e.g. "aircraft.glb") appended to the base URL, without the subdirectory.
# We resolve this with a search route: GET /models/{filename} walks the tree
# and returns the first match, so all object types work with one base URL.
_STK_MODELS_CANDIDATES = [
    Path(r"C:\Program Files\AGI\STK 12\STKData\VO\Models"),
    Path(r"C:\Program Files\AGI\STK 12.x\STKData\VO\Models"),
]
_stk_models_dir = next((p for p in _STK_MODELS_CANDIDATES if p.is_dir()), None)

log = logging.getLogger(__name__)

# In-memory store: exercise_id → {"status": str, "czml_path": str | None}
_exercises: dict[str, dict[str, Any]] = {}

CZML_DIR = Path("czml_output")
CZML_DIR.mkdir(exist_ok=True)

SCENARIO_DIR = Path("scenario_uploads")
SCENARIO_DIR.mkdir(exist_ok=True)

FOM_DIR = Path("fom_images")
FOM_DIR.mkdir(exist_ok=True)

_pool = ThreadPoolExecutor(max_workers=2)


class ExerciseIn(BaseModel):
    name: str
    start_time: str = "1 Jan 2025 00:00:00.000"
    stop_time: str = "1 Jan 2025 01:00:00.000"


@app.get("/health")
def health() -> dict[str, str]:
    return {"status": "ok"}


@app.get("/models/{filename}")
def get_model(filename: str) -> FileResponse:
    """
    Serve STK 3-D model files (.glb) for air-gapped Cesium rendering.
    STK's ExportCZML writes bare filenames (no subdirectory), so we search
    the full STKData/VO/Models/ tree for a match regardless of category folder.
    """
    if _stk_models_dir:
        matches = list(_stk_models_dir.rglob(filename))
        if matches:
            log.debug("models: serving %s from %s", filename, matches[0])
            return FileResponse(str(matches[0]), media_type="model/gltf-binary")
    raise HTTPException(status_code=404, detail=f"Model '{filename}' not found")


@app.post("/exercises", status_code=201)
def create_exercise(body: ExerciseIn) -> dict[str, str]:
    eid = str(uuid.uuid4())
    _exercises[eid] = {
        "name": body.name,
        "start_time": body.start_time,
        "stop_time": body.stop_time,
        "status": "created",
        "czml_path": None,
    }
    return {"exercise_id": eid, "status": "created"}


@app.post("/exercises/{exercise_id}/compute", status_code=202)
async def trigger_compute(
    exercise_id: str, background_tasks: BackgroundTasks
) -> dict[str, str]:
    if exercise_id not in _exercises:
        raise HTTPException(status_code=404, detail="exercise not found")
    if _exercises[exercise_id]["status"] in ("running", "ready"):
        return {"exercise_id": exercise_id, "status": _exercises[exercise_id]["status"]}
    _exercises[exercise_id]["status"] = "running"
    background_tasks.add_task(_run_compute_background, exercise_id)
    return {"exercise_id": exercise_id, "status": "running"}


async def _run_compute_background(exercise_id: str) -> None:
    from computation_job import run_computation
    import functools

    ex = _exercises[exercise_id]
    output_path = str(CZML_DIR / f"{exercise_id}.czml")
    loop = asyncio.get_event_loop()
    try:
        fn = functools.partial(
            run_computation,
            exercise_id,
            ex["start_time"],
            ex["stop_time"],
            output_path,
            ex.get("scenario_file"),
        )
        await loop.run_in_executor(_pool, fn)
        _exercises[exercise_id]["status"] = "ready"
        _exercises[exercise_id]["czml_path"] = output_path
    except Exception as exc:
        _exercises[exercise_id]["status"] = f"error: {exc}"


@app.post("/exercises/from-scenario-file", status_code=201)
async def create_from_scenario_file(
    background_tasks: BackgroundTasks,
    file: UploadFile = File(...),
    name: str = "Scenario Exercise",
) -> dict[str, str]:
    """
    Accept an STK scenario file (.sc / .vdf), persist it to disk, create an
    exercise record, and immediately kick off computation.
    Returns exercise_id + status "running" so the caller can start polling.
    """
    suffix = Path(file.filename or "scenario.sc").suffix or ".sc"
    eid = str(uuid.uuid4())
    saved_path = SCENARIO_DIR / f"{eid}{suffix}"
    contents = await file.read()
    saved_path.write_bytes(contents)

    _exercises[eid] = {
        "name": name,
        "start_time": "",
        "stop_time": "",
        "scenario_file": str(saved_path),
        "status": "running",
        "czml_path": None,
    }
    background_tasks.add_task(_run_compute_background, eid)
    return {"exercise_id": eid, "status": "running"}


@app.get("/exercises/{exercise_id}/status")
def get_status(exercise_id: str) -> dict[str, str]:
    if exercise_id not in _exercises:
        raise HTTPException(status_code=404, detail="exercise not found")
    return {"exercise_id": exercise_id, "status": _exercises[exercise_id]["status"]}


@app.get("/exercises/{exercise_id}/czml")
def get_czml(exercise_id: str) -> FileResponse:
    if exercise_id not in _exercises:
        raise HTTPException(status_code=404, detail="exercise not found")
    ex = _exercises[exercise_id]
    if ex["status"] != "ready" or not ex["czml_path"]:
        raise HTTPException(status_code=409, detail=f"computation status: {ex['status']}")
    return FileResponse(
        ex["czml_path"],
        media_type="application/json",
        headers={"Content-Disposition": f'inline; filename="{exercise_id}.czml"'},
    )


@app.post("/exercises/{exercise_id}/fom-image", status_code=200)
async def upload_fom_image(
    exercise_id: str,
    file: UploadFile = File(...),
) -> dict[str, str]:
    """
    Accept a FOM PNG exported from STK's 2-D Graphics window and patch the
    exercise CZML to include a georeferenced rectangle overlay.

    Bounds are derived automatically from the AreaTarget polygon already
    present in the exported CZML — no manual coordinate entry required.
    Supported image types: PNG, JPEG, GIF, WebP.
    """
    if exercise_id not in _exercises:
        raise HTTPException(status_code=404, detail="exercise not found")
    ex = _exercises[exercise_id]
    if ex["status"] != "ready" or not ex["czml_path"]:
        raise HTTPException(status_code=409, detail=f"computation status: {ex['status']}")

    suffix  = Path(file.filename or "fom.png").suffix or ".png"
    img_path = FOM_DIR / f"{exercise_id}{suffix}"
    img_path.write_bytes(await file.read())
    log.info("fom-image: saved %s (%.1f KB)", img_path, img_path.stat().st_size / 1024)

    image_url = f"http://localhost:8001/exercises/{exercise_id}/fom-image"
    from stk_com_service import StkComService
    StkComService.inject_fom_rectangle(Path(ex["czml_path"]), image_url)

    return {"exercise_id": exercise_id, "status": "fom_overlay_added"}


@app.get("/exercises/{exercise_id}/fom-image")
def get_fom_image(exercise_id: str) -> FileResponse:
    for suffix in (".png", ".jpg", ".jpeg", ".gif", ".webp"):
        p = FOM_DIR / f"{exercise_id}{suffix}"
        if p.exists():
            return FileResponse(str(p))
    raise HTTPException(status_code=404, detail="FOM image not uploaded for this exercise")
