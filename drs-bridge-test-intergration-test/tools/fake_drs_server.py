#!/usr/bin/env python3
"""Minimal fake DRS server — completes the full duplex loop for dev/testing.

Full duplex flow:
  Device --TCP--> Bridge --Kafka(hw.*.telemetry)--> [this server]
    ^                                                      |
    |                                                      v
    +<--UDP-- Bridge <--Kafka(drs.commands)---------------+

Logic per telemetry message:
  CMD frame  (device requesting something from DRS):
    -> publish RESP back to drs.commands  (unit N -> N+1, status=0, no payload)

  RESP frame (device pushing status/telemetry to DRS):
    -> log the data; no reply needed (device initiated, not requesting)

Usage:
  python tools/fake_drs_server.py
  python tools/fake_drs_server.py --kafka localhost:9092 --verbose
"""
from __future__ import annotations

import argparse
import asyncio
import json
import logging

TELEMETRY_TOPICS = [
    "hw.dp_ecm_hf.telemetry",
    "hw.dp_ecm_vu.telemetry",
]
COMMANDS_TOPIC = "drs.commands"

CYAN   = "\033[96m"
GREEN  = "\033[92m"
YELLOW = "\033[93m"
RESET  = "\033[0m"

logging.basicConfig(
    level=logging.INFO,
    format="%(asctime)s  %(message)s",
    datefmt="%H:%M:%S",
)
log = logging.getLogger("fake-drs")


def _cmd_reply(telemetry_msg: dict) -> dict | None:
    """Build a drs.commands payload in response to a CMD telemetry frame.

    CMD unit N → RESP unit N+1  (SDFC convention).
    Returns None if the telemetry frame is not a CMD (no reply needed).
    """
    frame = telemetry_msg.get("frame", {})
    if frame.get("frame_type") != "command":
        return None

    return {
        "instance_id": telemetry_msg["instance_id"],
        "variant":     telemetry_msg["variant"],
        "group_id":    frame["group_id"],
        "unit_id":     frame["unit_id"] + 1,   # SDFC: RESP = CMD unit + 1
        "status":      0,
        "payload_hex": "",
    }


async def run(kafka: str, verbose: bool) -> None:
    from aiokafka import AIOKafkaConsumer, AIOKafkaProducer

    producer = AIOKafkaProducer(bootstrap_servers=kafka)
    await producer.start()

    consumer = AIOKafkaConsumer(
        *TELEMETRY_TOPICS,
        bootstrap_servers=kafka,
        auto_offset_reset="latest",
        group_id="fake-drs-server",
        enable_auto_commit=True,
    )
    await consumer.start()

    log.info("fake-drs-server started")
    log.info("  consuming : %s", ", ".join(TELEMETRY_TOPICS))
    log.info("  publishing: %s", COMMANDS_TOPIC)
    print("-" * 60, flush=True)

    cmd_count  = 0
    resp_count = 0

    try:
        async for msg in consumer:
            try:
                data  = json.loads(msg.value)
                frame = data.get("frame", {})
                iid   = data.get("instance_id", "?")
                g     = frame.get("group_id", "?")
                u     = frame.get("unit_id",  "?")
                ft    = frame.get("frame_type", "?")
                sz    = frame.get("message_size_bytes", 0)

                reply = _cmd_reply(data)

                if reply:
                    cmd_count += 1
                    payload = json.dumps(reply).encode()
                    await producer.send_and_wait(
                        COMMANDS_TOPIC,
                        value=payload,
                        key=iid.encode(),
                    )
                    print(
                        f"{CYAN}[CMD  {g}/{u}]{RESET}  {iid}"
                        f"  -> replied RESP {g}/{u+1} to {COMMANDS_TOPIC}",
                        flush=True,
                    )
                    if verbose:
                        print(f"  command payload: {json.dumps(reply, indent=4)}", flush=True)
                else:
                    resp_count += 1
                    detail = ""
                    if sz > 0:
                        extra = {k: v for k, v in frame.items()
                                 if k not in {"hw","frame_type","group_id","unit_id",
                                              "message_size_bytes","status","status_name"}}
                        if extra:
                            detail = "  " + "  ".join(
                                f"{k}={v:.2f}" if isinstance(v, float) else f"{k}={v}"
                                for k, v in list(extra.items())[:4]
                            )
                    print(
                        f"{GREEN}[RESP {g}/{u}]{RESET}  {iid}"
                        f"  {sz}B telemetry{detail}",
                        flush=True,
                    )
                    if verbose:
                        print(f"  {json.dumps(data, indent=4)}", flush=True)

            except Exception as exc:
                log.exception("dispatch error: %s", exc)

    except asyncio.CancelledError:
        pass
    finally:
        print("-" * 60, flush=True)
        log.info("shutting down  cmd_replies=%d  telemetry_logged=%d", cmd_count, resp_count)
        await consumer.stop()
        await producer.stop()


def main() -> None:
    p = argparse.ArgumentParser(description="Fake DRS server for drs-bridge dev")
    p.add_argument("--kafka",   default="localhost:9092")
    p.add_argument("--verbose", action="store_true", help="print full JSON for every message")
    args = p.parse_args()
    try:
        asyncio.run(run(args.kafka, args.verbose))
    except KeyboardInterrupt:
        pass


if __name__ == "__main__":
    main()
