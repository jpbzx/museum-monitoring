"""Parâmetros e funções partilhados por publisher.py e subscriber.py.

Os valores por omissão do nó (site/room/device) coincidem com os de
firmware/main/app_config.h; o formato das mensagens está documentado em
firmware/components/net_manager/include/net_codec.h.

Ligação ao broker — tem de coincidir com o que foi introduzido no formulário de
provisioning da board (URI, username, password; ficam na NVS):
  --uri mqtt://host:porta   o mesmo URI da board (ou $MQTT_URI); tem prioridade sobre --host/--port
  --host/--port             alternativa (ou $MQTT_HOST, $MQTT_PORT)
  --username                ou $MQTT_USERNAME
A password só vem de $MQTT_PASSWORD (um argumento ficaria visível no `ps`).
Sem username não se envia autenticação, tal como na board (username vazio).
mqtts:// não é suportado (a board também não, por agora).
"""

import argparse
import os
import sys
from urllib.parse import urlparse

import paho.mqtt.client as mqtt

DEFAULT_SITE = "isep-dee"
DEFAULT_ROOM = "sala-01"
DEFAULT_DEVICE = "node-01"

# Atalhos para brokers conhecidos; qualquer outro valor é usado como endereço.
HOST_ALIASES = {
    "dev": "broker.emqx.io",    # broker público da EMQX (sem autenticação, dados visíveis a todos)
}


def resolve_host(text: str) -> str:
    return HOST_ALIASES.get(text, text)


def parse_broker_uri(text: str) -> tuple[str, int]:
    """'mqtt://host[:porta]' (formato do formulário da board) -> (host, porta); porta omissa = 1883."""
    uri = urlparse(text)
    if uri.scheme != "mqtt" or not uri.hostname:
        raise argparse.ArgumentTypeError("o URI deve ser mqtt://host[:porta] (mqtts:// não é suportado)")
    return resolve_host(uri.hostname), uri.port or 1883


def add_connection_args(parser: argparse.ArgumentParser) -> None:
    parser.add_argument("--uri", default=os.environ.get("MQTT_URI"),
                        help="URI do broker, igual ao da board (ex.: mqtt://broker.emqx.io:1883); "
                             "substitui --host/--port (omissão: $MQTT_URI)")
    parser.add_argument("--host", type=resolve_host, default=os.environ.get("MQTT_HOST", "localhost"),
                        help="endereço do broker, ou 'dev' para o broker público da EMQX "
                             "(omissão: $MQTT_HOST ou localhost)")
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


def resolve_connection(args: argparse.Namespace) -> None:
    """Aplica --uri sobre host/port e mostra o que vai ser usado, para comparar com a board."""
    if args.uri:
        try:
            args.host, args.port = parse_broker_uri(args.uri)
        except argparse.ArgumentTypeError as exc:
            die(str(exc))
    auth = f"utilizador '{args.username}'" if args.username else "sem autenticação"
    print(f"broker mqtt://{args.host}:{args.port}  ({auth})")


def make_client(args: argparse.Namespace, role: str) -> mqtt.Client:
    # client_id único: dois clientes com o mesmo id expulsam-se um ao outro no broker.
    client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2, client_id=f"dev-{role}-{os.getpid()}")
    if args.username:
        client.username_pw_set(args.username, os.environ.get("MQTT_PASSWORD"))
    return client


def die(message: str) -> None:
    print(f"erro: {message}", file=sys.stderr)
    sys.exit(1)
