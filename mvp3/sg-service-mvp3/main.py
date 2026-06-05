from __future__ import annotations
import asyncio
import functools
import logging
import os
import threading
import uuid
from concurrent.futures import ThreadPoolExecutor
from contextlib import asynccontextmanager
from pathlib import Path
from typing import Any

from fastapi import FastAPI, HTTPException
from fastapi.middleware.cors import CORSMiddleware
from fastapi.responses import FileResponse
from pydantic import BaseModel

_log_level = os.environ.get("LOG_LEVEL", "INFO").upper()
logging.basicConfig(
    level=getattr(logging, _log_level, logging.INFO),
    format="%(levelname)-8s %(name)s: %(message)s",
)
log = logging.getLogger(__name__)

_BASE    = Path(__file__).parent
CZML_DIR = _BASE / "czml_output"; CZML_DIR.mkdir(exist_ok=True)
FOM_DIR  = _BASE / "fom_images";  FOM_DIR.mkdir(exist_ok=True)

_exercises: dict[str, dict[str, Any]] = {}
_exercises_lock = threading.Lock()
_pool = ThreadPoolExecutor(max_workers=1)  # single STK seat

_STK_MODELS_CANDIDATES = [
    Path(r"C:\Program Files\AGI\STK 12\STKData\VO\Models"),
    Path(r"C:\Program Files\AGI\STK 12.x\STKData\VO\Models"),
]
_stk_models_dir = next((p for p in _STK_MODELS_CANDIDATES if p.is_dir()), None)


@asynccontextmanager
async def lifespan(app: FastAPI):  # noqa: ARG001
    yield
    # Drop the thread pool first so no compute worker holds STK busy.
    _pool.shutdown(wait=False, cancel_futures=True)

    import stk_com_service_mvp3 as _stk_mod
    if _stk_mod._service_instance is None:
        return
    # STK Desktop's Quit() can hang if the UI is busy — run shutdown in a
    # daemon thread with a short timeout so uvicorn doesn't stall forever.
    stopper = threading.Thread(
        target=_stk_mod._service_instance.shutdown,
        daemon=True,
        name="stk-shutdown",
    )
    stopper.start()
    stopper.join(timeout=5.0)
    if stopper.is_alive():
        log.warning("STK shutdown did not complete in 5s — leaving it detached")


app = FastAPI(title="EWTSS MVP3 sg-service-mvp3", lifespan=lifespan)
app.add_middleware(
    CORSMiddleware,
    allow_origins=["http://localhost:4200", "http://127.0.0.1:4200"],
    allow_methods=["*"], allow_headers=["*"],
)


class ComputeRequest(BaseModel):
    start_time: str = "1 Jan 2025 00:00:00.000"
    stop_time:  str = "1 Jan 2025 01:00:00.000"


@app.get("/health")
def health() -> dict[str, str]:
    return {"status": "ok"}


@app.get("/models/{filename}")
def get_model(filename: str) -> FileResponse:
    if _stk_models_dir:
        matches = list(_stk_models_dir.rglob(filename))
        if matches:
            return FileResponse(str(matches[0]), media_type="model/gltf-binary")
    raise HTTPException(status_code=404, detail=f"Model '{filename}' not found")


@app.post("/exercises", status_code=201)
def create_exercise() -> dict[str, str]:
    eid = str(uuid.uuid4())
    with _exercises_lock:
        _exercises[eid] = {"status": "created", "czml_path": None}
    return {"exercise_id": eid, "status": "created"}


@app.post("/exercises/{exercise_id}/compute", status_code=202)
async def trigger_compute(exercise_id: str, body: ComputeRequest) -> dict[str, str]:
    with _exercises_lock:
        if exercise_id not in _exercises:
            raise HTTPException(status_code=404, detail="exercise not found")
        ex = _exercises[exercise_id]
        if ex["status"] in ("running", "ready"):
            return {"exercise_id": exercise_id, "status": ex["status"]}
        ex.update(status="running",
                  start_time=body.start_time, stop_time=body.stop_time)

    fut = asyncio.get_running_loop().run_in_executor(
        _pool, functools.partial(_run_compute, exercise_id),
    )
    fut.add_done_callback(lambda f: f.exception() and
                          log.error("executor leaked exception: %s", f.exception()))
    return {"exercise_id": exercise_id, "status": "running"}


def _run_compute(exercise_id: str) -> None:
    from computation_job import run_computation
    with _exercises_lock:
        ex = _exercises[exercise_id]
        start, stop = ex["start_time"], ex["stop_time"]
    out = str(CZML_DIR / f"{exercise_id}.czml")
    try:
        run_computation(exercise_id, start, stop, out)
        with _exercises_lock:
            _exercises[exercise_id].update(status="ready", czml_path=out)
    except Exception as exc:
        with _exercises_lock:
            _exercises[exercise_id]["status"] = f"error: {exc}"
        log.exception("compute failed for %s", exercise_id)


@app.get("/exercises/{exercise_id}/status")
def get_status(exercise_id: str) -> dict[str, str]:
    with _exercises_lock:
        if exercise_id not in _exercises:
            raise HTTPException(status_code=404, detail="exercise not found")
        status = _exercises[exercise_id]["status"]
    return {"exercise_id": exercise_id, "status": status}


@app.get("/exercises/{exercise_id}/czml")
def get_czml(exercise_id: str) -> FileResponse:
    with _exercises_lock:
        if exercise_id not in _exercises:
            raise HTTPException(status_code=404, detail="exercise not found")
        ex = _exercises[exercise_id]
        status = ex["status"]
        czml_path = ex["czml_path"]
    if status != "ready" or not czml_path:
        raise HTTPException(status_code=409, detail=f"computation status: {status}")
    return FileResponse(czml_path, media_type="application/json",
                        headers={"Content-Disposition": f'inline; filename="{exercise_id}.czml"'})


@app.get("/exercises/{exercise_id}/fom-image")
def get_fom_image(exercise_id: str) -> FileResponse:
    with _exercises_lock:
        if exercise_id not in _exercises:
            raise HTTPException(status_code=404, detail="exercise not found")
        status = _exercises[exercise_id]["status"]
    if status != "ready":
        raise HTTPException(status_code=409, detail=f"computation status: {status}")
    for suffix in (".png", ".jpg", ".jpeg"):
        p = FOM_DIR / f"{exercise_id}{suffix}"
        if p.exists():
            return FileResponse(str(p))
    raise HTTPException(status_code=404, detail="FOM image not found")
