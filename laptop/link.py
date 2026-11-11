"""Line link to one explicitly enrolled TinyKey watch."""
import argparse
import asyncio
import json
import os
from pathlib import Path
import re
import sys
import tempfile
import time

import serial
from bleak import BleakClient, BleakScanner
from bleak.exc import BleakError
from serial.tools import list_ports

WATCH_IDS = (0x03EB, 0x8009)
UART_WRITE = "6e400002-b5a3-f393-e0a9-e50e24dcca9e"
UART_NOTIFY = "6e400003-b5a3-f393-e0a9-e50e24dcca9e"
PACKET = 20
ID_RE = re.compile(r"[0-9a-f]{32}\Z")
ID_TIMEOUT = 2.0


def settings_path():
    if os.name == "nt":
        base = os.environ.get("LOCALAPPDATA")
        if not base:
            raise RuntimeError("LOCALAPPDATA is not set; cannot locate TinyKey pairing settings")
        return Path(base) / "TinyKey" / "pairing.json"
    return Path(os.environ.get("XDG_CONFIG_HOME", Path.home() / ".config")) / "tinykey" / "pairing.json"


def watch_name(identity):
    return "TinyKey-" + identity[:12]


def valid_identity(identity):
    return isinstance(identity, str) and ID_RE.fullmatch(identity) is not None


def load_pairing(path=None):
    path = Path(path or settings_path())
    try:
        data = json.loads(path.read_text(encoding="utf-8"))
        identity = data["id"]
        name = data["name"]
    except FileNotFoundError as exc:
        raise RuntimeError("No TinyKey pairing found. Connect by USB and run: python laptop/link.py --setup") from exc
    except (OSError, ValueError, KeyError, TypeError) as exc:
        raise RuntimeError(f"Invalid TinyKey pairing file {path}: {exc}") from exc
    if not valid_identity(identity) or name != watch_name(identity):
        raise RuntimeError(f"Invalid TinyKey pairing file {path}: expected a 32 digit lowercase ID and its derived name")
    return identity, name


def save_pairing(identity, path=None):
    if not valid_identity(identity):
        raise ValueError("watch returned an invalid 128 bit identity")
    path = Path(path or settings_path())
    path.parent.mkdir(parents=True, exist_ok=True)
    payload = json.dumps({"id": identity, "name": watch_name(identity)}, indent=2) + "\n"
    temp_path = None
    try:
        with tempfile.NamedTemporaryFile("w", encoding="utf-8", dir=path.parent, delete=False) as stream:
            temp_path = Path(stream.name)
            stream.write(payload)
            stream.flush()
            os.fsync(stream.fileno())
        os.replace(temp_path, path)
    finally:
        if temp_path and temp_path.exists():
            temp_path.unlink()
    return path


def matching_ports():
    return [p for p in list_ports.comports() if (p.vid, p.pid) == WATCH_IDS]


def read_identity(ser, timeout=ID_TIMEOUT):
    ser.reset_input_buffer()
    ser.write(b"ID?\n")
    deadline = time.monotonic() + timeout
    while deadline > time.monotonic():
        line = ser.readline().decode("ascii", "replace").strip()
        if line.startswith("ID:"):
            identity = line[3:]
            if not valid_identity(identity):
                raise RuntimeError("watch returned a malformed ID response")
            return identity
    raise TimeoutError("no ID response; upload the updated TinyKey firmware, then retry setup")


def setup_pairing(path=None):
    ports = matching_ports()
    if not ports:
        raise RuntimeError("No TinyKey USB serial device found. Connect the watch by USB and retry.")
    if len(ports) != 1:
        raise RuntimeError(f"Found {len(ports)} TinyKey USB serial devices. Disconnect extras and retry setup.")
    try:
        with serial.Serial(ports[0].device, 115200, timeout=0.2, write_timeout=1) as ser:
            identity = read_identity(ser)
    except serial.SerialException as exc:
        raise RuntimeError(f"Could not open {ports[0].device}: {exc}") from exc
    try:
        path = save_pairing(identity, path)
    except OSError as exc:
        raise RuntimeError(f"Could not save TinyKey pairing: {exc}") from exc
    name = watch_name(identity)
    print(f"Saved watch {name} (ID {identity}) to {path}\nNext: python laptop/main.py")
    return identity


