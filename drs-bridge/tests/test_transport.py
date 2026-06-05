import asyncio

import pytest

from drs_bridge.transport import UdpSender, start_command_server


@pytest.mark.asyncio
async def test_udp_sender_sends_payload():
    # Stand up a tiny in-process UDP receiver on an ephemeral port.
    received: list[bytes] = []

    class _Recv(asyncio.DatagramProtocol):
        def datagram_received(self, data: bytes, addr) -> None:
            received.append(data)

    loop = asyncio.get_running_loop()
    transport, _ = await loop.create_datagram_endpoint(_Recv, local_addr=("127.0.0.1", 0))
    port = transport.get_extra_info("sockname")[1]
    try:
        sender = await UdpSender.connect("127.0.0.1", port)
        await sender.send(b"hello")
        await asyncio.sleep(0.05)
        assert received == [b"hello"]
        await sender.close()
    finally:
        transport.close()


@pytest.mark.asyncio
async def test_command_server_delivers_received_bytes_to_handler():
    received: list[bytes] = []

    async def handler(data: bytes) -> None:
        received.append(data)

    server = await start_command_server("127.0.0.1", 0, handler)
    port = server.sockets[0].getsockname()[1]
    try:
        reader, writer = await asyncio.open_connection("127.0.0.1", port)
        writer.write(b"frame")
        await writer.drain()
        writer.close()
        await writer.wait_closed()
        await asyncio.sleep(0.05)
        assert received and received[0].startswith(b"frame")
    finally:
        server.close()
        await server.wait_closed()
