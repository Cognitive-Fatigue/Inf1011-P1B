// TinyKey watch. Pair A owns this file: the link to the laptop (USB and Bluetooth, see ble.ino),
// the Key/Media mode switch on the lower-left button, and the shared mode check for remote.ino.
// Pairs B and C work in security.ino and remote.ino and only use sendLine() and their *OnLine().
// Every screen is drawn by ui.ino; this file only reports mode and link state to it.
#include <Wire.h>
#include <SPI.h>
#include <TinyScreen.h>
#include <STBLE.h>

TinyScreen display = TinyScreen(TinyScreenPlus);

const int MAX_LINE = 128;

enum WatchMode { MODE_KEY, MODE_MEDIA };
WatchMode appMode = MODE_KEY;  // boots in Key mode: buttons stay silent until Media is chosen
bool appLinkActive = false;
bool appLaptopAuthorized = false;  // the laptop sent SYNC on this link
bool appLaptopViaBle = false;
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
  if (appUsbActive()) {  // no laptop on USB: don't block
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

// ---------- Mode (shared checks for remote.ino and pointer.ino) ----------

bool appModeIsMedia() { return appMode == MODE_MEDIA; }

bool appHasLaptopLink() {
  return appLaptopAuthorized && (appLaptopViaBle ? bleConnected : appUsbActive());
}

void appSetMode(WatchMode mode) {
  if (appMode == mode) return;
  appMode = mode;
  remoteCancelHeld();
  uiSetKeyMode(mode == MODE_KEY);
  sendLine(mode == MODE_MEDIA ? "MODE:MEDIA" : "MODE:KEY");
}

// Lower-left button: tap = switch Key/Media. Hold in Media mode = laser pointer (pointer.ino).
// A press that turned into a pointer hold, or that overlapped an approval, never switches mode.
const unsigned long APP_MODE_DEBOUNCE_MS = 30;
const unsigned long APP_POINTER_HOLD_MS = 600;  // same "hold" time as the other buttons
bool appModeRaw = false, appModeDown = false;
bool appModeHoldUsed = false;  // this press became a pointer hold
bool appModeBlocked = false;   // this press overlapped an approval
unsigned long appModeChangedAt = 0, appModeDownAt = 0;

// The pointer may run only where the remote buttons could: Media mode, laptop linked, no approval.
bool appPointerAllowed() {
  return appModeIsMedia() && appHasLaptopLink() && !securityIsApprovalActive() && pointerAvailable();
}

// True while the lower-left button is being held as the pointer trigger.
bool appModeButtonHeld() {
  return appModeDown && appModeHoldUsed && !appModeBlocked && appPointerAllowed();
}

void appModeButtonLoop() {
  unsigned long now = millis();
  bool raw = display.getButtons(TSButtonLowerLeft);
  if (raw != appModeRaw) {
    appModeRaw = raw;
    appModeChangedAt = now;
  }
  if (raw != appModeDown && now - appModeChangedAt >= APP_MODE_DEBOUNCE_MS) {
    appModeDown = raw;
    if (appModeDown) {
      appModeDownAt = now;
      appModeHoldUsed = false;
      appModeBlocked = securityIsApprovalActive();
    } else if (!appModeHoldUsed && !appModeBlocked && !securityIsApprovalActive()) {
      appSetMode(appModeIsMedia() ? MODE_KEY : MODE_MEDIA);
    }
  }
  if (appModeDown && securityIsApprovalActive()) appModeBlocked = true;
  if (appModeDown && !appModeHoldUsed && !appModeBlocked && now - appModeDownAt >= APP_POINTER_HOLD_MS &&
      appPointerAllowed()) {
    appModeHoldUsed = true;
  }
}

// ---------- Link status (shown by ui.ino) ----------

const char *appLinkLabel() {
  bool usb = appUsbActive();
  if (bleConnected && usb) return "BLE+USB";
  if (bleConnected) return "BLE LINK";
  if (usb) return "USB LINK";
  if (blePresent) return "BLE WAIT";
  return "NO LINK";
}

void appLinkLoop() {
  bool usb = appUsbActive();
  appLinkActive = bleConnected || usb;
  if (appLaptopAuthorized && !(appLaptopViaBle ? bleConnected : usb)) {
    // the laptop that synced is gone: fall back to Key mode and drop anything in progress
    appLaptopAuthorized = false;
    appMode = MODE_KEY;
    securityCancelApproval();
    remoteCancelHeld();
    uiSetKeyMode(true);
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
  uiSetKeyMode(!appModeIsMedia());
  uiBootDone();
}

void loop() {
  while (SerialUSB.available()) feedByte(usbIn, SerialUSB.read(), false);
  bleLoop();
  appLinkLoop();
  appModeButtonLoop();
  securityLoop();
  remoteLoop();
  uiLoop();
}
