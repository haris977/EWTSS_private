"""Watch hw.dp_ecm_hf.telemetry and drs.commands for 20 seconds."""
import asyncio
import json
from aiokafka import AIOKafkaConsumer


async def main():
    consumer = AIOKafkaConsumer(
        "hw.dp_ecm_hf.telemetry",
        "drs.commands",
        bootstrap_servers="localhost:9092",
        auto_offset_reset="latest",
        group_id="debug-watcher-2",
    )
    await consumer.start()
    print("Watching hw.dp_ecm_hf.telemetry + drs.commands  (20s)...")
    print("Run the simulator now in another terminal:")
    print("  python tools/dp_ecm_simulator.py --port 19001 --variant hf")
    print("-" * 60)

    try:
        async for msg in consumer:
            topic = msg.topic
            data  = json.loads(msg.value)
            if topic == "hw.dp_ecm_hf.telemetry":
                frame = data.get("frame", {})
                print(f"[TELEMETRY]  {frame.get('frame_type'):8}  "
                      f"{frame.get('group_id')}/{frame.get('unit_id')}  "
                      f"inst={data.get('instance_id')}")
            else:
                print(f"[COMMANDS ]  {data.get('group_id')}/{data.get('unit_id')}  "
                      f"inst={data.get('instance_id')}  data={data.get('data') or data.get('payload_hex')}")
    except asyncio.CancelledError:
        pass
    finally:
        await consumer.stop()


asyncio.run(asyncio.wait_for(main(), timeout=20))
