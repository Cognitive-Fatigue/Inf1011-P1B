"""Link test: sends random ECHO lines and checks each comes back unchanged.

Run: python echo_test.py [count] [--usb] [--reconnect N]
--usb tests the cable instead of Bluetooth. --reconnect N then has the watch drop the Bluetooth
link N times (DROP) and checks each reconnect takes under 5 s.
"""
import argparse
import asyncio
import random
import string
import sys
import time

from link import BleLink, UsbLink


async def main(args):
    received = asyncio.Queue()
    connected = asyncio.Event()
    # only ECHO lines: the watch also sends RSSI (and other) lines that would shift every comparison
    link = (UsbLink if args.usb else BleLink)(
        lambda line: received.put_nowait(line) if line.startswith("ECHO:") else None, lambda where: connected.set())
    reader = asyncio.create_task(link.run())
    await asyncio.wait_for(connected.wait(), 20)

    failures = 0
    for i in range(args.count):
        # up to the watch's 127-character limit
        sent = "ECHO:" + "".join(random.choices(string.ascii_letters + string.digits, k=random.randint(1, 122)))
        link.send_line(sent)
        try:
            got = await asyncio.wait_for(received.get(), 2)
        except asyncio.TimeoutError:
            got = None
        if got != sent:
            failures += 1
            print(f"#{i} sent {sent!r} got {got!r}")
    print(f"{args.count - failures}/{args.count} lines came back intact")

    slow = 0
    for i in range(args.reconnect):
        connected.clear()
        try:
            while not link.send_line("DROP"):  # the watch ends the link, as when it walks out of range
                await asyncio.wait_for(connected.wait(), 30)  # still reconnecting after a failed round: drop once it's back
                connected.clear()
        except asyncio.TimeoutError:
            print(f"reconnect {i + 1}: link did not come back within 30 s, stopping  FAIL")
            slow += args.reconnect - i
            break
        start = time.monotonic()
        try:
            await asyncio.wait_for(connected.wait(), 15)
            took = time.monotonic() - start
        except asyncio.TimeoutError:
            took = None
        ok = took is not None and took < 5
        slow += not ok
        print(f"reconnect {i + 1}: {'gave up after 15 s' if took is None else f'{took:.1f} s'}{'' if ok else '  FAIL'}")
    if args.reconnect:
        print(f"{args.reconnect - slow}/{args.reconnect} reconnects under 5 s")

    reader.cancel()
    return failures == 0 and slow == 0


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("count", type=int, nargs="?", default=100)
    parser.add_argument("--usb", action="store_true")
    parser.add_argument("--reconnect", type=int, default=0)
    args = parser.parse_args()
    if args.usb and args.reconnect:
        parser.error("--reconnect only works over Bluetooth")
    sys.exit(0 if asyncio.run(main(args)) else 1)
