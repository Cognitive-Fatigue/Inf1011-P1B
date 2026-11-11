// Pair C core controls: short and long presses in Media mode.
const unsigned long REMOTE_DEBOUNCE_MS = 30;
const unsigned long REMOTE_LONG_MS = 600;
const unsigned long REMOTE_FEEDBACK_MS = 700;
unsigned long remoteFeedbackAt = 0;
bool remoteFeedbackShown = false;

struct RemoteButton {
  uint8_t mask;
  const char *shortCmd;
  const char *longCmd;
  bool raw;
  bool down;
  bool handled;
  bool blocked;
  unsigned long changedAt;
  unsigned long downAt;
};

RemoteButton remoteButtons[] = {
  {TSButtonUpperLeft, "CMD:PLAY", "CMD:SKIP"},
  {TSButtonUpperRight, "CMD:VOLUP", "CMD:NEXT"},
  {TSButtonLowerRight, "CMD:VOLDN", "CMD:PREV"},
};

bool remoteModeRaw = false, remoteModeDown = false;
bool remoteModeJustChanged = false;
unsigned long remoteModeChangedAt = 0;

bool remoteActive() {
  return appModeIsMedia() && appHasLaptopLink() && !securityIsApprovalActive();
}

void remoteCancelHeld() {
  for (RemoteButton &b : remoteButtons) {
    if (b.down || b.raw) b.handled = true;
  }
  remoteModeJustChanged = true;
  remoteFeedbackShown = false;
}

void remoteSend(const char *cmd) {
  if (!remoteActive()) return;
  sendLine(cmd);
  const char *name = cmd + 4;
  display.clearWindow(28, 44, 40, 10);
  display.setCursor((96 - display.getPrintWidth((char *)name)) / 2, 44);
  display.print((char *)name);
  remoteFeedbackAt = millis();
  remoteFeedbackShown = true;
}

void remoteLabel(const char *text, uint8_t y, bool right) {
  display.setCursor(right ? 96 - display.getPrintWidth((char *)text) : 0, y);
  display.print((char *)text);
}

void remoteDrawLabels() {
  remoteLabel("PLAY", 0, false);
  remoteLabel("SKIP", 10, false);
  remoteLabel("VOL+", 0, true);
  remoteLabel("NEXT", 10, true);
  remoteLabel("MODE", 54, false);
  remoteLabel("VOL-", 44, true);
  remoteLabel("BACK", 54, true);
}

void remoteModeButton(uint8_t pressed, unsigned long now) {
  bool raw = pressed & TSButtonLowerLeft;
  if (raw != remoteModeRaw) {
    remoteModeRaw = raw;
    remoteModeChangedAt = now;
  }
  if (raw != remoteModeDown && now - remoteModeChangedAt >= REMOTE_DEBOUNCE_MS) {
    remoteModeDown = raw;
    if (remoteModeDown && !securityIsApprovalActive()) {
      appSetMode(appModeIsMedia() ? MODE_KEY : MODE_MEDIA);
    }
  }
}

void remoteSetup() {}

void remoteLoop() {
  unsigned long now = millis();
  uint8_t pressed = display.getButtons();
  remoteModeButton(pressed, now);

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
    if (!remoteActive() && (raw || b.down)) {
      b.blocked = true;
      b.handled = true;
    }
    if (b.down && !b.handled && now - b.downAt >= REMOTE_LONG_MS) {
      remoteSend(b.longCmd);
      b.handled = true;
    }
  }
  remoteModeJustChanged = false;

  if (remoteFeedbackShown && now - remoteFeedbackAt >= REMOTE_FEEDBACK_MS) {
    display.clearWindow(28, 44, 40, 10);
    remoteFeedbackShown = false;
    appStatusDirty = true;
  }
}

bool remoteOnLine(const char *line) {
  return false;
}
