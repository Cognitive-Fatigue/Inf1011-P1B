// TinyKey watch. Pair A owns this file: the link to the laptop (USB and Bluetooth, see ble.ino).
// Pairs B and C work in security.ino and remote.ino and only use sendLine() and their *OnLine().
#include <Wire.h>
#include <SPI.h>
#include <TinyScreen.h>
#include <STBLE.h>

TinyScreen display = TinyScreen(TinyScreenPlus);

const int MAX_LINE = 128;

enum WatchMode { MODE_KEY, MODE_MEDIA };
WatchMode appMode = MODE_KEY;
bool appLinkActive = false;
bool appLaptopAuthorized = false;
bool appLaptopViaBle = false;
bool appStatusDirty = true;
extern bool blePresent;
extern bool bleConnected;

// Each link gets its own buffer so USB and Bluetooth bytes never mix in one line.
struct LineReader {
  char buf[MAX_LINE];
  int len;
  bool tooLong;
};
LineReader usbIn, bleIn;

const unsigned long APP_USB_FRAME_TIMEOUT_MS = 250;

// USB configuration and DTR remain set after unplugging; recent frame changes prove bus activity.
bool appUsbActive() {
  static uint16_t lastFrame = 0;
  static unsigned long lastFrameAt = 0;
  static bool haveFrame = false;
  if (!USBDevice.configured()) {
    haveFrame = false;
    return false;
  }

  uint16_t frame = USB->DEVICE.FNUM.bit.FNUM;
  unsigned long now = millis();
  if (!haveFrame || frame != lastFrame) {
    lastFrame = frame;
    lastFrameAt = now;
    haveFrame = true;
  }
  return now - lastFrameAt <= APP_USB_FRAME_TIMEOUT_MS;
}

// Sends one message on every connected link; adds the newline.
void sendLine(const char *text) {
  if (appUsbActive()) {
    SerialUSB.print(text);
    SerialUSB.print('\n');
  }
  bleSend(text);
}

void handleLine(const char *line, bool fromBle) {
  if (strcmp(line, "ID?") == 0) {
    char identity[33];
    const uint32_t *uid = (const uint32_t *)0x0080A00C;
    snprintf(identity, sizeof(identity), "%08lx%08lx%08lx%08lx",
             (unsigned long)uid[0], (unsigned long)uid[13], (unsigned long)uid[14], (unsigned long)uid[15]);
    char response[36];
    snprintf(response, sizeof(response), "ID:%s", identity);
    if (fromBle) bleSend(response);
    else if (appUsbActive()) { SerialUSB.print(response); SerialUSB.print('\n'); }
    return;
  }
  if (strncmp(line, "ECHO:", 5) == 0) { sendLine(line); return; }  // link test
  if (strcmp(line, "DROP") == 0) { bleDrop(); return; }  // reconnect test
  if (strcmp(line, "SYNC") == 0) {
    if (!fromBle && !appUsbActive()) return;
    appLaptopAuthorized = true;
    appLaptopViaBle = fromBle;
    sendLine(appMode == MODE_MEDIA ? "MODE:MEDIA" : "MODE:KEY");
    return;
  }
  if (securityOnLine(line)) return;
  if (remoteOnLine(line)) return;
  // unknown messages are ignored
}

bool appModeIsMedia() { return appMode == MODE_MEDIA; }

bool appHasLaptopLink() {
  return appLaptopAuthorized && (appLaptopViaBle ? bleConnected : appUsbActive());
}

void appSetMode(WatchMode mode) {
  if (appMode == mode) return;
  appMode = mode;
  appStatusDirty = true;
  remoteCancelHeld();
  sendLine(mode == MODE_MEDIA ? "MODE:MEDIA" : "MODE:KEY");
}

const char *appLinkLabel() {
  bool usb = appUsbActive();
  if (bleConnected && usb) return "BLE+USB";
  if (bleConnected) return "BLE LINK";
  if (usb) return "USB LINK";
  if (blePresent) return "BLE WAIT";
  return "NO LINK";
}

void appDrawStatus() {
  if (securityIsApprovalActive()) return;
  if (!appStatusDirty) return;
  appStatusDirty = false;
  display.clearWindow(0, 0, 96, 64);
  if (appModeIsMedia()) remoteDrawLabels();
  const char *mode = appModeIsMedia() ? "MEDIA" : "KEY";
  const char *link = appLinkLabel();
  display.setCursor((96 - display.getPrintWidth((char *)mode)) / 2, 22);
  display.print((char *)mode);
  display.setCursor((96 - display.getPrintWidth((char *)link)) / 2, 32);
  display.print((char *)link);
}

void appLinkLoop() {
  bool usb = appUsbActive();
  bool active = bleConnected || usb;
  static int previousStatus = -1;
  int status = (blePresent ? 4 : 0) | (bleConnected ? 2 : 0) | (usb ? 1 : 0);
  if (status != previousStatus) {
    previousStatus = status;
    appStatusDirty = true;
  }
  if (active != appLinkActive) {
    appLinkActive = active;
    appStatusDirty = true;
  }
  if (appLaptopAuthorized && !(appLaptopViaBle ? bleConnected : usb)) {
    appLaptopAuthorized = false;
    appMode = MODE_KEY;
    securityCancelApproval();
    remoteCancelHeld();
    appStatusDirty = true;
  }
}

// Collects bytes into whole lines; an overlong line is dropped up to its newline.
void feedByte(LineReader &in, char c, bool fromBle) {
  if (c == '\r') return;
  if (c == '\n') {
    in.buf[in.len] = 0;
    if (!in.tooLong && in.len > 0) handleLine(in.buf, fromBle);
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
  
  display.setFont(thinPixel7_10ptFontInfo);
  display.fontColor(TS_8b_White, TS_8b_Black);

  bleSetup();
  securitySetup();
  remoteSetup();
}

void loop() {
  while (SerialUSB.available()) feedByte(usbIn, SerialUSB.read(), false);
  bleLoop();
  appLinkLoop();
  securityLoop();
  remoteLoop();
  appDrawStatus();
}
