"""HMAC test: sends random challenges and checks the watch's reply matches Python's HMAC.

Run: python laptop/security_test.py [count] [--usb]
--usb tests over the cable instead of Bluetooth. Close the Arduino Serial Monitor first: only one
program can use the USB port at a time.
"""
import argparse
import asyncio
import secrets
import sys

import security
from link import BleLink, UsbLink


async def main(args):
    received = asyncio.Queue()
    connected = asyncio.Event()

    def on_line(line):
        if line.startswith("RESP:"):
            received.put_nowait(line)  # DBG and other lines are ignored

    link = (UsbLink if args.usb else BleLink)(on_line, lambda where: connected.set())
    reader = asyncio.create_task(link.run())
    await asyncio.wait_for(connected.wait(), 20)
    await asyncio.sleep(0.5)  # let the watch finish starting up after the port opens

    failures = 0
    for i in range(args.count):
        challenge = secrets.token_bytes(16)
        link.send_line(f"CHAL:{challenge.hex()}:VAULT")
        want = "RESP:" + security.expected_hex(challenge)
        try:
            got = await asyncio.wait_for(received.get(), 2)
        except asyncio.TimeoutError:
            got = None
        if got != want:
            failures += 1
            print(f"#{i} challenge {challenge.hex()}\n  want {want}\n  got  {got}")
    print(f"{args.count - failures}/{args.count} watch replies match Python's HMAC")
    reader.cancel()
    return failures == 0


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("count", type=int, nargs="?", default=20)
    parser.add_argument("--usb", action="store_true")
    sys.exit(0 if asyncio.run(main(parser.parse_args())) else 1)
