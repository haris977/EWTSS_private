import json
import pytest

from drs_bridge.health_publisher import HealthPublisher


class _CapturingProducer:
    def __init__(self):
        self.sent = []

    async def send_and_wait(self, topic, value):
        self.sent.append((topic, json.loads(value.decode("utf-8"))))


@pytest.mark.asyncio
async def test_instance_health_events_shape():
    prod = _CapturingProducer()
    hp = HealthPublisher(prod)
    await hp.publish("instance.connected", {"instance_id": "rdfs#1"})
    await hp.publish(
        "instance.framing_mismatch", {"instance_id": "rdfs#1", "reason": "FRAMING_MISMATCH_CEILING"}
    )
    await hp.publish("instance.failed_bind", {"instance_id": "rdfs#9", "reason": "BIND_ERROR"})
    topics = {t for t, _ in prod.sent}
    assert topics == {"system.health"}
    events = [b["event"] for _, b in prod.sent]
    assert events == ["instance.connected", "instance.framing_mismatch", "instance.failed_bind"]
    assert all("instance_id" in b for _, b in prod.sent)
