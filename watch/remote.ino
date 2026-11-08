// Pair C: buttons to commands. Send with sendLine("CMD:NEXT") etc., one message per press.

const unsigned long REMOTE_DEBOUNCE_MS = 30;  // a level must hold this long to count, so bounces are ignored
const unsigned long REMOTE_LONG_MS = 600;  // held this long: send the long command once, nothing on release
const unsigned long REMOTE_FEEDBACK_MS = 700;  // how long the confirmation stays on screen
unsigned long remoteFeedbackAt = 0;
bool remoteFeedbackShown = false;

// Sends a command AND shows its name at the bottom middle, so the user sees the press worked.
void remoteSend(const char *cmd) {
  sendLine(cmd);
  const char *name = cmd + 4;  // skip the "CMD:" part, e.g. "CMD:NEXT" -> "NEXT"
  display.clearWindow(28, 44, 40, 10);  // wipe the feedback box (x, y, width, height)
  display.setCursor((96 - display.getPrintWidth((char *)name)) / 2, 44);  // centre the word
  display.print(name);
  remoteFeedbackAt = millis();  // remember when we showed it
  remoteFeedbackShown = true;
}

// Wipes the feedback once 0.7 s have passed.
void remoteFeedbackLoop() {
  if (remoteFeedbackShown && millis() - remoteFeedbackAt >= REMOTE_FEEDBACK_MS) {
    display.clearWindow(28, 44, 40, 10);
    remoteFeedbackShown = false;
  }
}

struct RemoteButton {
  uint8_t mask;
  const char *shortCmd;
  const char *longCmd;
  bool raw;  // last level read
  bool down;  // debounced level
  bool handled;  // this press has sent its message, or must stay silent
  unsigned long changedAt;
  unsigned long downAt;
};

// Lower left is Pair A's mode switch.
RemoteButton remoteButtons[] = {
  {TSButtonUpperLeft, "CMD:PLAY", "CMD:SKIP"},
  {TSButtonUpperRight, "CMD:VOLUP", "CMD:NEXT"},
  {TSButtonLowerRight, "CMD:VOLDN", "CMD:PREV"},
};

// Week 2: false in Key mode (Pair A's mode check) and during approval (Pair B's approval check).
bool remoteActive() {
  return true;
}

void remoteLabel(const char *text, uint8_t y, bool right) {
  display.setCursor(right ? 96 - display.getPrintWidth((char *)text) : 0, y);
  display.print(text);
}

// Each corner shows the short press, then the long press under it. Pair A calls this when redrawing in Media mode.
void remoteDrawLabels() {
  remoteLabel("PLAY", 0, false);
  remoteLabel("SKIP", 10, false);
  remoteLabel("VOL+", 0, true);
  remoteLabel("NEXT", 10, true);
  remoteLabel("MODE", 54, false);
  remoteLabel("VOL-", 44, true);
  remoteLabel("BACK", 54, true);
}

// TEMPORARY until Pair A's status screen lands: remove this function and its calls in remoteSetup() and remoteLoop().
void remoteTestScreen() {
  static int shown = -1;
  int state = !blePresent ? 0 : bleConnected ? 2 : 1;
  if (state == shown) return;
  shown = state;
  const char *status = state == 0 ? "USB only" : state == 2 ? "BLE connected" : "BLE waiting";
  display.clearWindow(0, 0, 96, 64);
  remoteDrawLabels();
  display.setCursor((96 - display.getPrintWidth((char *)"MEDIA")) / 2, 22);
  display.print("MEDIA");
  display.setCursor((96 - display.getPrintWidth((char *)status)) / 2, 32);
  display.print(status);
}

void remoteSetup() {
  remoteTestScreen();
}

void remoteLoop() {
  remoteTestScreen();
  uint8_t pressed = display.getButtons();
  unsigned long now = millis();
  for (RemoteButton &b : remoteButtons) {
    bool raw = pressed & b.mask;
    if (raw != b.raw) {
      b.raw = raw;
      b.changedAt = now;
    }
    if (raw != b.down && now - b.changedAt >= REMOTE_DEBOUNCE_MS) {
      b.down = raw;
      if (b.down) {
        b.downAt = now;
        b.handled = false;
      } else if (!b.handled && remoteActive()) {
        remoteSend(b.shortCmd);
      }
    }
    if (!remoteActive()) b.handled = true;  // a press that overlaps Key mode or approval sends nothing
    if (b.down && !b.handled && now - b.downAt >= REMOTE_LONG_MS) {
      remoteSend(b.longCmd);
      b.handled = true;
    }
  }
  remoteFeedbackLoop();
}

// Return true if this line was for you.
bool remoteOnLine(const char *line) {
  return false;
}
