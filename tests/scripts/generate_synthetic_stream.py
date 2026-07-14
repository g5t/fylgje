#!/usr/bin/env python3
"""
Generate a small synthetic ar51 message stream for the fylgje consumer
integration test, in the JSON-lines format kafka_uploader.py accepts:

    {"value": <base64>, "value_is_binary": true, "timestamp": <ms>, "partition": <p>}

Each value is a finished ar51 flatbuffer (RawReadoutMessage) wrapping one ESS
data packet: a PacketHeaderV0 followed by CAEN readouts, mirroring
tests/fylgje/SyntheticData.cpp. Messages are distributed round-robin across
partitions with evenly spaced timestamps, so the expected count for any
consumer time window is trivially computable.

Only the python standard library is required: the ar51 schema is small enough
that the flatbuffer is assembled by hand (see build_ar51_flatbuffer).
"""

import argparse
import base64
import json
import struct
import sys
from datetime import datetime, timezone
from pathlib import Path

CAEN_TYPE = 3
ESS_COOKIE = 0x535345  # "ESS"

# PacketHeaderV0: Padding0, Version, CookieAndType, TotalLength, OutputQueue,
#                 TimeSource, PulseHigh, PulseLow, PrevPulseHigh, PrevPulseLow, SeqNum
PACKET_HEADER_V0 = struct.Struct("<BBIHBBIIIII")
# CAENReadout: Fiber, FEN, Length, HighTime, LowTime, Flags_OM, Group, Unused, A, B, C, D
CAEN_READOUT = struct.Struct("<BBHIIBBHhhhh")


def make_caen_packet(n_readouts: int, pulse_high: int, pulse_low: int, seq_num: int) -> bytes:
    total = PACKET_HEADER_V0.size + n_readouts * CAEN_READOUT.size
    header = PACKET_HEADER_V0.pack(
        0,                     # Padding0
        0,                     # Version (V0)
        (CAEN_TYPE << 28) | ESS_COOKIE,
        total,                 # TotalLength
        0,                     # OutputQueue
        0,                     # TimeSource
        pulse_high,
        pulse_low,
        pulse_high - 1 if pulse_high > 0 else 0,  # PrevPulseHigh
        pulse_low,             # PrevPulseLow
        seq_num,
    )
    readouts = b"".join(
        CAEN_READOUT.pack(
            (seq_num + i) % 10,        # Fiber
            0,                         # FEN: non-zero is flagged as a bad readout
            CAEN_READOUT.size,         # Length
            pulse_high,                # HighTime
            pulse_low + 1 + i,         # LowTime: strictly after the header pulse
            0,                         # Flags_OM
            (seq_num + i) % 15,        # Group
            0,                         # Unused
            100 + i,                   # A
            200 + i,                   # B
            0,                         # C
            0,                         # D
        )
        for i in range(n_readouts)
    )
    return header + readouts


def build_ar51_flatbuffer(source_name: str, message_id: int, raw_data: bytes) -> bytes:
    """Hand-assembled flatbuffer for the ar51 RawReadoutMessage table:

        table RawReadoutMessage {
            source_name: string (required);  // field id 0
            message_id: long;                // field id 1
            raw_data: [ubyte];               // field id 2
        }

    Layout (little-endian):
        0   uint32  offset to root table
        4   "ar51"  file identifier
        8   vtable  [len=10][table_len=24][source_name@8][message_id@16][raw_data@4]
        18  pad to 8-aligned table position
        24  table   [soffset to vtable][raw_data uoffset][source_name uoffset]
                    [pad][int64 message_id]
        48  string  [len][utf8][NUL] padded to 4
        ..  vector  [len][bytes] padded to 4
    """
    table_pos = 24
    vtable = struct.pack("<5H", 10, 24, 8, 16, 4)

    string_pos = table_pos + 24
    encoded = source_name.encode("utf-8")
    string_bytes = struct.pack("<I", len(encoded)) + encoded + b"\0"
    string_bytes += b"\0" * (-len(string_bytes) % 4)

    vector_pos = string_pos + len(string_bytes)
    vector_bytes = struct.pack("<I", len(raw_data)) + raw_data
    vector_bytes += b"\0" * (-len(vector_bytes) % 4)

    table = struct.pack(
        "<iII4xq",
        table_pos - 8,                  # soffset: table position minus vtable position
        vector_pos - (table_pos + 4),   # raw_data uoffset, relative to its own field
        string_pos - (table_pos + 8),   # source_name uoffset, relative to its own field
        message_id,
    )

    buffer = struct.pack("<I", table_pos) + b"ar51" + vtable + b"\0" * (table_pos - 18)
    buffer += table + string_bytes + vector_bytes
    return buffer


def parse_time_ms(value: str) -> int:
    if "T" not in value:
        value += "T00:00:00"
    if value.endswith("Z"):
        value = value[:-1] + "+00:00"
    parsed = datetime.fromisoformat(value)
    if parsed.tzinfo is None:
        parsed = parsed.replace(tzinfo=timezone.utc)
    return int(parsed.timestamp() * 1000)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--messages", type=int, default=300, help="number of ar51 messages")
    parser.add_argument("--partitions", type=int, default=3, help="partitions to distribute over, round-robin")
    parser.add_argument("--readouts", type=int, default=10, help="CAEN readouts per message")
    parser.add_argument("--start-time", default="2026-01-01T00:00:00Z",
                        help="ISO timestamp of the first message (default: %(default)s)")
    parser.add_argument("--interval-ms", type=int, default=1000, help="milliseconds between messages")
    parser.add_argument("--source", default="bifrost", help="ar51 source_name")
    parser.add_argument("--output", "-o", type=Path, required=True, help="output .jsonl path")
    args = parser.parse_args()

    t0_ms = parse_time_ms(args.start_time)
    with open(args.output, "w", encoding="utf-8") as f:
        for i in range(args.messages):
            timestamp_ms = t0_ms + i * args.interval_ms
            packet = make_caen_packet(args.readouts, timestamp_ms // 1000, 0, i)
            payload = build_ar51_flatbuffer(args.source, i, packet)
            record = {
                "value": base64.b64encode(payload).decode("ascii"),
                "value_is_binary": True,
                "timestamp": timestamp_ms,
                "partition": i % args.partitions,
            }
            f.write(json.dumps(record) + "\n")

    last_ms = t0_ms + (args.messages - 1) * args.interval_ms
    print(f"Wrote {args.messages} messages over {args.partitions} partitions to {args.output}")
    print(f"Timestamps {t0_ms}..{last_ms} ms "
          f"({args.start_time} + {(args.messages - 1) * args.interval_ms} ms)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
