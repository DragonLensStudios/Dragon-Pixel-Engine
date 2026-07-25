from __future__ import annotations

import io
import unittest

from dragonpixel_tools.protocol import MAXIMUM_MESSAGE_LENGTH, encode_message, read_message


class ProtocolTests(unittest.TestCase):
    def test_unicode_json_rpc_round_trip(self) -> None:
        message = {"jsonrpc": "2.0", "id": 7, "method": "inspectScene", "params": {"name": "Drágon"}}
        self.assertEqual(read_message(io.BytesIO(encode_message(message))), message)

    def test_rejects_oversized_payload(self) -> None:
        with self.assertRaises(ValueError):
            encode_message({"payload": "x" * MAXIMUM_MESSAGE_LENGTH})

    def test_rejects_truncated_frame(self) -> None:
        with self.assertRaises(EOFError):
            read_message(io.BytesIO(b"\x05\x00\x00\x00{}"))


if __name__ == "__main__":
    unittest.main()
