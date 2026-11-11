"""Focused tests for watch identity enrollment and transport verification."""
import asyncio
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

import link


IDENTITY = "0123456789abcdef0123456789abcdef"


class LinkTests(unittest.TestCase):
    def test_ble_advertisement_buffer_has_no_extra_byte(self):
        source = (Path(__file__).resolve().parents[1] / "watch" / "ble.ino").read_text(encoding="utf-8")
        self.assertIn("char adv[sizeof(BLE_NAME)];", source)
        self.assertIn("NO_WHITE_LIST_USE, sizeof(adv), adv, 0, NULL, 0, 0);", source)

    def test_identity_name_and_pairing_validation(self):
        self.assertTrue(link.valid_identity(IDENTITY))
        self.assertEqual(link.watch_name(IDENTITY), "TinyKey-0123456789ab")
        self.assertFalse(link.valid_identity(IDENTITY.upper()))
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "pairing.json"
            with self.assertRaisesRegex(RuntimeError, "No TinyKey pairing"):
                link.load_pairing(path)
            path.write_text("{", encoding="utf-8")
            with self.assertRaisesRegex(RuntimeError, "Invalid TinyKey pairing"):
                link.load_pairing(path)
            link.save_pairing(IDENTITY, path)
            self.assertEqual(json.loads(path.read_text()), {"id": IDENTITY, "name": link.watch_name(IDENTITY)})

    def test_usb_enrollment_valid_invalid_timeout_and_device_counts(self):
        class Port:
            device, vid, pid = "COM9", *link.WATCH_IDS

        class Serial:
            responses = []
            def __init__(self, *args, **kwargs): self.closed = False
            def __enter__(self): return self
            def __exit__(self, *args): self.close()
            def close(self): self.closed = True
            def reset_input_buffer(self): pass
            def write(self, value): self.query = value
            def readline(self): return self.responses.pop(0) if self.responses else b""

        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "pair.json"
            with patch.object(link, "matching_ports", return_value=[]), self.assertRaisesRegex(RuntimeError, "No TinyKey USB"):
                link.setup_pairing(path)
            with patch.object(link, "matching_ports", return_value=[Port(), Port()]), self.assertRaisesRegex(RuntimeError, "Found 2"):
                link.setup_pairing(path)
            with patch.object(link, "matching_ports", return_value=[Port()]), patch.object(link.serial, "Serial", Serial):
                Serial.responses = [b"debug\n", b"ID:" + IDENTITY.encode() + b"\n"]
                self.assertEqual(link.setup_pairing(path), IDENTITY)
                Serial.responses = [b"ID:not-an-id\n"]
                with patch.object(link, "ID_TIMEOUT", 0.01):
                    with self.assertRaisesRegex(RuntimeError, "malformed ID"):
                        link.setup_pairing(path)
                Serial.responses = []
                with patch.object(link, "ID_TIMEOUT", 0):
                    with self.assertRaisesRegex(TimeoutError, "upload"):
                        link.setup_pairing(path)

    def test_usb_selects_only_full_identity_before_connect(self):
        class Port:
            def __init__(self, device): self.device, self.vid, self.pid = device, *link.WATCH_IDS
        class Serial:
            def __init__(self, port, *args, **kwargs): self.port, self.in_waiting, self.closed = port, 0, False
            def close(self): self.closed = True
            def read(self, _): raise asyncio.CancelledError

        callbacks = []
        usb = link.UsbLink(lambda line: callbacks.append(("line", line)), lambda port: callbacks.append(("connect", port)))
        with patch.object(link, "load_pairing", return_value=(IDENTITY, link.watch_name(IDENTITY))), \
             patch.object(link, "matching_ports", return_value=[Port("COM1"), Port("COM2")]), \
             patch.object(link.serial, "Serial", Serial), \
             patch.object(link, "read_identity", side_effect=["f" * 32, IDENTITY]), \
             patch.object(link.asyncio, "sleep", side_effect=asyncio.CancelledError):
            with self.assertRaises(asyncio.CancelledError):
                asyncio.run(usb.run())
        self.assertEqual(callbacks, [("connect", "COM2")])

    def test_ble_rejects_wrong_id_and_never_calls_callbacks(self):
        device = type("Device", (), {"address": "test"})()
        callbacks = []
        class Scanner:
            calls = 0
            @classmethod
            async def find_device_by_name(cls, name, timeout):
                cls.calls += 1
                if name != link.watch_name(IDENTITY): raise AssertionError(name)
                if cls.calls > 1: raise asyncio.CancelledError
                return device
        class Client:
            def __init__(self, *args, **kwargs): self.is_connected = True; self.notify = None
            async def __aenter__(self): return self
            async def __aexit__(self, *args): pass
            async def start_notify(self, _, callback): self.notify = callback
            async def write_gatt_char(self, _, data, response=False):
                if data == b"ID?\n": self.notify(None, b"ID:" + b"f" * 32 + b"\n")
        ble = link.BleLink(lambda line: callbacks.append(("line", line)), lambda addr: callbacks.append(("connect", addr)))
        with patch.object(link, "load_pairing", return_value=(IDENTITY, link.watch_name(IDENTITY))), \
             patch.object(link, "BleakScanner", Scanner), patch.object(link, "BleakClient", Client), \
             patch.object(link, "ID_TIMEOUT", 0.01):
            with self.assertRaises(asyncio.CancelledError): asyncio.run(ble.run())
        self.assertEqual(callbacks, [])

    def test_ble_verifies_saved_watch_and_calls_connect_before_lines(self):
        device = type("Device", (), {"address": "saved-device"})()
        callbacks = []
        class Scanner:
            @staticmethod
            async def find_device_by_name(name, timeout): return device
        class Client:
            def __init__(self, *args, **kwargs): self.is_connected = True; self.notify = None
            async def __aenter__(self): return self
            async def __aexit__(self, *args): pass
            async def start_notify(self, _, callback): self.notify = callback
            async def write_gatt_char(self, _, data, response=False):
                if data == b"ID?\n": self.notify(None, b"debug\nID:" + IDENTITY.encode() + b"\n")
        def connected(addr):
            callbacks.append(("connect", addr))
            self.assertEqual([x[0] for x in callbacks], ["connect"])
        ble = link.BleLink(lambda line: callbacks.append(("line", line)), connected)
        with patch.object(link, "load_pairing", return_value=(IDENTITY, link.watch_name(IDENTITY))), \
             patch.object(link, "BleakScanner", Scanner), patch.object(link, "BleakClient", Client), \
             patch.object(link, "ID_TIMEOUT", 0.01), \
             patch.object(link.asyncio, "sleep", side_effect=asyncio.CancelledError):
            with self.assertRaises(asyncio.CancelledError): asyncio.run(ble.run())
        self.assertEqual(callbacks, [("connect", "saved-device")])

    def test_usb_retries_until_saved_watch_returns(self):
        class Port:
            device, vid, pid = "COM7", *link.WATCH_IDS
        class Serial:
            def __init__(self, port, *args, **kwargs): self.port, self.in_waiting = port, 0
            def close(self): pass
            def read(self, _): raise asyncio.CancelledError

        ports = iter(([], [Port()]))
        callbacks = []
        async def pause(_):
            try:
                current = next(ports)
            except StopIteration:
                raise asyncio.CancelledError
            port_state[0] = current
        port_state = [[]]
        usb = link.UsbLink(lambda line: None, lambda port: callbacks.append(port))
        with patch.object(link, "load_pairing", return_value=(IDENTITY, link.watch_name(IDENTITY))), \
             patch.object(link, "matching_ports", side_effect=lambda: port_state[0]), \
             patch.object(link.serial, "Serial", Serial), \
             patch.object(link, "read_identity", return_value=IDENTITY), \
             patch.object(link.asyncio, "sleep", side_effect=pause):
            with self.assertRaises(asyncio.CancelledError): asyncio.run(usb.run())
        self.assertEqual(callbacks, ["COM7"])


if __name__ == "__main__":
    unittest.main()
