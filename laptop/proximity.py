"""Pair C extra feature: lock the laptop when the watch's Bluetooth signal stays weak.

The watch sends "RSSI:-72" once a second. If the average of the last few readings is
weaker than LOCK_BELOW, the laptop locks once. It can only lock again after the signal
has been strong again (you came back).
"""
import ctypes

ENABLED = True     # set to False to switch this feature off (e.g. if it misbehaves on demo day)
LOCK_BELOW = -85   # dBm: average weaker than this = walked away (about 4-5 m in our test)
ARM_ABOVE = -70    # dBm: average stronger than this = back at the laptop
SAMPLES = 4        # average of the last 4 readings (about 4 seconds)

send_line = None
readings = []      # the newest few signal readings
armed = True       # True = allowed to lock


def setup(send):
    global send_line
    send_line = send


def lock_laptop():
    ctypes.windll.user32.LockWorkStation()  # the same as pressing Windows + L


def on_line(line):
    """Return True if this line was for you."""
    global armed
    if not line.startswith("RSSI:"):
        return False                 # not ours
    try:
        rssi = int(line[5:])         # "RSSI:-72" -> -72
    except ValueError:
        return True                  # ours but garbled: ignore it, never crash

    readings.append(rssi)
    if len(readings) > SAMPLES:
        readings.pop(0)              # throw away the oldest reading
    if len(readings) < SAMPLES:
        return True                  # not enough readings yet

    average = sum(readings) / SAMPLES
    print(f"signal {rssi} dBm, average {average:.0f}")  # TEST: see the numbers live

    if not ENABLED:
        return True
    if armed and average < LOCK_BELOW:
        print("Signal weak: locking laptop")
        lock_laptop()
        armed = False                # don't lock again until you come back
    elif not armed and average > ARM_ABOVE:
        armed = True                 # you're back: allowed to lock next time
    return True