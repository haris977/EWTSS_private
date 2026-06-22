"""AUS-C2 HTTP transport layer.

Authenticates against the AUS-C2 REST API and polls GET /api/vX.Y/system,
yielding the raw JSON response bytes on each cycle.

JSON parsing is intentionally NOT done here — the raw bytes are passed to
the C++ aus.dll which implements extract_frame / parse_message per the
standard sdfc ABI.  This keeps all protocol logic in the DLL.

Limitations:
  - Pull model only (default 1-second poll). Sub-second events are not visible.
  - Token lifetime is unspecified; re-auth is triggered automatically on 401.
  - During active RF jamming, devices_info goes empty — expected, not a bug.
"""

from __future__ import annotations

import asyncio
import base64
import logging
from dataclasses import dataclass
from typing import AsyncIterator, Optional

import aiohttp

logger = logging.getLogger(__name__)


@dataclass
class AusConfig:
    host: str                       # AUS-C2 IP, e.g. "192.168.23.60"
    port: int = 5000
    username: str = "admin"
    password: str = "admin"
    poll_interval_s: float = 1.0
    api_version: str = "v4.2"
    connect_timeout_s: float = 5.0
    read_timeout_s: float = 10.0

    @property
    def base_url(self) -> str:
        return f"http://{self.host}:{self.port}/api/{self.api_version}"


class AusPoller:
    """Authenticates with AUS-C2 and continuously polls /system.

    Yields raw JSON bytes — callers pass them directly to the C++ DLL.

    Usage:
        async for raw_bytes in poller.poll_raw():
            ...
    """

    def __init__(self, config: AusConfig) -> None:
        self._cfg = config
        self._token: Optional[str] = None
        self._session: Optional[aiohttp.ClientSession] = None

    async def poll_raw(self) -> AsyncIterator[bytes]:
        """Async generator — yields raw JSON bytes from each /system poll."""
        timeout = aiohttp.ClientTimeout(
            connect=self._cfg.connect_timeout_s,
            total=self._cfg.read_timeout_s,
        )
        async with aiohttp.ClientSession(timeout=timeout) as session:
            self._session = session
            await self._authenticate()

            while True:
                try:
                    raw = await self._fetch_system()
                    yield raw
                except aiohttp.ClientResponseError as exc:
                    if exc.status == 401:
                        logger.warning("AUS-C2 token expired — re-authenticating")
                        await self._authenticate()
                    else:
                        logger.error("AUS-C2 HTTP error %s: %s", exc.status, exc.message)
                except aiohttp.ClientConnectionError as exc:
                    backoff = self._cfg.poll_interval_s * 5
                    logger.error("AUS-C2 connection lost: %s — retrying in %.0fs", exc, backoff)
                    await asyncio.sleep(backoff)
                    try:
                        await self._authenticate()
                    except Exception:
                        logger.exception("AUS-C2 re-authentication failed")
                except Exception:
                    logger.exception("AUS poller unexpected error")

                await asyncio.sleep(self._cfg.poll_interval_s)

    async def run_forever(
        self,
        on_raw_bytes,  # Callable[[bytes], Awaitable[None]]
    ) -> None:
        """Drive the poll loop, calling on_raw_bytes for every successful response."""
        async for raw in self.poll_raw():
            try:
                await on_raw_bytes(raw)
            except Exception:
                logger.exception("on_raw_bytes callback raised")

    # ------------------------------------------------------------------

    async def _authenticate(self) -> None:
        assert self._session is not None
        credentials = base64.b64encode(
            f"{self._cfg.username}:{self._cfg.password}".encode()
        ).decode()
        url = f"{self._cfg.base_url}/login"
        logger.debug("AUS-C2 authenticating at %s", url)
        async with self._session.get(
            url,
            headers={"Authorization": f"Basic {credentials}"},
        ) as resp:
            resp.raise_for_status()
            body = await resp.json()
            self._token = body["token"]
            logger.info("AUS-C2 authenticated (host=%s)", self._cfg.host)

    async def _fetch_system(self) -> bytes:
        assert self._session is not None and self._token is not None
        url = f"{self._cfg.base_url}/system"
        async with self._session.get(
            url,
            headers={"Authorization": f"Bearer {self._token}"},
        ) as resp:
            resp.raise_for_status()
            return await resp.read()  # raw bytes — DLL parses
