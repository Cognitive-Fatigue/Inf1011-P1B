// ui.ino - every watch screen is drawn here (team file). The other tabs only report state through
// the ui...() calls below, so no two tabs draw over each other.
// Screens: boot, media, pointer, key mode, approval prompt, approval result.

// 8-bit colours are BBBGGGRR (3 bits blue, 3 green, 2 red).
const uint8_t UI_WHITE = TS_8b_White;
const uint8_t UI_GREY = TS_8b_Gray;
const uint8_t UI_DIM = 0x49;  // dark grey: long-press icons, empty signal bars
const uint8_t UI_BLACK = TS_8b_Black;
const uint8_t UI_TEAL = 0xB8;
const uint8_t UI_TEAL_DARK = 0x28;
const uint8_t UI_GREEN = TS_8b_Green;
const uint8_t UI_AMBER = 0x17;
const uint8_t UI_AMBER_DARK = 0x05;
const uint8_t UI_RED = TS_8b_Red;

const uint8_t UI_BOOT = 0, UI_MEDIA = 1, UI_POINTER = 2, UI_KEY = 3, UI_APPROVE = 4, UI_RESULT = 5;
const unsigned long UI_FLASH_MS = 300;    // a pressed corner stays lit this long
const unsigned long UI_RESULT_MS = 1500;  // OPEN / DENIED stays on screen this long

uint8_t uiScreen = UI_BOOT;
bool uiKeyMode = false;   // Pair A's mode switch sets this with uiSetKeyMode()
bool uiLinked = false;    // laptop connected over Bluetooth or USB
bool uiNoAccel = false;
int uiBattery = -1;       // %, -1 = not measured yet
uint8_t uiBars = 0;       // signal strength, 0-4
long uiTimerSecs = 0;
bool uiTimerOn = false;
long uiClockMin = -1;     // minutes since midnight from the laptop, -1 = not known yet
int8_t uiFlashCorner = -1;
unsigned long uiFlashAt = 0;
unsigned long uiApproveAt = 0;
uint8_t uiApproveW = 0;   // countdown bar width currently drawn
unsigned long uiResultAt = 0;

// Icons: 'X' = lit pixel. Drawn as horizontal runs, so each row costs a few rectangle commands.
const char *const UI_ICON_PLAY[] = {"X....X.X", "XX...X.X", "XXX..X.X", "XXXX.X.X", "XXX..X.X", "XX...X.X", "X....X.X"};
const char *const UI_ICON_SKIP[] = {"X....X", "XX...X", "XXX..X", "XXXX.X", "XXX..X", "XX...X", "X....X"};
const char *const UI_ICON_NEXT[] = {"XX....", ".XX...", "..XX..", "...XX.", "..XX..", ".XX...", "XX...."};
const char *const UI_ICON_BACK[] = {"....XX", "...XX.", "..XX..", ".XX...", "..XX..", "...XX.", "....XX"};
const char *const UI_ICON_VOLUP[] = {"...X.......", "..XX....X..", "XXXX....X..", "XXXX..XXXXX", "XXXX....X..", "..XX....X..", "...X......."};
const char *const UI_ICON_VOLDN[] = {"...X.......", "..XX.......", "XXXX.......", "XXXX..XXXXX", "XXXX.......", "..XX.......", "...X......."};
const char *const UI_ICON_MODE[] = {"....X..", "XXXXXX.", "....X..", ".......", "..X....", ".XXXXXX", "..X...."};
const char *const UI_ICON_CIRCLE[] = {"..XXX..", ".X...X.", "X.....X", "X..X..X", "X.....X", ".X...X.", "..XXX.."};
const char *const UI_ICON_LOCK[] = {"..XXX..", ".X...X.", ".X...X.", "XXXXXXX", "XXX.XXX", "XXX.XXX", "XXXXXXX"};
const char *const UI_ICON_DOT[] = {".XXX.", "XXXXX", "XXXXX", "XXXXX", ".XXX."};
const char *const UI_ICON_TICK[] = {"......X", ".....XX", "X...XX.", "XX.XX..", ".XXX...", "..X...."};
const char *const UI_ICON_CROSS[] = {"X....X", "XX..XX", ".XXXX.", "..XX..", ".XXXX.", "XX..XX", "X....X"};
#define UI_ICON(name) name, sizeof(name) / sizeof(name[0])

