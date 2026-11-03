#!/usr/bin/env python3
"""Escuta o que as boards publicam: telemetry, status (online/offline) e config/state.

Exemplos:
    .env/bin/python subscriber.py                       # todas as boards (museum/+/+/+/...)
    .env/bin/python subscriber.py --device node-01      # só uma board
    .env/bin/python subscriber.py --host 192.168.1.10
"""

import argparse
import json
from datetime import datetime

import paho.mqtt.client as mqtt

from mqtt_common import add_connection_args, add_node_args, die, make_client, topic_base

CHANNELS = ("telemetry", "status", "config/state")

# chave JSON -> (rótulo, unidade); a ordem é a de apresentação
MEASUREMENTS = {
    "temperature_c": ("T", "°C"),
    "humidity_pct": ("RH", "%"),
    "illuminance_lux": ("lux", ""),
    "co2_ppm": ("CO2", "ppm"),
    "voc_index": ("VOC", ""),
    "battery_mv": ("bat", "mV"),
}


def format_telemetry(payload: str) -> str:
    data = json.loads(payload)
    if not isinstance(data, dict):
        raise ValueError("não é um objeto JSON")
    parts = [f"seq={data.get('seq', '?')}"]
    for key, (label, unit) in MEASUREMENTS.items():
        if key in data:
            parts.append(f"{label}={data[key]}{unit}")
    if len(parts) == 1:
        parts.append("(sem medições válidas)")
    return "  ".join(parts)


def format_status(payload: str, retained: bool) -> str:
    text = {"online": "ONLINE", "offline": "OFFLINE"}.get(payload, f"estado desconhecido: {payload!r}")
    return f"{text}{'  (retained)' if retained else ''}"


def format_message(topic: str, payload: str, retained: bool) -> str:
    # museum/<site>/<room>/<device>/<canal>; o canal pode ter barras (config/state)
    parts = topic.split("/")
    who = "/".join(parts[1:4])
    channel = "/".join(parts[4:])

    try:
        if channel == "telemetry":
            body = format_telemetry(payload)
        elif channel == "status":
            body = format_status(payload, retained)
        else:
            body = payload
    except (ValueError, TypeError) as exc:      # json.JSONDecodeError é um ValueError
        body = f"payload inválido ({exc}): {payload!r}"

    return f"{datetime.now():%H:%M:%S}  {who:<28} {channel:<13} {body}"


def on_connect(client, userdata, flags, reason_code, properties):
    if reason_code.is_failure:
        print(f"ligação recusada pelo broker: {reason_code}")
        return
    print(f"ligado ao broker; a escutar {userdata['pattern']}/{{{', '.join(CHANNELS)}}}  (Ctrl-C para sair)")
    # (re)subscrever aqui: numa reconexão o broker pode ter perdido as subscrições
    for channel in CHANNELS:
        client.subscribe(f"{userdata['pattern']}/{channel}", qos=1)


def on_message(client, userdata, msg):
    payload = msg.payload.decode("utf-8", errors="replace")
    print(format_message(msg.topic, payload, bool(msg.retain)), flush=True)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    add_connection_args(parser)
    add_node_args(parser, default="+")      # '+' = qualquer
    args = parser.parse_args()

    client = make_client(args, "sub")
    client.user_data_set({"pattern": topic_base(args.site, args.room, args.device)})
    client.on_connect = on_connect
    client.on_message = on_message

    try:
        client.connect(args.host, args.port, keepalive=60)
    except OSError as exc:
        die(f"não consegui ligar a {args.host}:{args.port} ({exc})")

    try:
        client.loop_forever()       # reconecta sozinho se o broker cair
    except KeyboardInterrupt:
        print()
    finally:
        client.disconnect()


if __name__ == "__main__":
    main()
