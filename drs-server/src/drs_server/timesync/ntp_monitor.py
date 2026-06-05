"""Reads Meinberg NTP daemon state via `ntpq -c "rv 0 ..."`.

Returns NtpSample with offset_ms, jitter_ms, stratum. Used by SyncStateEngine.
Per B1.3 design spec §4 and §7.1.
"""
import asyncio
import re
from dataclasses import dataclass
from datetime import datetime, timezone
from typing import Optional


@dataclass(frozen=True)
class NtpSample:
    """One observation of local NTP daemon state."""
    offset_ms: float
    jitter_ms: float
    stratum: int
    sampled_at: datetime
    peer: Optional[str] = None


class NtpMonitor:
    """Subprocess wrapper around `ntpq` for sampling local NTP state."""

    _RV_PATTERN = re.compile(
        r"stratum=(\d+).*?offset=([\-\d\.]+).*?sys_jitter=([\-\d\.]+)",
        re.DOTALL,
    )
    _REFID_PATTERN = re.compile(r"refid=([^\s,]+)")

    def __init__(self, ntpq_path: str = r"C:\Program Files\NTP\bin\ntpq.exe"):
        self._ntpq_path = ntpq_path

    async def sample(self) -> NtpSample:
        """Run `ntpq -c "rv 0 ..."` and parse the output."""
        proc = await asyncio.create_subprocess_exec(
            self._ntpq_path,
            "-c", "rv 0 offset,jitter,stratum,refid,sys_jitter",
            stdout=asyncio.subprocess.PIPE,
            stderr=asyncio.subprocess.PIPE,
        )
        stdout, stderr = await proc.communicate()
        if proc.returncode != 0:
            raise RuntimeError(
                f"ntpq returned {proc.returncode}: {stderr.decode().strip()}"
            )
        return self._parse_rv_output(stdout.decode())

    def _parse_rv_output(self, output: str) -> NtpSample:
        match = self._RV_PATTERN.search(output)
        if not match:
            raise ValueError(f"Could not parse ntpq output: {output[:200]}")
        stratum = int(match.group(1))
        offset_ms = float(match.group(2))
        jitter_ms = float(match.group(3))

        peer = None
        refid_match = self._REFID_PATTERN.search(output)
        if refid_match:
            peer = refid_match.group(1)

        return NtpSample(
            offset_ms=offset_ms,
            jitter_ms=jitter_ms,
            stratum=stratum,
            sampled_at=datetime.now(timezone.utc),
            peer=peer,
        )