// Draws an icon with its top-left at (x, y), or its top-right at (x, y) when right is true.
void uiIcon(const char *const *rows, uint8_t n, int x, uint8_t y, uint8_t colour, uint8_t scale, bool right) {
  if (right) x -= strlen(rows[0]) * scale;
  for (uint8_t j = 0; j < n; j++) {
    const char *r = rows[j];
    uint8_t i = 0;
    while (r[i]) {
      if (r[i] != 'X') { i++; continue; }
      uint8_t start = i;
      while (r[i] == 'X') i++;
      display.drawRect(x + start * scale, y + j * scale, (i - start) * scale, scale, TSRectangleFilled, colour);
    }
  }
}

// align: 0 = left edge at x, 1 = centred on the screen, 2 = right edge at x.
void uiText(const char *s, int x, uint8_t y, uint8_t fg, uint8_t bg, uint8_t align) {
  display.fontColor(fg, bg);
  int w = display.getPrintWidth((char *)s);
  if (align == 1) x = (96 - w) / 2;
  else if (align == 2) x -= w;
  display.setCursor(x, y);
  display.print(s);
  display.fontColor(UI_WHITE, UI_BLACK);
}

void uiBigText(const char *s, uint8_t y, uint8_t colour, bool large) {
  display.setFont(large ? liberationSans_14ptFontInfo : liberationSans_12ptFontInfo);
  uiText(s, 0, y, colour, UI_BLACK, 1);
  display.setFont(thinPixel7_10ptFontInfo);
}

void uiBox(uint8_t x, uint8_t y, uint8_t w, uint8_t h, uint8_t colour) {
  display.drawRect(x, y, w, h, TSRectangleFilled, colour);
}

void uiFrame(uint8_t colour) {
  display.drawRect(2, 2, 92, 60, TSRectangleNoFill, colour);
}

// ---------- Parts of the media, pointer and key screens ----------

// Signal bars, battery icon and battery % in the top middle, between the two top corners.
void uiDrawStatus() {
  display.clearWindow(22, 0, 50, 10);
  for (uint8_t i = 0; i < 4; i++) {
    uint8_t h = 2 + (3 * i) / 2;
    uiBox(22 + i * 3, 9 - h, 2, h, i < uiBars ? UI_GREEN : UI_DIM);
  }
  if (uiBattery < 0) return;
  bool low = uiBattery <= 20;
  uint8_t level = uiBattery > 50 ? UI_GREEN : low ? UI_RED : UI_AMBER;
  display.drawRect(36, 2, 12, 7, TSRectangleNoFill, low ? UI_RED : UI_WHITE);
  uiBox(48, 4, 1, 3, low ? UI_RED : UI_WHITE);
  uint8_t w = (uint8_t)(8 * uiBattery / 100);
  uiBox(38, 4, w ? w : 1, 3, level);
  char text[4];
  sprintf(text, "%d", uiBattery);
  uiText(text, 51, 1, low ? UI_RED : UI_GREY, UI_BLACK, 0);
}

// corner: 0 = upper left, 1 = upper right, 2 = lower right. lit = just pressed.
// Short press on top, long press under it, as in the original text labels.
void uiDrawCorner(uint8_t corner, bool lit) {
  uint8_t on = lit ? UI_TEAL : uiLinked ? UI_WHITE : UI_GREY;
  uint8_t hold = lit ? UI_GREY : UI_DIM;
  uint8_t bg = lit ? UI_TEAL_DARK : UI_BLACK;
  if (corner == 0) {
    uiBox(0, 0, 12, 21, bg);
    uiIcon(UI_ICON(UI_ICON_PLAY), 2, 2, on, 1, false);
    uiIcon(UI_ICON(UI_ICON_SKIP), 2, 12, hold, 1, false);
  } else if (corner == 1) {
    uiBox(81, 0, 15, 21, bg);
    uiIcon(UI_ICON(UI_ICON_VOLUP), 94, 2, on, 1, true);
    uiIcon(UI_ICON(UI_ICON_NEXT), 94, 12, hold, 1, true);
  } else {
    uiBox(81, 43, 15, 21, bg);
    uiIcon(UI_ICON(UI_ICON_VOLDN), 94, 45, on, 1, true);
    uiIcon(UI_ICON(UI_ICON_BACK), 94, 55, hold, 1, true);
  }
}

