"""Pair C TEST: air mouse / laser pointer.

While the pointer button is held, the watch sends its tilt as "TILT:x,y" about 20 times a
second (-128..128, 0,0 = level). This file moves the mouse: tilt more = move faster.
In a PowerPoint slideshow, press Ctrl+L to show the pointer as a red laser dot.
"""
import threading
import time
import math
from pynput.mouse import Controller

# ---- Settings you can tune (no watch upload needed) ----
DEAD_ZONE = 10      # tilt smaller than this does nothing (about 7 degrees), so no drifting
MAX_SPEED = 1500    # pixels per second at full tilt (crosses a 1920-wide screen in about 1.3 s)
INVERT_X = False    # True if the dot moves the wrong way left/right
INVERT_Y = False    # True if the dot moves the wrong way up/down
SWAP_XY = True     # True if tilting left/right moves the dot up/down
TIMEOUT = 0.3       # stop moving if no TILT message arrives for this long (seconds)
CIRCLE_RADIUS = 160  # circle size in pixels
CIRCLE_TIME = 1.2   # seconds to draw one circle

mouse = Controller()
send_line = None
speed = [0.0, 0.0]  # current speed in pixels per second (x, y)
last_msg = 0.0
lock = threading.Lock()
circling = False    # True while a circle is being drawn

def to_speed(v):
    """Turns a tilt number into a speed. Small tilt = slow, big tilt = fast."""
    if abs(v) < DEAD_ZONE:
        return 0.0
    sign = 1 if v > 0 else -1
    amount = min((abs(v) - DEAD_ZONE) / (128 - DEAD_ZONE), 1.0)  # 0.0 to 1.0
    return sign * amount * amount * MAX_SPEED  # squared: small tilts are extra gentle for aiming


def mover():
    """Runs in the background: moves the mouse a little every 10 ms, so movement looks smooth."""
    carry = [0.0, 0.0]  # leftover fractions of a pixel
    while True:
        time.sleep(0.01)
        with lock:
            sx, sy = speed
            stale = time.time() - last_msg > TIMEOUT
        if circling or stale or (sx == 0 and sy == 0):
            carry = [0.0, 0.0]
            continue
        carry[0] += sx * 0.01
        carry[1] += sy * 0.01
        dx, dy = int(carry[0]), int(carry[1])
        carry[0] -= dx
        carry[1] -= dy
        if dx or dy:
            mouse.move(dx, dy)

def draw_circle():
    """Moves the pointer once around a circle centred on where it is now, then back to the centre."""
    global circling
    circling = True
    cx, cy = mouse.position          # the point you're highlighting
    steps = 60
    for i in range(steps + 1):
        angle = 2 * math.pi * i / steps
        mouse.position = (int(cx + CIRCLE_RADIUS * math.cos(angle)),
                          int(cy + CIRCLE_RADIUS * math.sin(angle)))
        time.sleep(CIRCLE_TIME / steps)
    mouse.position = (cx, cy)        # back to the middle
    circling = False


def setup(send):
    global send_line
    send_line = send
    threading.Thread(target=mover, daemon=True).start()


def on_line(line):
    """Return True if this line was for you."""
    global last_msg

    if line == "CIRCLE":
        if not circling:
            threading.Thread(target=draw_circle, daemon=True).start()
        return True
    
    if not line.startswith("TILT:"):
        return False
    try:
        x, y = (int(v) for v in line[5:].split(","))
    except ValueError:
        return True  # ours but garbled: ignore, never crash
    if SWAP_XY:
        x, y = y, x
    if INVERT_X:
        x = -x
    if INVERT_Y:
        y = -y
    with lock:
        speed[0] = to_speed(x)
        speed[1] = to_speed(y)
        last_msg = time.time()
    return True