# Installation and setup
Follow the tutorials below to install the required libraries  
https://learn.tinycircuits.com/Processors/TinyScreen+_Setup_Tutorial/#downloads  
https://learn.tinycircuits.com/Communication/Bluetooth-Low-Energy_TinyShield_Tutorial/  

On laptop:
- `pip install pyserial bleak pynput`
- `git clone -b Pair-A https://github.com/Cognitive-Fatigue/Inf1011-P1B.git`
- STBLE fix (do this once): the installed library's `src/arduino_bluenrg_ble.cpp` has empty functions, so Bluetooth never starts. Copy `stble-patch/arduino_bluenrg_ble.cpp` from INF1011-P1B over it, then rebuild. Without it, the watch still compiles and USB works, but no Bluetooth.

To run:
`python main.py` on laptop  
In arduino IDE, top left corner: file -> open -> Inf1011-P1B -> watch -> watch.ino  
Set Tools -> Board -> TinyCircuit SAMD Board -> Tinyscreen+  
Set Tools -> port -> port that you have inserted your tinycircuit to  
Upload code into tinycircuit via USB cable  


Verify that tinycircuit is running:
tinycircuit displays: the button labels in the corners and the connection state in the centre (temporary test screen until the status screen is added)
Laptop prints `Watch connected on ...`
Turn on and off the tinycircuit to verify that the bluetooth connection is working as it should


# TinyKey

The TinyScreen+ watch works as a security key and a remote for the laptop.
Watch (Arduino) <-- USB now, Bluetooth later --> Laptop (Python)

## Who works where

| Pair | Watch | Laptop |
|---|---|---|
| A – link and merging | `watch/watch.ino` | `laptop/link.py`, `laptop/main.py` |
| B – security | `watch/security.ino` | `laptop/security.py` |
| C – remote | `watch/remote.ino` | `laptop/remote.py` |

Pairs B and C never touch USB or Bluetooth. Use only these:

- Watch: `sendLine("CMD:NEXT")` to send. Your `...OnLine(line)` is called with each whole line received; return `true` if it was yours.
- Laptop: `send_line("CHAL:...")` to send. Your `on_line(line)` is called with each whole line; return `True` if it was yours.

## Running

- Get the code: `git clone -b Pair-A https://github.com/Cognitive-Fatigue/Inf1011-P1B.git`, then `cd Inf1011-P1B`. Run every `python laptop/...` command below from this folder (not from where the terminal opens, e.g. `C:\WINDOWS\system32`).
- Watch: open `watch/watch.ino` in the Arduino IDE (the other tabs load with it), board "TinyScreen+", upload. Needs the STBLE library and the BLE TinyShield on the stack.
- STBLE fix (do this once): the installed library's `src/arduino_bluenrg_ble.cpp` has empty functions, so Bluetooth never starts. Copy `stble-patch/arduino_bluenrg_ble.cpp` over it (library folder: Arduino `libraries/STBLE/src/`), then rebuild. Without it, the watch still compiles and USB works, but no Bluetooth.
- Laptop: `pip install pyserial bleak pynput`, then `python laptop/main.py` (Bluetooth; it finds the watch by the name `TinyKey`). Add `--usb` to use the cable instead. Turn the laptop's Bluetooth on first.
- Several watches in one room: they all advertise as `TinyKey`, and a laptop connects to the first one it finds. Give yours its own name in both `watch/ble.ino` (`BLE_NAME`) and `laptop/link.py` (`WATCH_NAME`), e.g. `TinyKey2`, and don't commit that change.
- Link test: `python laptop/echo_test.py` sends 100 random lines and checks they all come back. `--usb` tests the cable; `--reconnect 10` also drops the Bluetooth link 10 times and checks each reconnect takes under 5 s.
- Disconnect test on battery: upload over USB, unplug the cable and switch the watch on.
  - `python laptop/echo_test.py 20 --reconnect 10`: the watch drops the link itself. Each reconnect prints its time (e.g. `reconnect 3: 2.2 s`, `FAIL` if 5 s or more), then `10/10 reconnects under 5 s` if all passed.
  - `python laptop/main.py`, then walk away with the watch (10–30 m or through walls) or put it in a metal tin: the laptop should print `Watch disconnected`, then `Watch connected on ...` a few seconds after you bring it back.
