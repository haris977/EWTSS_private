"""Idempotent provisioning of EWTSS control-plane Kafka topics.

Creates the three known control-plane topics if missing. Safe to re-run.

NOT provisioned here:
  - hw.<variant>.<kind> (data-plane telemetry topics) — these are
    Phase 2 work. Topic naming + partition strategy will be designed
    against the first real IRS, not pre-committed here.

Usage:
  python infrastructure/kafka/create-topics.py
  python infrastructure/kafka/create-topics.py --bootstrap localhost:9092
"""
from __future__ import annotations

import argparse
import asyncio
import sys


CONTROL_PLANE_TOPICS = [
    # name, partitions, replication_factor, topic_configs
    ("drs.control", 1, 1, {}),       # variant.registered + future bridge -> server events
    ("system.timesync", 1, 1, {}),   # SyncStateEngine state transitions
    ("system.health", 1, 1, {}),     # per-variant health events from drs-bridge
    ("drs.roster", 1, 1, {"cleanup.policy": "compact"}),  # active-roster snapshot, key 'active'
]


async def create_topics(bootstrap: str) -> int:
    # Imported here (not at module top) so CONTROL_PLANE_TOPICS stays inspectable
    # without aiokafka installed (e.g. config unit test on a bare interpreter).
    from aiokafka.admin import AIOKafkaAdminClient, NewTopic
    from aiokafka.errors import TopicAlreadyExistsError

    admin = AIOKafkaAdminClient(bootstrap_servers=bootstrap)
    await admin.start()
    try:
        existing = set(await admin.list_topics())
        to_create = [
            NewTopic(name, num_partitions=parts, replication_factor=rf,
                     topic_configs=configs or None)
            for name, parts, rf, configs in CONTROL_PLANE_TOPICS
            if name not in existing
        ]
        if not to_create:
            print(f"All {len(CONTROL_PLANE_TOPICS)} control-plane topics already exist; nothing to do.")
            return 0
        try:
            await admin.create_topics(new_topics=to_create)
        except TopicAlreadyExistsError:
            # Race: someone else created it between list_topics and create_topics.
            pass
        for nt in to_create:
            print(f"created topic: {nt.name} (partitions={nt.num_partitions}, rf={nt.replication_factor})")
        return 0
    finally:
        await admin.close()


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--bootstrap", default="localhost:9092",
                        help="Kafka bootstrap server (default: localhost:9092)")
    args = parser.parse_args()
    return asyncio.run(create_topics(args.bootstrap))


if __name__ == "__main__":
    sys.exit(main())
