# Installation
Install Python and follow the [TinyScreen+ setup tutorial](https://learn.tinycircuits.com/Processors/TinyScreen+_Setup_Tutorial/#downloads) and [BLE TinyShield tutorial](https://learn.tinycircuits.com/Communication/Bluetooth-Low-Energy_TinyShield_Tutorial/). The one-command setup installs the laptop's Python dependencies.

The installed STBLE library's `src/arduino_bluenrg_ble.cpp` has empty functions, so Bluetooth needs the included patch. Copy `stble-patch/arduino_bluenrg_ble.cpp` over the installed `Arduino/libraries/STBLE/src/arduino_bluenrg_ble.cpp`, then rebuild. Without it, USB works but BLE does not.


# TinyKey

The TinyScreen+ watch works as a security key and a remote for the laptop.
Watch (Arduino) <-- USB or Bluetooth --> Laptop (Python)

## One-command setup

Prerequisites: install Python, upload the updated watch firmware, and connect exactly one watch by USB. Stop any existing TinyKey controller before rerunning setup.

- `python setup.py` installs dependencies, enrolls the USB watch, and starts the BLE controller.
- `python setup.py --usb` installs dependencies, enrolls the USB watch, and starts the USB controller.

Setup can also be invoked by absolute path from another working directory.

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

- Get the code: `git clone -b Pair-A https://github.com/Cognitive-Fatigue/Inf1011-P1B.git`, then `cd Inf1011-P1B`.
- Watch: open `watch/watch.ino` in the Arduino IDE (the other tabs load with it), board "TinyScreen+", upload. Needs the STBLE library and the BLE TinyShield on the stack.
- STBLE fix (do this once): the installed library's `src/arduino_bluenrg_ble.cpp` has empty functions, so Bluetooth never starts. Copy `stble-patch/arduino_bluenrg_ble.cpp` over it (library folder: Arduino `libraries/STBLE/src/`), then rebuild. Without it, the watch still compiles and USB works, but no Bluetooth.
- Later startup: run `python laptop/main.py` for BLE or `python laptop/main.py --usb` for USB. The watch must have the updated firmware uploaded; older firmware does not answer the ID query.
- USB being connected does not switch a running controller from BLE to USB. The watch's USB/BLE status reports detected links and does not prove which transport the controller selected. Each laptop saves its own watch ID; two laptops enrolled to the same watch can still compete for its connection. Matching is identity selection, not authentication or exclusive ownership.
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
| `SYNC` | A | Request the watch's current mode after each connection. |
| `ECHO:<text>` | A | Link test. The watch sends the same line back. |

### Watch to laptop

| Message | Sent by | Meaning |
|---|---|---|
| `RESP:<64 hex>` | B | Approved by double-tap. HMAC-SHA256 result. |
| `RESP:REJECT` | B | No double-tap within 10 s. |
| `CMD:NEXT` / `CMD:PREV` | C | Right / left arrow (slides). |
| `CMD:PLAY` | C | Media play/pause. |
| `CMD:VOLUP` / `CMD:VOLDN` | C | Media volume up / down (long press). |
| `MODE:KEY` / `MODE:MEDIA` | A | Current mode; the laptop authorizes remote commands after this reply. |

One button press sends exactly one message.

The lower-left button toggles Key/Media. Remote commands are accepted only in Media mode;
the laptop locks its Windows session after a connected watch disconnects.

### HMAC

- Key: shared 32-byte key, hardcoded on both sides (known limitation, stated in the video).
- Input: the 16 raw challenge bytes (not the hex text), then the ASCII bytes of the action, e.g. `VAULT`.
- Output: HMAC-SHA256, sent as 64 lowercase hex characters.
- Python reference: `hmac.new(key, challenge + b"VAULT", hashlib.sha256).hexdigest()`
