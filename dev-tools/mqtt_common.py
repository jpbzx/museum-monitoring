"""Parâmetros e funções partilhados por publisher.py e subscriber.py.

Os valores por omissão do nó (site/room/device) coincidem com os de
firmware/main/app_config.h; o formato das mensagens está documentado em
firmware/components/net_manager/include/net_codec.h.

Ligação ao broker: --host/--port/--username, ou as variáveis de ambiente
MQTT_HOST, MQTT_PORT, MQTT_USERNAME. A password só vem de MQTT_PASSWORD
(um argumento de linha de comandos ficaria visível no `ps`).
"""

import argparse
import os
import sys

import paho.mqtt.client as mqtt

DEFAULT_SITE = "isep-dee"
DEFAULT_ROOM = "sala-01"
DEFAULT_DEVICE = "node-01"


def add_connection_args(parser: argparse.ArgumentParser) -> None:
    parser.add_argument("--host", default=os.environ.get("MQTT_HOST", "localhost"),
                        help="endereço do broker (omissão: $MQTT_HOST ou localhost)")
    parser.add_argument("--port", type=int, default=int(os.environ.get("MQTT_PORT", "1883")),
                        help="porta do broker (omissão: $MQTT_PORT ou 1883)")
    parser.add_argument("--username", default=os.environ.get("MQTT_USERNAME"),
                        help="utilizador do broker (omissão: $MQTT_USERNAME; a password vem de $MQTT_PASSWORD)")


def add_node_args(parser: argparse.ArgumentParser, default: str) -> None:
    """--site/--room/--device. `default` é o valor por omissão de cada um (ex.: '+' para escutar todos)."""
    parser.add_argument("--site", default=DEFAULT_SITE if default is None else default)
    parser.add_argument("--room", default=DEFAULT_ROOM if default is None else default)
    parser.add_argument("--device", default=DEFAULT_DEVICE if default is None else default)


def topic_base(site: str, room: str, device: str) -> str:
    return f"museum/{site}/{room}/{device}"


def make_client(args: argparse.Namespace, role: str) -> mqtt.Client:
    # client_id único: dois clientes com o mesmo id expulsam-se um ao outro no broker.
    client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2, client_id=f"dev-{role}-{os.getpid()}")
    if args.username:
        client.username_pw_set(args.username, os.environ.get("MQTT_PASSWORD"))
    return client


def die(message: str) -> None:
    print(f"erro: {message}", file=sys.stderr)
    sys.exit(1)
