#!/usr/bin/env python3
"""
Kafka consumer integration test.

Spins up an ephemeral, single-node Kafka broker (KRaft mode, no ZooKeeper)
using Podman, loads a previously-extracted message stream into it, then runs
the C++ consumer executable under test against that broker and checks the
result.

Intended to be invoked from CTest, but also runs standalone:

    python3 test_kafka_consumer_integration.py \
        --consumer-exe build/my_consumer \
        --stream-file data/extracted_topic.jsonl \
        --upload-script scripts/upload_to_kafka.py \
        --topic my-topic

Exit code 0 = pass, non-zero = fail.
"""

import socket
import subprocess
import sys
import time
from contextlib import contextmanager
from pathlib import Path
from typing import List, Optional, Tuple

import click
from python_on_whales import DockerClient

from scripts.kafka_uploader import detect_file_format, make_uploader_and_send, KafkaMessageUploader

KAFKA_IMAGE = "apache/kafka:3.7.0"
CONTAINER_NAME = "fylgje-consumer-itest"

# python-on-whales shells out to the given CLI (no daemon socket required),
# so this works with a plain `podman` binary on PATH.
docker = DockerClient(client_call=["podman"])


def find_free_port() -> int:
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
        s.bind(("", 0))
        return s.getsockname()[1]


@contextmanager
def ephemeral_kafka(image: str = KAFKA_IMAGE, broker_port: Optional[int] = None):
    """
    Start a single-node KRaft Kafka broker in a Podman container and yield
    the host:port to connect to. Container is stopped/removed on exit
    regardless of test outcome.
    """
    broker_port = broker_port or find_free_port()
    controller_port = find_free_port()

    env = {
        "KAFKA_NODE_ID": "1",
        "KAFKA_PROCESS_ROLES": "broker,controller",
        "KAFKA_LISTENERS": f"PLAINTEXT://:{broker_port},CONTROLLER://:{controller_port}",
        "KAFKA_ADVERTISED_LISTENERS": f"PLAINTEXT://localhost:{broker_port}",
        "KAFKA_LISTENER_SECURITY_PROTOCOL_MAP": "CONTROLLER:PLAINTEXT,PLAINTEXT:PLAINTEXT",
        "KAFKA_CONTROLLER_LISTENER_NAMES": "CONTROLLER",
        "KAFKA_INTER_BROKER_LISTENER_NAME": "PLAINTEXT",
        "KAFKA_CONTROLLER_QUORUM_VOTERS": f"1@localhost:{controller_port}",
        "KAFKA_OFFSETS_TOPIC_REPLICATION_FACTOR": "1",
        "KAFKA_AUTO_CREATE_TOPICS_ENABLE": "false",
        # Fixed cluster ID (valid base64, 16 bytes) keeps startup deterministic.
        "CLUSTER_ID": "MkU3OEVBNTcwNTJENDM2Qk",
    }

    click.echo(f"[itest] starting {image} ...")
    container = docker.run(
        image,
        detach=True,
        name=CONTAINER_NAME,
        envs=env,
        publish=[(broker_port, broker_port), (controller_port, controller_port)],
        remove=True,
    )

    try:
        _wait_for_broker("localhost", broker_port, timeout=60)
        yield f"localhost:{broker_port}"
    finally:
        click.echo("[itest] stopping kafka container ...")
        try:
            docker.stop(container, time=5)
        except Exception as e:
            click.echo(f"[itest] warning: failed to stop container cleanly: {e}", err=True)


def _wait_for_broker(host: str, port: int, timeout: int = 60):
    """Poll until the broker is actually serving metadata, not just TCP-open
    (during KRaft startup the port can accept connections before Kafka is
    ready to answer requests)."""
    from confluent_kafka.admin import AdminClient

    deadline = time.monotonic() + timeout
    last_err = None

    while time.monotonic() < deadline:
        try:
            with socket.create_connection((host, port), timeout=2):
                pass
        except OSError as e:
            last_err = e
            time.sleep(1)
            continue

        try:
            admin = AdminClient({"bootstrap.servers": f"{host}:{port}"})
            md = admin.list_topics(timeout=5)
            if md is not None:
                click.echo(f"[itest] broker ready ({len(md.topics)} topics visible)")
                return
        except Exception as e:
            last_err = e
            time.sleep(1)

    raise TimeoutError(f"Kafka broker did not become ready within {timeout}s: {last_err}")


def create_topic(
        bootstrap_servers: str,
        topic: str,
        num_partitions: int,
        replication_factor: int = 1,
        timeout: int = 30,
):
    """Explicitly create the topic with the given partition count."""
    from confluent_kafka.admin import AdminClient, NewTopic
    from confluent_kafka import KafkaException

    click.echo(f"[itest] creating topic '{topic}' with {num_partitions} partition(s) ...")
    admin = AdminClient({"bootstrap.servers": bootstrap_servers})
    new_topic = NewTopic(topic, num_partitions=num_partitions, replication_factor=replication_factor)
    futures = admin.create_topics([new_topic], request_timeout=timeout)

    for created_topic, future in futures.items():
        try:
            future.result(timeout=timeout)
            click.echo(f"[itest] topic '{created_topic}' created")
        except KafkaException as e:
            raise RuntimeError(f"failed to create topic '{created_topic}': {e}") from e


