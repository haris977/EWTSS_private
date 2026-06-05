import asyncio

import pytest

from drs_bridge.transport import UdpSender, start_command_server, probation_connection


def _ref_detector(buf: bytes):
    # mimic the reference parser: 0xAA magic, len byte, then payload
    for i in range(len(buf) - 1):
        if buf[i] == 0xAA:
            need = 2 + buf[i + 1]
            if i + need <= len(buf):
                return 0, bytes(buf[i : i + need])
            return -1, None
    return -1, None


class _FakeReader:
    def __init__(self, chunks):
        self._chunks = list(chunks)

    async def read(self, n):
        if self._chunks:
            return self._chunks.pop(0)
        return b""  # EOF


class _FakeWriter:
    def __init__(self):
        self.closed = False

    def close(self):
        self.closed = True

    async def wait_closed(self):
        return None


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
async def test_probation_accepts_segmented_frame():
    frames = []
    reader = _FakeReader([b"\xaa\x06\x01\x00", b"\x00\x00\x00\x00"])  # split valid frame
    writer = _FakeWriter()
    await probation_connection(
        reader,
        writer,
        instance_id="rdfs#1",
        detector=_ref_detector,
        on_frame=lambda iid, f: frames.append((iid, f)),
        garbage_ceiling=4096,
        idle_timeout=1.0,
    )
    assert frames and frames[0][0] == "rdfs#1"
    assert frames[0][1] == bytes([0xAA, 0x06, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00])


@pytest.mark.asyncio
async def test_probation_closes_on_garbage_ceiling():
    on_close = []
    reader = _FakeReader([b"\x00" * 5000])  # never frames, exceeds ceiling
    writer = _FakeWriter()
    await probation_connection(
        reader,
        writer,
        instance_id="rdfs#1",
        detector=_ref_detector,
        on_frame=lambda iid, f: None,
        garbage_ceiling=4096,
        idle_timeout=1.0,
        on_reject=lambda iid, reason: on_close.append(reason),
    )
    assert writer.closed
    assert on_close == ["FRAMING_MISMATCH_CEILING"]


@pytest.mark.asyncio
async def test_probation_closes_on_idle_timeout():
    on_close = []

    class _SlowReader:
        async def read(self, n):
            await asyncio.sleep(10)  # never returns within idle_timeout
            return b""

    writer = _FakeWriter()
    await probation_connection(
        _SlowReader(),
        writer,
        instance_id="rdfs#1",
        detector=_ref_detector,
        on_frame=lambda iid, f: None,
        garbage_ceiling=4096,
        idle_timeout=0.05,
        on_reject=lambda iid, reason: on_close.append(reason),
    )
    assert writer.closed
    assert on_close == ["FRAMING_MISMATCH_IDLE"]


@pytest.mark.asyncio
async def test_command_server_routes_first_frame_with_instance_id():
    frames = []
    server = await start_command_server(
        "127.0.0.1",
        0,
        instance_id="rdfs#1",
        detector=_ref_detector,
        on_frame=lambda iid, f: frames.append((iid, f)),
        garbage_ceiling=4096,
        idle_timeout=1.0,
    )
    port = server.sockets[0].getsockname()[1]
    try:
        reader, writer = await asyncio.open_connection("127.0.0.1", port)
        writer.write(bytes([0xAA, 0x06, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00]))
        await writer.drain()
        await asyncio.sleep(0.05)
        writer.close()
        assert frames == [("rdfs#1", bytes([0xAA, 0x06, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00]))]
    finally:
        server.close()
        await server.wait_closed()
