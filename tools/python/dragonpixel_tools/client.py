"""Standard-library client for the editor's local automation broker."""

from __future__ import annotations

import os
import socket
import time
from pathlib import Path
from typing import BinaryIO, Any

from .protocol import encode_message, read_message


class AutomationError(RuntimeError):
    def __init__(self, code: int, message: str) -> None:
        super().__init__(f"Automation error {code}: {message}")
        self.code = code
        self.message = message


class AutomationClient:
    def __init__(self, endpoint: str, capability_token: str, timeout_seconds: float = 5.0) -> None:
        if not endpoint or not capability_token:
            raise ValueError("Endpoint and capability token are required")
        self._endpoint = endpoint
        self._token = capability_token
        self._timeout_seconds = timeout_seconds
        self._stream: BinaryIO | None = None
        self._socket: socket.socket | None = None
        self._next_request_id = 0

    def __enter__(self) -> "AutomationClient":
        deadline = time.monotonic() + self._timeout_seconds
        while True:
            try:
                if os.name == "nt":
                    pipe_path = self._endpoint
                    if not pipe_path.startswith("\\\\.\\pipe\\"):
                        pipe_path = "\\\\.\\pipe\\" + pipe_path
                    self._stream = open(pipe_path, "r+b", buffering=0)
                else:
                    connection = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
                    connection.settimeout(self._timeout_seconds)
                    connection.connect(str(Path(self._endpoint)))
                    self._socket = connection
                    self._stream = connection.makefile("rwb", buffering=0)
                return self
            except (FileNotFoundError, ConnectionRefusedError, PermissionError, OSError):
                if time.monotonic() >= deadline:
                    raise
                time.sleep(0.05)

    def __exit__(self, *_: object) -> None:
        if self._stream is not None:
            self._stream.close()
            self._stream = None
        if self._socket is not None:
            self._socket.close()
            self._socket = None

    def request(self, method: str, parameters: dict[str, Any] | None = None) -> dict[str, Any]:
        if self._stream is None:
            raise RuntimeError("Automation client is not connected")
        self._next_request_id += 1
        request = {
            "jsonrpc": "2.0",
            "id": self._next_request_id,
            "method": method,
            "params": parameters or {},
            "capabilityToken": self._token,
        }
        self._stream.write(encode_message(request))
        self._stream.flush()
        response = read_message(self._stream)
        if response.get("id") != self._next_request_id:
            raise RuntimeError("Automation response ID did not match the request")
        if "error" in response:
            error = response["error"]
            raise AutomationError(int(error.get("code", -32603)), str(error.get("message", "unknown error")))
        result = response.get("result")
        if not isinstance(result, dict):
            raise RuntimeError("Automation response had no object result")
        return result
