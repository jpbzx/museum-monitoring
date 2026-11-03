#!/usr/bin/env python3
"""Envia comandos para uma board, como faria o dashboard (tópico <base>/config/set).

Exemplos:
    .env/bin/python publisher.py period 30                 # amostrar de 30 em 30 s (5..3600)
    .env/bin/python publisher.py power balanced            # performance | balanced | ultra_low
    .env/bin/python publisher.py sensor sht4x off          # ativar/desativar um sensor
    .env/bin/python publisher.py report                    # pedir à board que reporte a configuração
    .env/bin/python publisher.py --device node-02 period 10
    .env/bin/python publisher.py raw '{"power_mode":"turbo"}'   # texto verbatim, para testar rejeições
"""

import argparse
import json
import threading

from mqtt_common import add_connection_args, add_node_args, die, make_client, topic_base

# Limites do contrato: firmware/components/app_types/include/app_types.h
SAMPLE_PERIOD_MIN_S = 5
SAMPLE_PERIOD_MAX_S = 3600
SENSOR_NAME_MAX_LEN = 15
POWER_MODES = ("performance", "balanced", "ultra_low")


def period_arg(text: str) -> int:
    value = int(text)
    if not SAMPLE_PERIOD_MIN_S <= value <= SAMPLE_PERIOD_MAX_S:
        raise argparse.ArgumentTypeError(f"deve estar entre {SAMPLE_PERIOD_MIN_S} e {SAMPLE_PERIOD_MAX_S}")
    return value


def sensor_name_arg(text: str) -> str:
    if not 1 <= len(text) <= SENSOR_NAME_MAX_LEN:
        raise argparse.ArgumentTypeError(f"o nome deve ter 1 a {SENSOR_NAME_MAX_LEN} caracteres")
    return text


def build_payload(args: argparse.Namespace) -> str:
    if args.command == "period":
        return json.dumps({"sample_period_s": args.seconds})
    if args.command == "power":
        return json.dumps({"power_mode": args.mode})
    if args.command == "sensor":
        return json.dumps({"sensor": {"name": args.name, "enabled": args.state == "on"}})
    if args.command == "report":
        return json.dumps({"report_config": True})
    return args.payload     # raw: sem validar, de propósito


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    add_connection_args(parser)
    add_node_args(parser, default=None)     # None = valores por omissão do nó (isep-dee/sala-01/node-01)

    sub = parser.add_subparsers(dest="command", required=True)
    p = sub.add_parser("period", help="alterar o período de amostragem")
    p.add_argument("seconds", type=period_arg)
    p = sub.add_parser("power", help="alterar o modo de energia")
    p.add_argument("mode", choices=POWER_MODES)
    p = sub.add_parser("sensor", help="ativar/desativar um sensor")
    p.add_argument("name", type=sensor_name_arg)
    p.add_argument("state", choices=("on", "off"))
    sub.add_parser("report", help="pedir o reporte da configuração")
    p = sub.add_parser("raw", help="enviar um payload tal como escrito (sem validação)")
    p.add_argument("payload")

    return parser.parse_args()


def main() -> None:
    args = parse_args()
    topic = f"{topic_base(args.site, args.room, args.device)}/config/set"
    payload = build_payload(args)

    client = make_client(args, "pub")
    connected = threading.Event()
    refused = []

    def on_connect(client, userdata, flags, reason_code, properties):
        if reason_code.is_failure:
            refused.append(str(reason_code))
        connected.set()

    client.on_connect = on_connect
    try:
        client.connect(args.host, args.port, keepalive=30)
    except OSError as exc:
        die(f"não consegui ligar a {args.host}:{args.port} ({exc})")

    client.loop_start()
    try:
        if not connected.wait(timeout=5):
            die("o broker não respondeu ao pedido de ligação")
        if refused:
            die(f"ligação recusada pelo broker: {refused[0]}")

        info = client.publish(topic, payload, qos=1)
        info.wait_for_publish(timeout=5)
        if not info.is_published():
            die("o broker não confirmou a publicação (PUBACK)")
        print(f"enviado para {topic}\n  {payload}")
    finally:
        client.loop_stop()
        client.disconnect()


if __name__ == "__main__":
    main()
