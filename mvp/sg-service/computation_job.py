"""
Runs inside ProcessPoolExecutor.
Must import its own IStkService — cannot share state with the main process.
"""
from __future__ import annotations

import logging

log = logging.getLogger(__name__)


def run_computation(
    exercise_id: str,
    start_time: str,
    stop_time: str,
    output_path: str,
    scenario_file: str | None = None,
) -> None:
    """
    Called by main.py._run_compute_background via run_in_executor.
    Selects the appropriate STK service, runs computation, writes CZML.

    If scenario_file is set, loads that .sc/.vdf instead of building from
    parameters.  Raises on any error — the caller sets exercise status to
    "error: <msg>".
    """
    from stk_com_service import get_stk_service

    log.info("computation_job: starting exercise %s", exercise_id)
    svc = get_stk_service()
    if scenario_file:
        log.info("computation_job: loading scenario file %s", scenario_file)
        svc.load_scenario(scenario_file)
    else:
        svc.build_and_compute(exercise_id, start_time, stop_time)
    svc.export_czml(exercise_id, output_path)
    log.info("computation_job: done, CZML at %s", output_path)
