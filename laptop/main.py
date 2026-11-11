"""TinyKey laptop program. Pair A owns this file. Run: python main.py (Bluetooth) or python main.py --usb"""
import asyncio
import ctypes
import os
import sys

import remote
import security
from link import BleLink, UsbLink

_connected = False


def lock_workstation():
    """Lock the local Windows session; return (success, error text)."""
    if os.name != "nt":
        return False, "Windows LockWorkStation is unavailable on this platform"
    try:
        if ctypes.windll.user32.LockWorkStation():
            return True, ""
        return False, str(ctypes.WinError())
    except (AttributeError, OSError) as exc:
        return False, str(exc)


def on_line(line):
    for handler in (security.on_line, remote.on_line):
        if handler(line):
            return
    # unknown messages are ignored


def on_connect(port):
    global _connected
    _connected = True
    security.disconnect()
    remote.set_connected(True)
    remote.set_mode(None)
    print(f"Watch connected on {port}")
    if not link.send_line("SYNC"):
        print("Watch mode sync could not be sent")


def on_disconnect():
    global _connected
    if not _connected:
        return
    _connected = False
    security.disconnect()
    remote.set_connected(False)
    print("Watch disconnected")
    locked, error = lock_workstation()
    if not locked:
        print(f"Laptop lock failed: {error}")


async def main():
    global link, _connected
    _connected = False
    link = (UsbLink if "--usb" in sys.argv else BleLink)(on_line, on_connect, on_disconnect)
    security.setup(link.send_line)
    remote.setup(link.send_line, lambda: _connected and remote.mode_authorized and
                 remote.media_mode and not security.is_pending())
    await link.run()


if __name__ == "__main__":
    asyncio.run(main())
