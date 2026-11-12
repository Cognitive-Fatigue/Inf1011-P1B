// timer.ino - presentation timer and clock (Pair C extra feature).
// The timer starts at the first NEXT press; while it runs, ui.ino shows it in the middle of the
// media screen. Otherwise the middle shows the clock.

unsigned long timerStartAt = 0;  // when the talk started (millis)
bool timerRunning = false;
long timerShown = -1;            // the second last sent to the screen; -1 = send again


// Called on every NEXT. Only the first one starts the timer.
void timerStart() {
  if (timerRunning) return;  // already running: ignore
  timerRunning = true;
  timerStartAt = millis();
  timerShown = -1;
}

// Call every loop. Only updates the screen when the second changes.
void timerLoop() {
  if (!timerRunning) return;                    // not started yet: the screen shows 00:00
  long secs = (millis() - timerStartAt) / 1000; // seconds since the first NEXT
  if (secs == timerShown) return;               // same second: nothing to do
  timerShown = secs;
  uiSetTimer(secs, true);
}

// Stops the timer and puts 00:00 back on the screen. The next NEXT starts it again.
void timerReset() {
  timerRunning = false;
  timerShown = -1;
  uiSetTimer(0, false);
}

// ---------- Clock ----------
// The watch has no time of its own: it asks the laptop ("TIME?") and counts on from the reply.
const unsigned long CLOCK_ASK_MS = 5000;        // ask every 5 s until the laptop answers
const unsigned long CLOCK_RESYNC_MS = 3600000;  // then once an hour, to correct drift
long clockSyncSecs = -1;        // seconds since midnight in the last reply; -1 = not known yet
unsigned long clockSyncAt = 0;  // millis() when that reply arrived
unsigned long clockAskedAt = 0;
bool clockAsked = false;
long clockShown = -2;           // minute last sent to the screen

// "TIME:14:32:05" from the laptop. Returns true for any TIME line.
bool clockOnLine(const char *line) {
  if (strncmp(line, "TIME:", 5) != 0) return false;
  int h, m, s;
  if (sscanf(line + 5, "%d:%d:%d", &h, &m, &s) == 3 && h >= 0 && h < 24 && m >= 0 && m < 60 && s >= 0 && s < 60) {
    clockSyncSecs = h * 3600L + m * 60 + s;
    clockSyncAt = millis();
  }
  return true;  // ours even if garbled: ignore it, never crash
}

// Call every loop.
void clockLoop() {
  unsigned long now = millis();
  bool linked = bleConnected || SerialUSB.dtr();  // dtr(): `if (SerialUSB)` waits 10 ms every call
  bool stale = clockSyncSecs < 0 || now - clockSyncAt >= CLOCK_RESYNC_MS;
  if (linked && stale && (!clockAsked || now - clockAskedAt >= CLOCK_ASK_MS)) {
    clockAsked = true;
    clockAskedAt = now;
    sendLine("TIME?");
  }
  long minute = clockSyncSecs < 0 ? -1 : ((clockSyncSecs + (long)((now - clockSyncAt) / 1000)) / 60) % 1440;
  if (minute != clockShown) {
    clockShown = minute;
    uiSetClock(minute);
  }
}
