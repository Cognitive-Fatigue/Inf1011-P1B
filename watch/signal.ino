// signal.ino - TEST: shows the Bluetooth signal strength (RSSI) on screen once a second.
// While testing it temporarily covers the word "MEDIA" in the middle of the screen.

unsigned long signalCheckedAt = 0;

void signalLoop() {
  if (!bleConnected) return;                      // no Bluetooth connection: nothing to measure
  if (millis() - signalCheckedAt < 1000) return;  // only once per second
  signalCheckedAt = millis();

  uint16_t handle = bleConnHandle;  // a COPY of the connection ID (the function may change it)
  int8_t rssi = 0;
  int status = hci_read_rssi(&handle, &rssi);  // ask the Bluetooth chip for the signal strength

  char text[16];
  if (status == 0) sprintf(text, "%d dBm", rssi);  // 0 = success, e.g. "-58 dBm"
  else sprintf(text, "err %d", status);             // something went wrong: show the error code
  display.clearWindow(20, 22, 56, 9);
  display.setCursor((96 - display.getPrintWidth(text)) / 2, 22);
  display.print(text);
}