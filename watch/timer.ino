// timer.ino - presentation timer (Pair C extra feature).
// Starts at the first NEXT press and shows minutes:seconds on row 2 of the screen.

unsigned long timerStartAt = 0;  // when the talk started (millis)
bool timerRunning = false;
long timerShown = -1;            // the second currently on screen; -1 = draw again


// Called on every NEXT. Only the first one starts the timer.
void timerStart() {
  if (timerRunning) return;  // already running: ignore
  timerRunning = true;
  timerStartAt = millis();
  timerShown = -1;
}

// Call when the screen was redrawn, so the time gets drawn again.
void timerRedraw() {
  timerShown = -1;
}

// Call every loop. Only redraws when the second changes.
void timerLoop() {
  if (!timerRunning) return;                    // not started yet: show nothing
  long secs = (millis() - timerStartAt) / 1000; // seconds since the first NEXT
  if (secs == timerShown) return;               // same second: nothing to do
  timerShown = secs;

  char text[8];
  sprintf(text, "%02ld:%02ld", (secs / 60) % 100, secs % 60);  // e.g. "03:25"
  display.clearWindow(30, 10, 36, 9);           // wipe the old time
  display.setCursor((96 - display.getPrintWidth(text)) / 2, 10);  // centre it on row 2
  display.print(text);
}

// Stops the timer and wipes it off the screen. The next NEXT starts it again from 00:00.
void timerReset() {
  timerRunning = false;
  timerShown = -1;
  display.clearWindow(30, 10, 36, 9);
}