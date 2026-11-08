"""Test pointer.py WITHOUT a watch: pretends to send TILT messages and moves your mouse."""
import time

import pointer

pointer.setup(None)  # starts the background mover

# (tilt x, tilt y, seconds): like holding the watch tilted for a while
moves = [
    (30, 0, 2),    # slight tilt right  -> slow move right
    (100, 0, 1),   # big tilt right     -> fast move right
    (0, -60, 2),   # medium tilt up     -> medium move up
    (-100, 0, 1),  # big tilt left      -> fast move left
    (10, 10, 2),   # tiny tilt (inside dead zone) -> should NOT move
    (0, 0, 1),     # level              -> stop
]

print("Hands off the mouse! Starting in 3 seconds...")
time.sleep(3)
for x, y, secs in moves:
    print(f"Pretending tilt {x},{y} for {secs} s")
    end = time.time() + secs
    while time.time() < end:
        pointer.on_line(f"TILT:{x},{y}")  # same message the watch would send
        time.sleep(0.05)                  # 20 times a second, like the watch
print("Done")