- Known issue: most reconnects take about 2 s, but roughly 1 in 27 test drops (3 of about 80) takes about 15 s. When the watch ends the link (DROP), the laptop sees it go down within 0.2 s, but the watch's Bluetooth chip sometimes only reports the disconnect after its own 12 s timeout (reason 0x22), and the watch does not advertise again until then. Walking out of range ends the link a different way (a timeout on both sides), so this may not happen in real use; not yet tested.

## Credits

- `watch/ble.ino` is based on UART.ino from the STBLE library's UARTPassThrough example (Ben Rose, TinyCircuits).

## Message rules

- Format `TYPE:payload`, plain ASCII, no spaces, one message per line ending in `\n`.
- Type names uppercase. Hex values lowercase.
- Unknown messages are ignored, never crash. Lines over 127 characters are dropped.
- Same text over USB and Bluetooth. Over Bluetooth, Pair A splits lines into packets of at most 20 bytes and joins them back.

### Laptop to watch

| Message | Sent by | Meaning |
|---|---|---|
| `CHAL:<32 hex>:VAULT` | B | "Approve opening the vault?" The hex is a random 16-byte challenge. |
| `ECHO:<text>` | A | Link test. The watch sends the same line back. |

### Watch to laptop

| Message | Sent by | Meaning |
|---|---|---|
| `RESP:<64 hex>` | B | Approved by double-tap. HMAC-SHA256 result. |
| `RESP:REJECT` | B | No double-tap within 10 s. |
| `CMD:NEXT` / `CMD:PREV` | C | Right / left arrow (slides). Upper right / lower right, long press. |
| `CMD:PLAY` | C | Media play/pause. Upper left, short press. |
| `CMD:SKIP` | C | Media next track. Upper left, long press. |
| `CMD:VOLUP` / `CMD:VOLDN` | C | Media volume up / down. Upper right / lower right, short press. |

One button press sends exactly one message.

### Proposed, not agreed yet

- `PING` (laptop) / `PONG` (watch) every second, so the laptop notices a lost watch within the 5 s lock target.

### HMAC

- Key: shared 32-byte key, hardcoded on both sides (known limitation, stated in the video).
- Input: the 16 raw challenge bytes (not the hex text), then the ASCII bytes of the action, e.g. `VAULT`.
- Output: HMAC-SHA256, sent as 64 lowercase hex characters.
- Python reference: `hmac.new(key, challenge + b"VAULT", hashlib.sha256).hexdigest()`


## Pair C – remote and extra features

### Buttons (Media mode)

Each label on screen sits next to the button it belongs to. **Tap** = short press, **hold** = about 0.6 s.

