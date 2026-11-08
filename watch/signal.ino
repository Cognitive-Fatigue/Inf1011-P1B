// signal.ino - Pair C extra feature: Bluetooth signal strength (RSSI).
// Once a second the watch measures the signal and sends "RSSI:-72" to the laptop.
// The laptop (proximity.py) locks the screen when the signal stays weak.

const bool SIGNAL_SHOW_ON_SCREEN = true;  // TEST: also show the number in the middle of the screen
unsigned long signalCheckedAt = 0;

void signalLoop() {
  if (!bleConnected) return;                      // no Bluetooth connection: nothing to measure
  if (millis() - signalCheckedAt < 1000) return;  // once per second
  signalCheckedAt = millis();

  uint16_t handle = bleConnHandle;  // a COPY of the connection ID (the function may change it)
  int8_t rssi = 0;
  if (hci_read_rssi(&handle, &rssi) != 0) return;  // no reading this time: try again next second
  if (rssi == 127) return;                         // 127 means "not available"

  char msg[12];
  sprintf(msg, "RSSI:%d", rssi);  // e.g. "RSSI:-72"
  sendLine(msg);                  // send it to the laptop

  if (SIGNAL_SHOW_ON_SCREEN) {
    char text[12];
    sprintf(text, "%d dBm", rssi);
    display.clearWindow(20, 22, 56, 9);
    display.setCursor((96 - display.getPrintWidth(text)) / 2, 22);
    display.print(text);
  }
}