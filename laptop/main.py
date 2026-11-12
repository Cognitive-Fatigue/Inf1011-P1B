"""TinyKey laptop program. Pair A owns this file. Run: python main.py (Bluetooth) or python main.py --usb"""
import asyncio
import sys
import pointer
import proximity  # (Pair C test): lock when the Bluetooth signal stays weak
import remote
import security
from link import BleLink, UsbLink


def on_line(line):
    for handler in (security.on_line, remote.on_line, proximity.on_line, pointer.on_line):
        if handler(line):
            return
    # unknown messages are ignored


def on_connect(port):
    print(f"Watch connected on {port}")


def on_disconnect():
    print("Watch disconnected")  # Week 2: lock the laptop here


async def main():
    link = (UsbLink if "--usb" in sys.argv else BleLink)(on_line, on_connect, on_disconnect)
    security.setup(link.send_line)
    remote.setup(link.send_line)
    proximity.setup(link.send_line)  # NEW proximity testing setup
    pointer.setup(link.send_line)    # NEW
    await link.run()


if __name__ == "__main__":
    asyncio.run(main())
