#!/usr/bin/env python3
"""Publish a dev roster snapshot to drs.roster Kafka topic.

Usage:
  python tools/publish_roster.py               # HF on 19001, VU on 19011
  python tools/publish_roster.py --hf-only
  python tools/publish_roster.py --vu-only
  python tools/publish_roster.py --kafka localhost:9092

Publishes once with key=b'active' (the compacted-topic key the bridge expects).
The bridge will pick it up and bind TCP servers on the configured ports.
"""
from __future__ import annotations

import argparse
import asyncio
import json


async def publish(kafka: str, entries: list[dict]) -> None:
    from aiokafka import AIOKafkaProducer

    roster = {
        "roster_id": "dev-roster",
        "version": 1,
        "entries": entries,
    }
    payload = json.dumps(roster).encode()

    producer = AIOKafkaProducer(bootstrap_servers=kafka)
    await producer.start()
    try:
        await producer.send_and_wait("drs.roster", value=payload, key=b"active")
        print(f"Published roster ({len(entries)} entries) to drs.roster")
        for e in entries:
            print(f"  {e['instance_id']}  {e['variant']}  TCP:{e['command']['port']}  UDP:{e['response']['port']}")
    finally:
        await producer.stop()


def _make_entry(instance_id: str, variant: str, cmd_port: int, resp_port: int) -> dict:
    return {
        "instance_id": instance_id,
        "variant": variant,
        "host": "127.0.0.1",
        "command":  {"host": "0.0.0.0", "port": cmd_port,  "protocol": "tcp"},
        "response": {"host": "127.0.0.1", "port": resp_port, "protocol": "udp"},
        "port_source": "allocated",
        "enabled": True,
    }


def main() -> None:
    p = argparse.ArgumentParser(description="Publish dev roster to Kafka")
    p.add_argument("--kafka",   default="localhost:9092")
    p.add_argument("--hf-only", action="store_true")
    p.add_argument("--vu-only", action="store_true")
    args = p.parse_args()

    entries: list[dict] = []
    if not args.vu_only:
        entries.append(_make_entry("dp_ecm_hf#1", "dp_ecm_hf", 19001, 19002))
    if not args.hf_only:
        entries.append(_make_entry("dp_ecm_vu#1", "dp_ecm_vu", 19011, 19012))

    asyncio.run(publish(args.kafka, entries))


if __name__ == "__main__":
    main()
