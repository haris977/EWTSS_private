"""asyncio TCP command-server (framing probation) + UDP response-sender for
drs-bridge.

Per the B1.43 design, each enabled instance has a TCP `command` server
(receives frames from an Entity Controller) and a UDP `response` sender. Each
inbound connection is PROVISIONAL until the variant's frame detector extracts a
valid frame; connections that never frame within a byte ceiling or idle timeout
are closed and reported (framing-probation identity guard).
"""

from __future__ import annotations

import asyncio
from typing import Awaitable, Callable, Optional

# Detector: (rc, frame_bytes). rc == 0 => a complete frame was extracted.
FrameDetector = Callable[[bytes], "tuple[int, bytes | None]"]
# Called with (instance_id, frame_bytes) for each extracted frame.
FrameHandler = Callable[[str, bytes], Optional[Awaitable[None]]]
# Called with (instance_id, reason) when probation rejects a connection.
RejectHandler = Callable[[str, str], None]


async def probation_connection(
    reader,
    writer,
    *,
    instance_id: str,
    detector: FrameDetector,
    on_frame: FrameHandler,
    garbage_ceiling: int,
    idle_timeout: float,
    on_reject: RejectHandler | None = None,
    on_disconnect: Callable[[str], None] | None = None,
) -> None:
    """Handle one inbound connection under framing probation.

    The connection is PROVISIONAL until `detector` extracts the first valid
    frame. Until then: buffer bytes, retrying the detector each read. If the
    buffer exceeds `garbage_ceiling` with no frame, or no bytes arrive within
    `idle_timeout`, close and report FRAMING_MISMATCH_*. Once a frame is
    extracted, probation clears; subsequent frames flow to `on_frame` and the
    idle timeout no longer applies (a quiet but valid peer is fine).
    """
    buffer = bytearray()
    cleared = False
    try:
        while True:
            if not cleared:
                try:
                    data = await asyncio.wait_for(reader.read(4096), timeout=idle_timeout)
                except asyncio.TimeoutError:
                    if on_reject:
                        on_reject(instance_id, "FRAMING_MISMATCH_IDLE")
                    return
            else:
                data = await reader.read(4096)
            if not data:
                return  # EOF
            buffer += data
            # Drain as many complete frames as the buffer holds.
            while True:
                rc, frame = detector(bytes(buffer))
                if rc != 0 or frame is None:
                    break
                cleared = True
                # TODO(data-plane): exact resync offset from extract_frame.
                del buffer[: len(frame)]
                result = on_frame(instance_id, frame)
                if result is not None:
                    await result
            if not cleared and len(buffer) > garbage_ceiling:
                if on_reject:
                    on_reject(instance_id, "FRAMING_MISMATCH_CEILING")
                return
    finally:
        writer.close()
        try:
            await writer.wait_closed()
        except Exception:
            pass
        if on_disconnect:
            on_disconnect(instance_id)


async def start_command_server(
    host: str,
    port: int,
    *,
    instance_id: str,
    detector: FrameDetector,
    on_frame: FrameHandler,
    garbage_ceiling: int,
    idle_timeout: float,
    on_connect: Callable[[str], None] | None = None,
    on_reject: RejectHandler | None = None,
    on_disconnect: Callable[[str], None] | None = None,
) -> asyncio.Server:
    """Start a TCP server whose connections run under framing probation."""

    async def _handle(reader, writer):
        if on_connect:
            on_connect(instance_id)
        await probation_connection(
            reader,
            writer,
            instance_id=instance_id,
            detector=detector,
            on_frame=on_frame,
            garbage_ceiling=garbage_ceiling,
            idle_timeout=idle_timeout,
            on_reject=on_reject,
            on_disconnect=on_disconnect,
        )

    return await asyncio.start_server(_handle, host, port)


class UdpSender:
    """Async wrapper around a UDP datagram endpoint pointed at one peer.

    Satisfies the `Sender` Protocol (`async send(payload: bytes) -> None`).
    """

    def __init__(self, transport: asyncio.DatagramTransport, peer: tuple[str, int]) -> None:
        self._transport = transport
        self._peer = peer

    @classmethod
    async def connect(cls, host: str, port: int) -> "UdpSender":
        loop = asyncio.get_running_loop()
        transport, _ = await loop.create_datagram_endpoint(
            asyncio.DatagramProtocol, remote_addr=(host, port)
        )
        return cls(transport, (host, port))

    async def send(self, payload: bytes) -> None:
        self._transport.sendto(payload)

    async def close(self) -> None:
        self._transport.close()
