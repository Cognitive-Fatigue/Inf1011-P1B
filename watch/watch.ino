// TinyKey watch. Pair A owns this file: the link to the laptop (USB and Bluetooth, see ble.ino).
// Pairs B and C work in security.ino and remote.ino and only use sendLine() and their *OnLine().
#include <Wire.h>
#include <SPI.h>
#include <TinyScreen.h>
#include <STBLE.h>

TinyScreen display = TinyScreen(TinyScreenPlus);

const int MAX_LINE = 128;

// Each link gets its own buffer so USB and Bluetooth bytes never mix in one line.
struct LineReader {
  char buf[MAX_LINE];
  int len;
  bool tooLong;
};
LineReader usbIn, bleIn;

// Sends one message on every connected link; adds the newline.
void sendLine(const char *text) {
  if (SerialUSB) {  // no laptop listening on USB: don't block
    SerialUSB.print(text);
    SerialUSB.print('\n');
  }
  bleSend(text);
}

void handleLine(const char *line) {
  if (strncmp(line, "ECHO:", 5) == 0) { sendLine(line); return; }  // link test
  if (strcmp(line, "DROP") == 0) { bleDrop(); return; }  // reconnect test
  if (securityOnLine(line)) return;
  if (remoteOnLine(line)) return;
  // unknown messages are ignored
}

// Collects bytes into whole lines; an overlong line is dropped up to its newline.
void feedByte(LineReader &in, char c) {
  if (c == '\r') return;
  if (c == '\n') {
    in.buf[in.len] = 0;
    if (!in.tooLong && in.len > 0) handleLine(in.buf);
    in.len = 0;
    in.tooLong = false;
  } else if (in.len < MAX_LINE - 1) {
    in.buf[in.len++] = c;
  } else {
    in.tooLong = true;
  }
}

void resetReader(LineReader &in) {
  in.len = 0;
  in.tooLong = false;
}

void setup() {
  SerialUSB.begin(115200);
  Wire.begin();
  display.begin();
  display.setFlip(true);
  display.setBrightness(10);
  
  // --- DISPLAY INITIALIZATION ---
  display.setFont(thinPixel7_10ptFontInfo);          // Uses authentic TinyScreen library font configuration
  display.fontColor(TS_8b_White, TS_8b_Black);       // White text on a Black background
  uiBoot();                                          // boot screen (ui.ino draws every screen)
  // ------------------------------

  bleSetup();
  uiBootStep(1, 3);
  securitySetup();
  uiBootStep(2, 3);
  remoteSetup();
  uiBootStep(3, 3);
  uiBootDone();
}

void loop() {
  while (SerialUSB.available()) feedByte(usbIn, SerialUSB.read());
  bleLoop();
  securityLoop();
  remoteLoop();
  uiLoop();
}
