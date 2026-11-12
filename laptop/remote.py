"""Pair C: turn CMD lines into key presses (pynput), and answer the watch's TIME? with the time."""
import time

from pynput.keyboard import Controller, Key

send_line = None
keyboard = Controller()

KEYS = {
    "CMD:NEXT": Key.right,
    "CMD:PREV": Key.left,
    "CMD:PLAY": Key.media_play_pause,
    "CMD:SKIP": Key.media_next,
    "CMD:VOLUP": Key.media_volume_up,
    "CMD:VOLDN": Key.media_volume_down,
}


def setup(send):
    global send_line
    send_line = send


def on_line(line):
    """Return True if this line was for you."""
    if line == "TIME?":  # the watch asks for the time to show its clock
        send_line(time.strftime("TIME:%H:%M:%S"))
        return True
    key = KEYS.get(line)
    if key is None:
        return False
    keyboard.tap(key)
    return True
