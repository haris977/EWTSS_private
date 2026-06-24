#!/usr/bin/env python3
"""Live Kafka watcher — prints decoded telemetry and commands as they arrive.

Usage:
  python tools/kafka_watch.py                     # watch all topics
  python tools/kafka_watch.py --topic hw.dp_ecm_hf.telemetry
  python tools/kafka_watch.py --topic drs.commands
"""
import argparse
import asyncio
import json
import sys

TOPICS = [
    "hw.dp_ecm_hf.telemetry",
    "hw.dp_ecm_vu.telemetry",
    "drs.commands",
]

CYAN  = "\033[96m"
GREEN = "\033[92m"
YELLOW = "\033[93m"
RESET = "\033[0m"

TOPIC_COLOR = {
    "hw.dp_ecm_hf.telemetry": CYAN,
    "hw.dp_ecm_vu.telemetry": GREEN,
    "drs.commands":           YELLOW,
}


def format_msg(topic: str, data: dict) -> str:
    color = TOPIC_COLOR.get(topic, "")
    lines = []
    lines.append(f"{color}[{topic}]{RESET}")
    lines.append(json.dumps(data, indent=2))
    return "\n".join(lines)


async def watch(topics: list[str], kafka: str) -> None:
    from aiokafka import AIOKafkaConsumer

    consumer = AIOKafkaConsumer(
        *topics,
        bootstrap_servers=kafka,
        auto_offset_reset="latest",
        group_id=None,
        enable_auto_commit=False,
    )
    await consumer.start()
    await consumer.seek_to_end()
    print(f"Watching {len(topics)} topic(s) — send frames from the simulator now.\n")
    print(f"Topics: {', '.join(topics)}\n")
    print("-" * 60)

    try:
        while True:
            try:
                batch = await asyncio.wait_for(
                    consumer.getmany(max_records=20), timeout=1.0
                )
                for _tp, messages in batch.items():
                    for msg in messages:
                        try:
                            data = json.loads(msg.value)
                            print(format_msg(msg.topic, data))
                            print()
                        except Exception as exc:
                            print(f"[parse error] {exc}: {msg.value[:80]}")
            except asyncio.TimeoutError:
                pass
    except KeyboardInterrupt:
        print("\nStopped.")
    finally:
        await consumer.stop()


def main() -> None:
    p = argparse.ArgumentParser(description="Watch live Kafka telemetry and command messages")
    p.add_argument("--topic",  default=None, help="Single topic to watch (default: all 3)")
    p.add_argument("--kafka",  default="localhost:9092", help="Kafka bootstrap server")
    args = p.parse_args()

    topics = [args.topic] if args.topic else TOPICS
    asyncio.run(watch(topics, args.kafka))


if __name__ == "__main__":
    main()
