// Pair C: buttons to commands. Send with sendLine("CMD:NEXT") etc., one message per press.

const unsigned long REMOTE_DEBOUNCE_MS = 30;  // a level must hold this long to count, so bounces are ignored
const unsigned long REMOTE_LONG_MS = 600;  // held this long: send the long command once, nothing on release

struct RemoteButton {
  uint8_t mask;
  const char *shortCmd;
  const char *longCmd;
  bool raw;  // last level read
  bool down;  // debounced level
  bool handled;  // this press has sent its message, or must stay silent
  bool blocked;  // this press overlapped a mode change, Key mode or approval: silent until released
  unsigned long changedAt;
  unsigned long downAt;
};

// Lower left is Pair A's mode switch (watch.ino).
RemoteButton remoteButtons[] = {
  {TSButtonUpperLeft, "CMD:PLAY", "CMD:SKIP"},
  {TSButtonUpperRight, "CMD:VOLUP", "CMD:NEXT"},
  {TSButtonLowerRight, "CMD:VOLDN", "CMD:PREV"},
};

bool remoteModeJustChanged = false;

// False in Key mode or with no synced laptop (Pair A's mode check), during approval (Pair B's
// approval check), and while the pointer is held (other buttons don't send their normal commands).
bool remoteActive() {
  return appModeIsMedia() && appHasLaptopLink() && !securityIsApprovalActive() && !pointerHeld();
}

// Called by Pair A on a mode change or link loss and by Pair B when an approval starts:
// any button already down stays silent until it is released.
void remoteCancelHeld() {
  for (RemoteButton &b : remoteButtons) {
    if (b.down || b.raw) b.handled = true;
  }
  remoteModeJustChanged = true;
}

// Sends a command and lights the pressed corner on screen, so the user sees the press worked.
void remoteSend(const char *cmd) {
  if (!remoteActive()) return;
  sendLine(cmd);
  if (strcmp(cmd, "CMD:NEXT") == 0) timerStart();  // 3a: first NEXT starts the timer
  uiPressed(cmd);
}

// Each corner shows the short press as a bright icon and the long press as a dim icon under it (ui.ino).
void remoteDrawLabels() {
  for (uint8_t c = 0; c < 3; c++) uiDrawCorner(c, false);
}

void remoteSetup() {
  pointerSetup();    // NEW (test): laser pointer
}

void remoteLoop() {
  uint8_t pressed = display.getButtons();
  unsigned long now = millis();
  for (RemoteButton &b : remoteButtons) {
    bool raw = pressed & b.mask;
    if (remoteModeJustChanged && raw) b.blocked = true;
    if (raw != b.raw) {
      b.raw = raw;
      b.changedAt = now;
    }
    if (raw != b.down && now - b.changedAt >= REMOTE_DEBOUNCE_MS) {
      b.down = raw;
      if (b.down) {
        b.downAt = now;
        b.handled = b.blocked || !remoteActive();
      } else if (!b.handled && remoteActive()) {
        remoteSend(b.shortCmd);
      }
      if (!b.down) b.blocked = false;
    }
    if (!remoteActive() && (raw || b.down)) {  // a press that overlaps Key mode or approval sends nothing
      b.blocked = true;
      b.handled = true;
    }
    if (b.down && !b.handled && now - b.downAt >= REMOTE_LONG_MS) {
      remoteSend(b.longCmd);
      b.handled = true;
    }
  }
  remoteModeJustChanged = false;

  // New: hold VOL+ and VOL- together to reset the presentation timer
  RemoteButton &up = remoteButtons[1];  // upper right (VOL+ / NEXT)
  RemoteButton &dn = remoteButtons[2];  // lower right (VOL- / BACK)
  if (up.down && dn.down && !(up.handled && dn.handled)) {
    up.handled = true;  // so neither button ALSO sends its own command
    dn.handled = true;
    timerReset();
  }
  batteryLoop();     // NEW
  timerLoop();       // 3c: update the timer every loop
  clockLoop();       // clock shown while the timer isn't running
  signalLoop();      // TEST: show signal strength
  pointerLoop();     // NEW (test): laser pointer
}

// Return true if this line was for you.
bool remoteOnLine(const char *line) {
  return clockOnLine(line);  // "TIME:..." from the laptop sets the clock
}
