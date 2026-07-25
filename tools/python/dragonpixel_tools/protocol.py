"""Versioned length-prefixed JSON-RPC framing shared by automation clients."""

from __future__ import annotations

import json
import struct
from typing import BinaryIO, Any

MAXIMUM_MESSAGE_LENGTH = 1024 * 1024


def encode_message(message: dict[str, Any]) -> bytes:
    payload = json.dumps(message, ensure_ascii=False, separators=(",", ":")).encode("utf-8")
    if not payload or len(payload) > MAXIMUM_MESSAGE_LENGTH:
        raise ValueError("Automation message length is out of range")
    return struct.pack("<I", len(payload)) + payload


def read_exact(stream: BinaryIO, length: int) -> bytes:
    chunks = bytearray()
    while len(chunks) < length:
        chunk = stream.read(length - len(chunks))
        if not chunk:
            raise EOFError("Automation endpoint closed during a framed message")
        chunks.extend(chunk)
    return bytes(chunks)


def read_message(stream: BinaryIO) -> dict[str, Any]:
    length = struct.unpack("<I", read_exact(stream, 4))[0]
    if length == 0 or length > MAXIMUM_MESSAGE_LENGTH:
        raise ValueError("Automation response length is out of range")
    value = json.loads(read_exact(stream, length).decode("utf-8"))
    if not isinstance(value, dict):
        raise ValueError("Automation response root must be an object")
    return value
