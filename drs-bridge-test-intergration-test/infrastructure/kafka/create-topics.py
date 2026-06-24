"""Idempotent provisioning of drs-bridge Kafka topics.

Creates all control-plane and dp_ecm data-plane topics if missing.
Safe to re-run; existing topics are skipped without error.

Topic inventory
---------------
Control-plane (shared with drs-server):
  drs.control          — drs-bridge -> drs-server events (variant.registered, ...)
  system.timesync      — SyncStateEngine state transitions (published by drs-server)
  system.health        — per-instance / system health events (tick-lag, hw errors)
  drs.roster           — compacted active-roster snapshot; key 'active' (drs-server -> drs-bridge)

Data-plane (hw.<variant>.<kind>):
  hw.dp_ecm_hf.telemetry — decoded response frames from DP-ECM-1071 HF instances
  hw.dp_ecm_vu.telemetry — decoded response frames from DP-ECM-1074 VU instances

Usage:
  python infrastructure/kafka/create-topics.py
  python infrastructure/kafka/create-topics.py --bootstrap localhost:9092
"""
from __future__ import annotations

import argparse
import asyncio
import sys


# (name, partitions, replication_factor, topic_configs)
CONTROL_PLANE_TOPICS = [
    ("drs.control",    1, 1, {}),
    ("system.timesync", 1, 1, {}),
    ("system.health",  1, 1, {}),
    # compacted: single key 'active', latest retained message = live roster
    ("drs.roster",     1, 1, {"cleanup.policy": "compact"}),
    # drs-server -> drs-bridge: JSON command messages; bridge encodes to binary and sends via UDP
    ("drs.commands",   1, 1, {}),
]

# Data-plane telemetry topics — one per dp_ecm variant.
# Keyed by instance_id for per-instance partition affinity.
# Partition count = 1 for dev; scale to match instance count in production.
DATA_PLANE_TOPICS = [
    ("hw.dp_ecm_hf.telemetry", 1, 1, {}),
    ("hw.dp_ecm_vu.telemetry", 1, 1, {}),
]

ALL_TOPICS = CONTROL_PLANE_TOPICS + DATA_PLANE_TOPICS


async def create_topics(bootstrap: str) -> int:
    from aiokafka.admin import AIOKafkaAdminClient, NewTopic
    from aiokafka.errors import TopicAlreadyExistsError

    admin = AIOKafkaAdminClient(bootstrap_servers=bootstrap)
    await admin.start()
    try:
        existing = set(await admin.list_topics())
        to_create = [
            NewTopic(name, num_partitions=parts, replication_factor=rf,
                     topic_configs=configs or None)
            for name, parts, rf, configs in ALL_TOPICS
            if name not in existing
        ]
        if not to_create:
            print(f"All {len(ALL_TOPICS)} topics already exist; nothing to do.")
            return 0
        try:
            await admin.create_topics(new_topics=to_create)
        except TopicAlreadyExistsError:
            pass
        for nt in to_create:
            print(f"created  {nt.name}  (partitions={nt.num_partitions}, rf={nt.replication_factor})")
        skipped = len(ALL_TOPICS) - len(to_create)
        if skipped:
            print(f"skipped  {skipped} topic(s) already present")
        return 0
    finally:
        await admin.close()


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--bootstrap", default="localhost:9092",
                        help="Kafka bootstrap server (default: localhost:9092)")
    args = parser.parse_args()
    return asyncio.run(create_topics(args.bootstrap))


if __name__ == "__main__":
    sys.exit(main())
