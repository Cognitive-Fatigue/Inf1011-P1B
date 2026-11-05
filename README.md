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