void uiDrawModeButton(bool lit) {
  uiBox(0, 53, 11, 11, lit ? UI_TEAL_DARK : UI_BLACK);
  uiIcon(UI_ICON(UI_ICON_MODE), 2, 55, lit ? UI_TEAL : UI_WHITE, 1, false);
}

// The middle shows the presentation timer in teal while it runs, otherwise the clock in white.
void uiDrawTimer() {
  char text[10];
  uint8_t colour;
  if (uiTimerOn) {
    long h = uiTimerSecs / 3600, m = (uiTimerSecs / 60) % 60, s = uiTimerSecs % 60;
    if (h) sprintf(text, "%ld:%02ld:%02ld", h, m, s);
    else sprintf(text, "%02ld:%02ld", m, s);
    colour = UI_TEAL;
  } else if (uiClockMin >= 0) {
    sprintf(text, "%02ld:%02ld", uiClockMin / 60, uiClockMin % 60);
    colour = UI_WHITE;
  } else {
    strcpy(text, "--:--");
    colour = UI_GREY;
  }
  display.clearWindow(16, 19, 64, 20);
  uiBigText(text, 20, colour, true);
}

void uiDrawMediaTag() {
  display.clearWindow(20, 40, 56, 11);
  if (uiLinked) {
    uiText("MEDIA", 0, 41, UI_TEAL, UI_BLACK, 1);
  } else {
    uiBox(22, 40, 52, 10, UI_AMBER_DARK);
    uiText("NO LINK", 0, 41, UI_AMBER, UI_AMBER_DARK, 1);
  }
}

// ---------- Whole screens ----------

void uiDrawScreen() {
  display.clearWindow(0, 0, 96, 64);
  if (uiScreen == UI_MEDIA) {
    for (uint8_t c = 0; c < 3; c++) uiDrawCorner(c, false);
    uiDrawModeButton(false);
    uiDrawStatus();
    uiDrawTimer();
    uiDrawMediaTag();
  } else if (uiScreen == UI_POINTER) {
    uiIcon(UI_ICON(UI_ICON_CIRCLE), 2, 2, UI_WHITE, 1, false);  // upper left draws a circle while pointing
    uiDrawModeButton(true);
    uiDrawStatus();
    uiIcon(UI_ICON(UI_ICON_DOT), 43, 20, UI_RED, 2, false);
    uiText("POINTER", 0, 36, UI_RED, UI_BLACK, 1);
    uiText("tilt to move", 0, 46, UI_GREY, UI_BLACK, 1);
  } else if (uiScreen == UI_KEY) {
    uiDrawModeButton(false);
    uiDrawStatus();
    uiIcon(UI_ICON(UI_ICON_LOCK), 41, 16, UI_AMBER, 2, false);
    uiBox(36, 40, 24, 10, UI_AMBER_DARK);
    uiText("KEY", 0, 41, UI_AMBER, UI_AMBER_DARK, 1);
  } else if (uiScreen == UI_APPROVE) {
    uiFrame(UI_AMBER);
    uiBigText("Approve?", 10, UI_AMBER, false);
    uiText("double-tap", 0, 30, UI_WHITE, UI_BLACK, 1);
    uiText("to open vault", 0, 40, UI_GREY, UI_BLACK, 1);
    uiApproveW = 80;
    uiBox(8, 52, 80, 4, UI_AMBER);
  }
}

void uiShowHome() {
  uiScreen = uiKeyMode ? UI_KEY : UI_MEDIA;
  uiDrawScreen();
}

// ---------- Called from the other tabs ----------

void uiBoot() {
  display.clearWindow(0, 0, 96, 64);
  uiBigText("TinyKey", 16, UI_WHITE, true);
  uiBox(28, 44, 40, 3, UI_DIM);
}

// Fills the boot bar: step out of steps done.
void uiBootStep(uint8_t step, uint8_t steps) {
  uiBox(28, 44, 40 * step / steps, 3, UI_TEAL);
}

void uiBootDone() {
  if (uiNoAccel) uiText("NO ACCEL", 0, 52, UI_RED, UI_BLACK, 1);
  delay(uiNoAccel ? 1500 : 400);  // let the boot screen be seen
  uiShowHome();
}

void uiNoAccelerometer() {
  uiNoAccel = true;
}

