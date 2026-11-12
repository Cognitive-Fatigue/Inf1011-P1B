"""Pair B replay test: an old approval sent again must be rejected.

Run: python laptop/replay_test.py [--usb]
1. Asks the watch to approve; double-tap the watch within 10 s. The real reply is recorded.
2. Sends that same reply again with nothing pending (an attacker replaying it later).
3. Asks again, then sends the old reply before the watch answers (replay against a new challenge).
Both replays must come out "invalid", so the vault stays locked.
"""
import asyncio
import sys

import security
from link import BleLink, UsbLink


async def main(usb):
    replies = asyncio.Queue()
    connected = asyncio.Event()
    link = (UsbLink if usb else BleLink)(
        lambda line: replies.put_nowait(line) if line.startswith("RESP:") else None,
        lambda where: connected.set())
    reader = asyncio.create_task(link.run())
    await asyncio.wait_for(connected.wait(), 30)
    security.send_line = link.send_line  # no vault window here

    print("Step 1: double-tap the watch now to approve...")
    security.request_approval()
    real = await asyncio.wait_for(replies.get(), 15)
    first = security.check_resp(real)
    print(f"  real reply: {first}")
    if first != "approved":
        print("Not approved, so there is nothing to replay. Run again and double-tap within 10 s.")
        reader.cancel()
        return False

    second = security.check_resp(real)
    print(f"Step 2: same reply sent again, nothing pending: {second}")

    security.request_approval()
    third = security.check_resp(real)
    print(f"Step 3: old reply against a new challenge: {third}")
    print("  (don't tap: the watch rejects the new challenge after 10 s)")
    try:
        await asyncio.wait_for(replies.get(), 15)
    except asyncio.TimeoutError:
        pass

    ok = second == "invalid" and third == "invalid"
    print("PASS: replayed approvals are rejected" if ok else "FAIL: a replayed approval was accepted")
    reader.cancel()
    return ok


if __name__ == "__main__":
    sys.exit(0 if asyncio.run(main("--usb" in sys.argv)) else 1)