| Button | Tap | Hold |
|---|---|---|
| Upper left | `PLAY` play/pause | `SKIP` next track |
| Upper right | `VOL+` volume up | `NEXT` next slide |
| Lower right | `VOL-` volume down | `BACK` previous slide |
| Lower left | `MODE` (Pair A's mode switch) | – |
| **Upper right + lower right together** | Reset the presentation timer | – |

One press sends exactly one message. Holding sends the long command once (nothing extra on release).

### Screen layout

```
PLAY        87%        VOL+    ← battery %
SKIP       03:25       NEXT    ← presentation timer
          (status)             ← Pair A's area
           [NEXT]      VOL-    ← press feedback
MODE                   BACK
```

### Extra features

**Press feedback** (`watch/remote.ino`)
After each command, its name (e.g. `NEXT`, `VOLUP`) shows at the bottom middle for 0.7 s, so you can see the press worked.

**Battery %** (`watch/battery.ino`)
Shows the battery level at the top middle, measured every 10 s from the battery voltage (4.2 V = 100%, 3.5 V = 0%).
- On USB it shows about 100% because the cable is charging the battery.
- It is an estimate, not exact.

**Presentation timer** (`watch/timer.ino`)
Starts at the first `NEXT` and shows minutes:seconds. Later `NEXT` presses don't restart it.
- **Reset:** press the two right buttons (VOL+ and VOL-) together. The timer disappears and the next `NEXT` starts it from 00:00. Neither button sends its own command during the reset.
- It also resets when the watch is switched off.

**Early lock when you walk away** (`watch/signal.ino` + `laptop/proximity.py`)
Locks the laptop when the Bluetooth signal stays weak, before the link fully drops.
- Once a second, the watch measures the signal strength (RSSI) and sends `RSSI:-72`.
- The laptop averages the last 4 readings. If the average is weaker than **-85 dBm** (about 4-5 m), it locks Windows once (same as Windows + L).
- It only locks again after the signal has been strong again (better than -70 dBm, i.e. you came back).
- Bluetooth only: there is no signal reading over USB.
- Unlocking is always done by Windows (PIN/password). The watch can lock but never unlock.

Signal measured in testing (watch on its own, no case):

| Distance | Signal |
|---|---|
| Next to laptop | -35 to -37 dBm |
| 1 m | -50 to -55 dBm |
| 2 m | -70 to -73 dBm |
| 5 m | -88 to -90 dBm |
| 7-8 m behind a wall | disconnected |

### Setup for early lock (Pair A: `laptop/main.py`)

Add these 3 lines so `RSSI:` messages reach `proximity.py`:

```python
import proximity
```
```python
    for handler in (security.on_line, remote.on_line, proximity.on_line):
```
```python
    proximity.setup(link.send_line)
```

### Settings

| File | Setting | Default | Meaning |
|---|---|---|---|
| `laptop/proximity.py` | `ENABLED` | `True` | `False` turns early lock off (e.g. if it misbehaves on demo day) |
| `laptop/proximity.py` | `LOCK_BELOW` | `-85` | Lock when the average is weaker than this. Locks too easily: try `-88`. Too slow: try `-80` |
| `laptop/proximity.py` | `ARM_ABOVE` | `-70` | Ready to lock again once the average is stronger than this |
| `laptop/proximity.py` | `SAMPLES` | `4` | How many readings (seconds) to average |
| `watch/signal.ino` | `SIGNAL_SHOW_ON_SCREEN` | `false` | `true` shows the live dBm number in the middle of the screen (testing only) |

### New message

| Message | Direction | Meaning |
|---|---|---|
| `RSSI:<number>` | watch to laptop | Bluetooth signal strength in dBm, once a second, e.g. `RSSI:-72` |

### How to test

1. Upload `watch/watch.ino` with the USB cable, then unplug it.
2. Run `python laptop/main.py` (Bluetooth). It prints `signal -38 dBm, average -37` every second.
3. Buttons: tap and hold each one in PowerPoint (F5) and a music player. The command name should flash at the bottom of the screen.
4. Timer: hold NEXT and it starts counting. Press both right buttons and it disappears.
5. Early lock: walk slowly to about 5 m and stand still. After about 4 s the terminal prints `Signal weak: locking laptop` and Windows locks.
6. False-alarm check: sit at the laptop for 2-3 minutes using it normally. It should never lock.

### Known limitations

- **Walking away fast** can take you out of range (about 7 m) before the 4-second average reacts. That case is covered by Pair A's lock on disconnect.
- **The 3D-printed case or your body** can weaken the signal. Re-check the numbers above with the watch in its case and adjust `LOCK_BELOW` if needed.
- **Battery %** is an estimate and shows about 100% while charging.
- **The timer reset combo** isn't shown on screen (no space), so mention it in the demo.
- **`remoteActive()`** still always returns `true`. It needs Pair A's `isMediaMode()` and Pair B's `isApprovalActive()` so the buttons stay silent in Key mode and during approval.