def upload_stream(stream_file: Path, bootstrap_servers: str, topic: str) -> int:
    """
    Use the ECDC kafka_uploader.py provided KafkaMessageUploader to decode and upload the saved stream
    ----
    Returns:
        the number of messages sent successfully to the broker
    """
    from scripts.kafka_uploader import  KafkaMessageUploader, load_kafka_config
    click.echo(f"[itest] uploading {stream_file} to '{topic}' via kafka_uploader.KafkaMessageUploader ...")

    preserve_timestamps = True  # important for our tests
    kafka_config = None  # load_kafka_config(kafka_config_file) if kafka_config_file else None
    uploader = KafkaMessageUploader(bootstrap_servers, preserve_timestamps, kafka_config)

    # this is the no-rate-limit path -- consider implementing the rate-limited call as an option
    if 'json' in stream_file.suffix:
        count, errors = uploader.upload_from_jsonl(stream_file, topic)
    else:
        count, errors = uploader.upload_from_binary(stream_file, topic)

    if errors:
        click.echo(f"[itest] kafka upload errors: {errors}")

    return count

def run_consumer_under_test(
        consumer_exe: Path,
        bootstrap_servers: str,
        topic: str,
        extra_args: Tuple[str, ...],
        timeout: int = 60,
) -> subprocess.CompletedProcess:
    cmd = [
        str(consumer_exe),
        "-b", bootstrap_servers,
        "-t", topic,
        *extra_args
    ]
    click.echo(f"[itest] running consumer under test: {' '.join(cmd)}")
    return subprocess.run(cmd, capture_output=True, text=True, timeout=timeout)


@click.command(context_settings={"help_option_names": ["-h", "--help"]})
@click.option(
    "--consumer-exe", required=True,
    type=click.Path(exists=True, dir_okay=False, path_type=Path),
    help="Path to the compiled C++ consumer executable.",
)
@click.option(
    "--stream-file", required=True,
    type=click.Path(exists=True, dir_okay=False, path_type=Path),
    help="Path to extracted topic stream (JSON-lines).",
)
@click.option(
    "--topic", default="test-topic", show_default=True,
    help="Topic name to use on the ephemeral broker.",
)
@click.option(
    "--kafka-image", default=KAFKA_IMAGE, show_default=True,
    help="Container image for the ephemeral Kafka broker.",
)
@click.option(
    "--consumer-arg", "consumer_args", multiple=True,
    help="Extra CLI arg to pass through to the consumer under test "
         "(repeatable, e.g. --consumer-arg=--verbose --consumer-arg=--foo=bar).",
)
@click.option(
    "--expected-count", type=int, default=None,
    help="Expected message count; defaults to counting lines in --stream-file.",
)
@click.option(
    "--partitions", type=int, default=None,
    help="Number of partitions to create the topic with; defaults to "
         "max(partition) + 1 detected from --stream-file.",
)
@click.option(
    "--replication-factor", type=int, default=1, show_default=True,
    help="Replication factor for the created topic (single-broker cluster, "
         "so this should normally stay at 1).",
)

def cli(
        consumer_exe: Path,
        stream_file: Path,
        topic: str,
        kafka_image: str,
        consumer_args: Tuple[str, ...],
        expected_count: Optional[int],
        partitions: Optional[int],
        replication_factor: int,
):
    """Run the Kafka consumer integration test.

    Exit code 0 = pass, non-zero = fail, so this can be used directly as a
    CTest command.
    """
    sys.exit(
        run(
            consumer_exe=consumer_exe,
            stream_file=stream_file,
            topic=topic,
            kafka_image=kafka_image,
            consumer_args=consumer_args,
            expected_count=expected_count,
            partitions=partitions,
            replication_factor=replication_factor,
        )
    )


def run(
        consumer_exe: Path,
        stream_file: Path,
        topic: str,
        kafka_image: str,
        consumer_args: Tuple[str, ...],
        expected_count: Optional[int],
        partitions: Optional[int],
        replication_factor: int,
) -> int:
    from scripts.kafka_uploader import scan_file
    if not expected_count or not partitions:
        detected_count, detected_partitions, detected_earliest, detected_latest = scan_file(stream_file)
        expected_count = expected_count or detected_count
        partitions = partitions or detected_partitions

    with ephemeral_kafka(image=kafka_image) as bootstrap_servers:
        create_topic(bootstrap_servers, topic, partitions, replication_factor)
        upload_count = upload_stream(stream_file, bootstrap_servers, topic)
        click.echo(f"[itest] expecting consumer to see {expected_count} messages")

        result = run_consumer_under_test(consumer_exe, bootstrap_servers, topic, consumer_args)

        click.echo("---- consumer stdout ----")
        click.echo(result.stdout)
        click.echo("---- consumer stderr ----")
        click.echo(result.stderr)

        if result.returncode != 0:
            click.echo(f"[itest] FAIL: consumer exited with code {result.returncode}", err=True)
            return 1

        # ASSUMPTION: the consumer prints a line like "messages_consumed=<N>".
        # Replace this block with however your binary actually reports what
        # it processed.
        consumed = None
        for line in result.stdout.splitlines():
            if line.startswith("messages_consumed="):
                consumed = int(line.split("=", 1)[1])
                break

        if consumed is None:
            click.echo(
                "[itest] FAIL: could not find 'messages_consumed=' in consumer output",
                err=True,
            )
            return 1

        if consumed != expected_count:
            click.echo(
                f"[itest] FAIL: expected {expected_count} messages, "
                f"consumer reported {consumed}",
                err=True,
            )
            return 1

    click.echo("[itest] PASS")
    return 0


if __name__ == "__main__":
    cli()