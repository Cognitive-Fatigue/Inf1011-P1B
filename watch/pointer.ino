// pointer.ino - Pair C TEST: air mouse / laser pointer.
// While the lower-left button is held, sends the watch's tilt as "TILT:x,y" 20 times a second.
// x, y: -128..128 (0,0 = watch level, 128 = tilted 90 degrees).
// Reads the accelerometer (BMA250 on the Accelerometer TinyShield) the same way as Pair B's code.
// NOTE: lower-left is Pair A's MODE button; it is only borrowed here for testing.

const unsigned long POINTER_SEND_MS = 50;  // send 20 times a second
uint8_t pointerAccelAddr = 0;              // 0 = no accelerometer found
unsigned long pointerSentAt = 0;
bool pointerWasOn = false;
bool pointerCirclePrev = false;       // was the circle button down last time?
unsigned long pointerCircleAt = 0;    // when we last sent CIRCLE (to ignore button bounce)

// True while the pointer button (lower left) is held. remote.ino uses this to keep other buttons quiet.
bool pointerHeld() {
  return pointerAccelAddr && display.getButtons(TSButtonLowerLeft);
}

bool pointerAccelWrite(uint8_t reg, uint8_t value) {
  Wire.beginTransmission(pointerAccelAddr);
  Wire.write(reg);
  Wire.write(value);
  return Wire.endTransmission() == 0;
}

// Reads x, y, z. Each axis is 10 bits; this turns the raw bytes into a normal number.
bool pointerAccelRead(int *a) {
  Wire.beginTransmission(pointerAccelAddr);
  Wire.write(0x02);
  if (Wire.endTransmission() != 0) return false;
  if (Wire.requestFrom(pointerAccelAddr, (uint8_t)6) != 6) return false;
  for (int i = 0; i < 3; i++) {
    uint8_t lsb = Wire.read(), msb = Wire.read();
    a[i] = (int16_t)((msb << 8) | lsb) >> 6;
  }
  return true;
}

void pointerSetup() {
  const uint8_t addrs[2] = {0x18, 0x19};  // the accelerometer answers on one of these addresses
  for (int i = 0; i < 2 && !pointerAccelAddr; i++) {
    Wire.beginTransmission(addrs[i]);
    if (Wire.endTransmission() == 0) pointerAccelAddr = addrs[i];
  }
  if (!pointerAccelAddr) {
    uiNoAccelerometer();  // no accelerometer board on the stack: the boot screen says so
    return;
  }
  pointerAccelWrite(0x0F, 0x05);  // range +-4 g: 128 counts = 1 g (same setting as Pair B)
  pointerAccelWrite(0x10, 0x0D);  // filter setting (same as Pair B, so we don't fight over it)
}

void pointerLoop() {
  if (!pointerAccelAddr) return;
  bool on = display.getButtons(TSButtonLowerLeft);  // laser "trigger"
  if (!on) {
    if (pointerWasOn) sendLine("TILT:0,0");  // just released: tell the laptop to stop at once
    pointerWasOn = false;
    pointerCirclePrev = false;
    return;
  }
  pointerWasOn = true;
   // While pointing: tapping the upper-left button draws a circle around the dot.   ← circle code goes HERE
  bool circleBtn = display.getButtons(TSButtonUpperLeft);
  if (circleBtn && !pointerCirclePrev && millis() - pointerCircleAt > 1000) {
    sendLine("CIRCLE");
    pointerCircleAt = millis();
  }
  pointerCirclePrev = circleBtn;
  if (millis() - pointerSentAt < POINTER_SEND_MS) return;  // not time to send yet
  pointerSentAt = millis();

  int a[3];
  if (!pointerAccelRead(a)) return;
  char msg[24];
  sprintf(msg, "TILT:%d,%d", a[0], a[1]);  // e.g. "TILT:40,-12"
  sendLine(msg);
}