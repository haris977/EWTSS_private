"""GET /time/status — returns local NTP state per B1.3 design spec §7.1."""
from datetime import datetime, timezone

from fastapi import APIRouter, Request
from pydantic import BaseModel

router = APIRouter(prefix="/time", tags=["timesync"])


class TimeStatusResponse(BaseModel):
    current_time: datetime
    ntp_offset_ms: float
    ntp_jitter_ms: float
    ntp_peer: str | None
    last_sync: datetime
    status: str  # "healthy" | "warming" | "drift_warn" | "drift_alert" | "sync_lost"


@router.get("/status", response_model=TimeStatusResponse)
async def get_time_status(request: Request) -> TimeStatusResponse:
    monitor = request.app.state.ntp_monitor
    engine = request.app.state.sync_state_engine

    sample = await monitor.sample()
    status = await engine.current_status()

    return TimeStatusResponse(
        current_time=datetime.now(timezone.utc),
        ntp_offset_ms=sample.offset_ms,
        ntp_jitter_ms=sample.jitter_ms,
        ntp_peer=sample.peer,
        last_sync=sample.sampled_at,
        status=status.value,
    )
