"""Pair C: turn CMD lines into key presses (pynput), and answer the watch's TIME? with the time.

Pair A's mode sync: the watch reports MODE:KEY / MODE:MEDIA, and CMD lines only press keys
while main.py says the remote is active (connected, synced in Media mode, no approval pending).
"""
import time

send_line = None
keyboard = None  # pynput's Controller, created on the first key press
is_active = lambda: False
connected = False
mode_authorized = False
media_mode = False

KEYS = {
    "CMD:NEXT": "right",
    "CMD:PREV": "left",
    "CMD:PLAY": "media_play_pause",
    "CMD:SKIP": "media_next",
    "CMD:VOLUP": "media_volume_up",
    "CMD:VOLDN": "media_volume_down",
}


def setup(send, active=None):
    global send_line, is_active
    send_line = send
    is_active = active or (lambda: connected and mode_authorized and media_mode)


def set_connected(value):
    global connected, mode_authorized, media_mode
    connected = bool(value)
    mode_authorized = False
    media_mode = False


def set_mode(mode):
    global mode_authorized, media_mode
    mode_authorized = connected and mode in ("KEY", "MEDIA")
    media_mode = mode == "MEDIA" and mode_authorized


def on_line(line):
    """Return True if this line was for you."""
    if line == "TIME?":  # the watch asks for the time to show its clock
        send_line(time.strftime("TIME:%H:%M:%S"))
        return True
    if line in ("MODE:KEY", "MODE:MEDIA"):
        set_mode(line[5:])
        return True
    key = KEYS.get(line)
    if key is None:
        return False
    if not is_active():
        return True
    global keyboard
    if keyboard is None:
        from pynput.keyboard import Controller, Key
        keyboard = Controller()
        for command, name in KEYS.items():
            KEYS[command] = getattr(Key, name)
        key = KEYS[line]
    keyboard.tap(key)
    return True
