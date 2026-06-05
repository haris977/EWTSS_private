"""asyncio TCP command-server + UDP response-sender for drs-bridge.

Per B1.3 design spec, each variant has two ports: a TCP `command` port
(server-side, receives frames from Entity Controllers) and a UDP `response`
port (client-side, sends responses out). This module ships the asyncio
primitives for both.
"""
from __future__ import annotations

import asyncio
from typing import Awaitable, Callable

CommandHandler = Callable[[bytes], Awaitable[None]]


async def start_command_server(host: str, port: int, handler: CommandHandler) -> asyncio.Server:
    """Start a TCP server that calls `handler(bytes)` for each frame received."""

    async def _on_connect(reader: asyncio.StreamReader, writer: asyncio.StreamWriter) -> None:
        try:
            while not reader.at_eof():
                data = await reader.read(4096)
                if not data:
                    break
                await handler(data)
        finally:
            writer.close()
            try:
                await writer.wait_closed()
            except Exception:
                pass

    server = await asyncio.start_server(_on_connect, host, port)
    return server


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
