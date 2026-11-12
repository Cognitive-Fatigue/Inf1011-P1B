// battery.ino - shows the watch battery % at the top middle of the screen (Pair C extra feature).
// The voltage-reading code is based on TinyCircuits' TinyScreen+ example code.

const unsigned long BATTERY_CHECK_MS = 10000;  // re-measure every 10 s (a battery changes slowly)
unsigned long batteryCheckedAt = 0;
int batteryShown = -1;     // the % currently on screen; -1 = nothing drawn yet
bool batteryForce = true;  // true = measure and draw straight away

// Measures the chip's own supply voltage. Needed to turn the raw battery reading into volts.
float batteryGetVCC() {
  SYSCTRL->VREF.reg |= SYSCTRL_VREF_BGOUTEN;
  while (ADC->STATUS.bit.SYNCBUSY == 1);
  ADC->SAMPCTRL.bit.SAMPLEN = 0x1;
  while (ADC->STATUS.bit.SYNCBUSY == 1);
  ADC->INPUTCTRL.bit.MUXPOS = 0x19;  // measure the chip's fixed 1.1 V reference
  while (ADC->STATUS.bit.SYNCBUSY == 1);
  ADC->CTRLA.bit.ENABLE = 0x01;
  while (ADC->STATUS.bit.SYNCBUSY == 1);
  ADC->SWTRIG.bit.START = 1;
  ADC->INTFLAG.bit.RESRDY = 1;
  while (ADC->STATUS.bit.SYNCBUSY == 1);
  ADC->SWTRIG.bit.START = 1;  // measure again, the first result is thrown away
  while (ADC->INTFLAG.bit.RESRDY == 0);
  uint32_t valueRead = ADC->RESULT.reg;
  while (ADC->STATUS.bit.SYNCBUSY == 1);
  ADC->CTRLA.bit.ENABLE = 0x00;
  while (ADC->STATUS.bit.SYNCBUSY == 1);
  SYSCTRL->VREF.reg &= ~SYSCTRL_VREF_BGOUTEN;
  return (1.1 * 1023.0) / valueRead;
}

// Battery voltage in volts (full about 4.2 V, nearly empty about 3.5 V).
float batteryGetVoltage() {
  float vcc = batteryGetVCC();
  analogRead(A4);                         // the first reading is often off: throw it away
  float reading = analogRead(A4);         // pin A4 is wired to the battery
  return vcc * reading / 1023.0 / 0.5;    // the board halves the voltage before measuring
}

// Turns volts into 0-100 %.
int batteryPercent(float volts) {
  int pct = (int)((volts - 3.5) / (4.2 - 3.5) * 100);
  if (pct < 0) pct = 0;
  if (pct > 100) pct = 100;
  return pct;
}

// Call after the screen was wiped, so the % gets drawn again.
void batteryRedraw() {
  batteryShown = -1;
  batteryForce = true;
}

// Call every loop. Measures every 10 s and only redraws when the number changed.
void batteryLoop() {
  unsigned long now = millis();
  if (!batteryForce && now - batteryCheckedAt < BATTERY_CHECK_MS) return;  // not time yet
  batteryForce = false;
  batteryCheckedAt = now;

  int pct = batteryPercent(batteryGetVoltage());
  if (pct == batteryShown) return;  // same as before: no need to redraw
  batteryShown = pct;

  char text[6];
  sprintf(text, "%d%%", pct);           // e.g. "87%"  (%% prints a real % sign)
  display.clearWindow(34, 0, 28, 9);    // wipe the old number (x, y, width, height)
  display.setCursor((96 - display.getPrintWidth(text)) / 2, 0);  // centre it at the top
  display.print(text);
}