void uiSetKeyMode(bool key) {
  uiKeyMode = key;
  if (uiScreen == UI_MEDIA || uiScreen == UI_KEY) uiShowHome();
}

void uiSetBattery(int pct) {
  uiBattery = pct;
  if (uiScreen == UI_MEDIA || uiScreen == UI_POINTER || uiScreen == UI_KEY) uiDrawStatus();
}

void uiSetSignal(int rssi) {
  uint8_t bars = rssi >= -60 ? 4 : rssi >= -70 ? 3 : rssi >= -80 ? 2 : rssi >= -90 ? 1 : 0;
  if (bars == uiBars) return;
  uiBars = bars;
  if (uiScreen == UI_MEDIA || uiScreen == UI_POINTER || uiScreen == UI_KEY) uiDrawStatus();
}

void uiSetClock(long minute) {
  uiClockMin = minute;
  if (uiScreen == UI_MEDIA && !uiTimerOn) uiDrawTimer();
}

void uiSetTimer(long secs, bool running) {
  uiTimerSecs = secs;
  uiTimerOn = running;
  if (uiScreen == UI_MEDIA) uiDrawTimer();
}

// Lights the corner of the button that sent cmd.
void uiPressed(const char *cmd) {
  int8_t corner = -1;
  if (!strcmp(cmd, "CMD:PLAY") || !strcmp(cmd, "CMD:SKIP")) corner = 0;
  else if (!strcmp(cmd, "CMD:VOLUP") || !strcmp(cmd, "CMD:NEXT")) corner = 1;
  else if (!strcmp(cmd, "CMD:VOLDN") || !strcmp(cmd, "CMD:PREV")) corner = 2;
  if (corner < 0 || uiScreen != UI_MEDIA) return;
  if (uiFlashCorner >= 0 && uiFlashCorner != corner) uiDrawCorner(uiFlashCorner, false);
  uiFlashCorner = corner;
  uiFlashAt = millis();
  uiDrawCorner(corner, true);
}

void uiShowApproval() {
  uiScreen = UI_APPROVE;
  uiFlashCorner = -1;
  uiApproveAt = millis();
  uiDrawScreen();
}

void uiShowResult(bool ok) {
  uiScreen = UI_RESULT;
  uiResultAt = millis();
  display.clearWindow(0, 0, 96, 64);
  uiFrame(ok ? UI_GREEN : UI_RED);
  if (ok) uiIcon(UI_ICON(UI_ICON_TICK), 37, 12, UI_GREEN, 3, false);
  else uiIcon(UI_ICON(UI_ICON_CROSS), 39, 10, UI_RED, 3, false);
  uiText(ok ? "OPEN" : "DENIED", 0, 44, ok ? UI_GREEN : UI_RED, UI_BLACK, 1);
}

void uiLoop() {
  unsigned long now = millis();
  bool linked = bleConnected || SerialUSB.dtr();  // dtr(): `if (SerialUSB)` waits 10 ms every call
  if (linked != uiLinked) {
    uiLinked = linked;
    if (!linked) uiBars = 0;
    if (uiScreen == UI_MEDIA) uiDrawScreen();
    else if (uiScreen == UI_POINTER || uiScreen == UI_KEY) uiDrawStatus();
  }
  if (uiScreen == UI_MEDIA && pointerHeld()) {
    uiScreen = UI_POINTER;
    uiFlashCorner = -1;
    uiDrawScreen();
  } else if (uiScreen == UI_POINTER && !pointerHeld()) {
    uiShowHome();
  }
  if (uiFlashCorner >= 0 && now - uiFlashAt >= UI_FLASH_MS) {
    if (uiScreen == UI_MEDIA) uiDrawCorner(uiFlashCorner, false);
    uiFlashCorner = -1;
  }
  if (uiScreen == UI_APPROVE) {
    unsigned long gone = now - uiApproveAt;
    uint8_t w = gone >= APPROVAL_TIMEOUT_MS ? 0 : 80 - (uint8_t)(80 * gone / APPROVAL_TIMEOUT_MS);
    if (w < uiApproveW) {
      uiBox(8 + w, 52, uiApproveW - w, 4, UI_AMBER_DARK);
      uiApproveW = w;
    }
  }
  if (uiScreen == UI_RESULT && now - uiResultAt >= UI_RESULT_MS) uiShowHome();
}
