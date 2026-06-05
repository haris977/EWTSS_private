from __future__ import annotations
import logging

log = logging.getLogger(__name__)


def run_computation(
    exercise_id: str,
    start_time: str,
    stop_time: str,
    output_path: str,
    entities: list[dict],
) -> None:
    from stk_com_service_v13 import get_stk_service

    log.info("computation_job: starting exercise %s", exercise_id)
    svc = get_stk_service()
    svc.build_and_compute(exercise_id, start_time, stop_time, entities)
    svc.export_czml(exercise_id, output_path)
    log.info("computation_job: done, CZML at %s", output_path)