def split_lines(buf):
    """Split received bytes into whole lines; returns (lines, leftover partial line)."""
    *lines, rest = buf.split(b"\n")
    return [l.rstrip(b"\r").decode("ascii", "replace") for l in lines if l.strip()], rest


class UsbLink:
    def __init__(self, on_line, on_connect=None, on_disconnect=None):
        self.on_line, self.on_connect, self.on_disconnect = on_line, on_connect, on_disconnect
        self.ser = None

    def send_line(self, text):
        if not self.ser:
            return False
        try:
            self.ser.write(text.encode("ascii") + b"\n")
            return True
        except serial.SerialException:
            return False

    async def run(self):
        identity, _ = load_pairing()
        while True:
            ser = None
            verified = False
            try:
                for port in matching_ports():
                    try:
                        candidate = serial.Serial(port.device, 115200, timeout=0.2, write_timeout=1)
                    except serial.SerialException:
                        continue
                    try:
                        if read_identity(candidate) == identity:
                            ser, verified = candidate, True
                            break
                    except (RuntimeError, TimeoutError, serial.SerialException):
                        pass
                    candidate.close()
                if not verified:
                    await asyncio.sleep(1)
                    continue
                self.ser = ser
                if self.on_connect:
                    self.on_connect(ser.port)
                buf = b""
                while True:
                    buf += await asyncio.to_thread(ser.read, ser.in_waiting or 1)
                    lines, buf = split_lines(buf)
                    for line in lines:
                        if line.startswith("ID:"):
                            continue
                        self.on_line(line)
            except serial.SerialException:
                pass
            finally:
                if ser:
                    ser.close()
                self.ser = None
            if verified and self.on_disconnect:
                self.on_disconnect()


def packets(text):
    data = text.encode("ascii") + b"\n"
    return [data[i:i + PACKET] for i in range(0, len(data), PACKET)]


class BleLink:
    def __init__(self, on_line, on_connect=None, on_disconnect=None):
        self.on_line, self.on_connect, self.on_disconnect = on_line, on_connect, on_disconnect
        self.client = None
        self.outbox = None

    def send_line(self, text):
        if not self.client:
            return False
        for packet in packets(text):
            self.outbox.put_nowait(packet)
        return True

    async def _write(self, client, gone):
        try:
            while True:
                await client.write_gatt_char(UART_WRITE, await self.outbox.get(), response=False)
        except (BleakError, OSError):
            gone.set()

    async def run(self):
        identity, name = load_pairing()
        device = None
        while True:
            if device is None:
                device = await BleakScanner.find_device_by_name(name, timeout=5)
                if not device:
                    continue
            gone = asyncio.Event()
            verified = False
            buf = b""
            id_response = asyncio.Event()

            def on_notify(_, data):
                nonlocal buf
                lines, buf = split_lines(buf + bytes(data))
                for line in lines:
                    if line.startswith("ID:"):
                        if line == "ID:" + identity:
                            id_response.set()
                        continue
                    if verified:
                        self.on_line(line)

            connected = False
            try:
                async with BleakClient(device, disconnected_callback=lambda _: gone.set(), timeout=5) as client:
                    await client.start_notify(UART_NOTIFY, on_notify)
                    await client.write_gatt_char(UART_WRITE, b"ID?\n", response=False)
                    try:
                        await asyncio.wait_for(id_response.wait(), ID_TIMEOUT)
                    except asyncio.TimeoutError:
                        pass
                    if not id_response.is_set():
                        device = None
                        continue
                    verified = connected = True
                    self.outbox = asyncio.Queue()
                    self.client = client
                    if self.on_connect:
                        self.on_connect(device.address)
                    writer = asyncio.create_task(self._write(client, gone))
                    while client.is_connected and not gone.is_set():
                        await asyncio.sleep(0.2)
                    writer.cancel()
            except (BleakError, OSError, asyncio.TimeoutError):
                if not connected:
                    device = None
            self.client = None
            if connected and self.on_disconnect:
                self.on_disconnect()


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--setup", action="store_true", help="enroll the watch connected by USB")
    args = parser.parse_args()
    if not args.setup:
        parser.error("--setup is required when running link.py directly")
    try:
        setup_pairing()
    except (RuntimeError, TimeoutError, ValueError) as exc:
        print(f"TinyKey setup failed: {exc}", file=sys.stderr)
        sys.exit(1)
