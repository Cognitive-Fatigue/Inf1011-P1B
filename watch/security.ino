// Pair B: double-tap approval. Handle CHAL lines here, answer with sendLine("RESP:...").
//
// Week 1 (this file now):
//   - CHAL:<32 hex>:VAULT is answered at once with RESP:<64 hex> (HMAC-SHA256), so the laptop
//     can check both sides compute the same value. Test: python laptop/security_test.py --usb
//   - Double-tap detection on the accelerometer. With the USB cable in, each detected double-tap
//     prints DBG lines (open the Serial Monitor at 115200).
// Week 2: answer CHAL only after a double-tap ("Approve?" on screen), or RESP:REJECT after 10 s.
//
// No extra library needed: SHA-256 and HMAC are written out below, and the accelerometer
// (BMA250 on the Accelerometer TinyShield) is read directly over I2C with Wire.

// ---------- Shared key ----------
// Must be byte-for-byte the same as KEY in laptop/security.py.
// Hardcoded on both sides: a known limitation, stated in the video.
const uint8_t SEC_KEY[32] = {
  0x3d, 0x5e, 0x34, 0xe6, 0x6d, 0x2a, 0xd3, 0xc7, 0x42, 0x1b, 0x78, 0x21, 0xac, 0x42, 0x01, 0x51,
  0xd0, 0x7a, 0x67, 0xbc, 0x84, 0x8e, 0x99, 0x30, 0xac, 0x3d, 0x95, 0x9c, 0xd7, 0xdb, 0xc6, 0x2a
};

// ---------- Double-tap tuning ----------
// Units: accelerometer counts at the +-4 g range, 128 counts = 1 g.
// "Jerk" is how much the reading changed since the last sample (5 ms ago), summed over x, y, z.
// A tap is a sharp jolt (big jerk); swinging your arm changes the reading slowly (small jerk).
const bool SEC_DEBUG = true;               // print DBG lines over USB while tuning; false when done
const int SEC_TAP_JERK = 60;               // jerk at or above this is a tap (about 0.5 g in 5 ms); 80 missed taps through the case
const int SEC_QUIET_JERK = 20;             // jerk below this counts as "watch is still"
const unsigned long SEC_QUIET_BEFORE_MS = 150;  // watch must be still this long before the first tap
const unsigned long SEC_DEAD_MS = 60;      // ignore the ringing for this long after a tap
const unsigned long SEC_MIN_GAP_MS = 80;   // second tap must come at least this long after the first
const unsigned long SEC_MAX_GAP_MS = 450;  // ...and at most this long
const unsigned long SEC_SAMPLE_MS = 5;     // read the accelerometer every 5 ms (200 times a second)

// ---------- SHA-256 (FIPS 180-4) ----------
const uint32_t SEC_K[64] = {
  0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
  0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
  0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
  0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
  0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
  0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
  0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
  0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2
};

// One hash at a time, kept in globals: no struct in any function signature, because the Arduino IDE
// writes prototypes for every function above the types defined in this tab.
uint32_t secShaH[8];
uint8_t secShaBuf[64];
uint8_t secShaBufLen;
uint32_t secShaTotal;  // bytes hashed so far (our messages are tiny)

uint32_t secRotr(uint32_t x, int n) {
  return (x >> n) | (x << (32 - n));
}

void secShaBlock() {
  uint32_t w[64];
  for (int i = 0; i < 16; i++) {  // SHA-256 reads the block big-endian (the ARM chip is little-endian)
    w[i] = ((uint32_t)secShaBuf[4 * i] << 24) | ((uint32_t)secShaBuf[4 * i + 1] << 16) |
           ((uint32_t)secShaBuf[4 * i + 2] << 8) | secShaBuf[4 * i + 3];
  }
  for (int i = 16; i < 64; i++) {
    uint32_t s0 = secRotr(w[i - 15], 7) ^ secRotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
    uint32_t s1 = secRotr(w[i - 2], 17) ^ secRotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
    w[i] = w[i - 16] + s0 + w[i - 7] + s1;
  }
  uint32_t a = secShaH[0], b = secShaH[1], c = secShaH[2], d = secShaH[3];
  uint32_t e = secShaH[4], f = secShaH[5], g = secShaH[6], h = secShaH[7];
  for (int i = 0; i < 64; i++) {
    uint32_t t1 = h + (secRotr(e, 6) ^ secRotr(e, 11) ^ secRotr(e, 25)) + ((e & f) ^ (~e & g)) + SEC_K[i] + w[i];
    uint32_t t2 = (secRotr(a, 2) ^ secRotr(a, 13) ^ secRotr(a, 22)) + ((a & b) ^ (a & c) ^ (b & c));
    h = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
  }
  secShaH[0] += a; secShaH[1] += b; secShaH[2] += c; secShaH[3] += d;
  secShaH[4] += e; secShaH[5] += f; secShaH[6] += g; secShaH[7] += h;
}

void secShaInit() {
  const uint32_t start[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
  for (int i = 0; i < 8; i++) secShaH[i] = start[i];
  secShaBufLen = 0;
  secShaTotal = 0;
}

void secShaUpdate(const uint8_t *data, size_t len) {
  for (size_t i = 0; i < len; i++) {
    secShaBuf[secShaBufLen++] = data[i];
    if (secShaBufLen == 64) { secShaBlock(); secShaBufLen = 0; }
  }
  secShaTotal += len;
}

void secShaFinal(uint8_t *out) {
  uint32_t bits = secShaTotal * 8;
  uint8_t pad = 0x80;
  secShaUpdate(&pad, 1);
  pad = 0;
  while (secShaBufLen != 56) secShaUpdate(&pad, 1);
  uint8_t lenBytes[8] = {0, 0, 0, 0, (uint8_t)(bits >> 24), (uint8_t)(bits >> 16), (uint8_t)(bits >> 8), (uint8_t)bits};
  secShaUpdate(lenBytes, 8);  // fills the block exactly, so it is processed
  for (int i = 0; i < 8; i++) {
    out[4 * i] = secShaH[i] >> 24; out[4 * i + 1] = secShaH[i] >> 16;
    out[4 * i + 2] = secShaH[i] >> 8; out[4 * i + 3] = secShaH[i];
  }
}

// HMAC-SHA256 (RFC 2104) with our 32-byte key: H((K ^ opad) || H((K ^ ipad) || msg))
void secHmac(const uint8_t *msg, size_t len, uint8_t *out) {
  uint8_t pad[64], inner[32];
  for (int i = 0; i < 64; i++) pad[i] = (i < 32 ? SEC_KEY[i] : 0) ^ 0x36;
  secShaInit(); secShaUpdate(pad, 64); secShaUpdate(msg, len); secShaFinal(inner);
  for (int i = 0; i < 64; i++) pad[i] = (i < 32 ? SEC_KEY[i] : 0) ^ 0x5c;
  secShaInit(); secShaUpdate(pad, 64); secShaUpdate(inner, 32); secShaFinal(out);
}

// ---------- CHAL -> RESP ----------
int secHexValue(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  return -1;  // hex is lowercase only (message rules)
}

const int SEC_MAX_ACTION = 16;

// Parses "CHAL:<32 hex>:<ACTION>" into the HMAC input: 16 raw challenge bytes, then the action's
// ASCII bytes. Returns the input length, or 0 if the line is malformed.
size_t secParseChallenge(const char *line, uint8_t *msg) {  // msg holds 16 + SEC_MAX_ACTION bytes
  if (strncmp(line, "CHAL:", 5) != 0) return 0;
  const char *hex = line + 5;
  for (int i = 0; i < 16; i++) {
    int hi = secHexValue(hex[2 * i]), lo = secHexValue(hex[2 * i + 1]);  // stops at a short line's 0
    if (hi < 0 || lo < 0) return 0;
    msg[i] = (hi << 4) | lo;  // raw bytes, NOT the hex text
  }
  if (hex[32] != ':') return 0;
  const char *action = hex + 33;
  size_t n = strlen(action);
  if (n == 0 || n > SEC_MAX_ACTION) return 0;
  for (size_t i = 0; i < n; i++) {
    if (action[i] < 'A' || action[i] > 'Z') return 0;
    msg[16 + i] = action[i];
  }
  return 16 + n;
}

// Builds "RESP:<64 lowercase hex>" for a CHAL line; returns false if the line is malformed.
bool secMakeResponse(const char *line, char *resp) {  // resp holds 5 + 64 + 1 chars
  uint8_t msg[16 + SEC_MAX_ACTION], mac[32];
  size_t len = secParseChallenge(line, msg);
  if (len == 0) return false;
  secHmac(msg, len, mac);
  const char digits[] = "0123456789abcdef";
  memcpy(resp, "RESP:", 5);
  for (int i = 0; i < 32; i++) {
    resp[5 + 2 * i] = digits[mac[i] >> 4];
    resp[5 + 2 * i + 1] = digits[mac[i] & 0x0f];
  }
  resp[5 + 64] = 0;
  return true;
}

// ---------- Accelerometer (BMA250) ----------
uint8_t secAccelAddr = 0;  // 0: no accelerometer found

// Debug lines go over USB only, never Bluetooth. The laptop program ignores them (unknown type).
void secDebugText(const char *text) {
  if (!SEC_DEBUG || !SerialUSB) return;
  SerialUSB.print("DBG:");
  SerialUSB.print(text);
  SerialUSB.print('\n');
}

void secDebug(const char *text, long value) {
  if (!SEC_DEBUG || !SerialUSB) return;
  SerialUSB.print("DBG:");
  SerialUSB.print(text);
  SerialUSB.print(value);
  SerialUSB.print('\n');
}

bool secAccelWrite(uint8_t reg, uint8_t value) {
  Wire.beginTransmission(secAccelAddr);
  Wire.write(reg);
  Wire.write(value);
  return Wire.endTransmission() == 0;
}

// Reads x, y, z (registers 0x02-0x07). Each axis is 10 bits: low 2 bits in the first byte's
// top bits, high 8 bits in the second byte, little-endian order. Returns false if the read failed.
bool secAccelRead(int *a) {
  Wire.beginTransmission(secAccelAddr);
  Wire.write(0x02);
  if (Wire.endTransmission() != 0) return false;
  if (Wire.requestFrom(secAccelAddr, (uint8_t)6) != 6) return false;
  for (int i = 0; i < 3; i++) {
    uint8_t lsb = Wire.read(), msb = Wire.read();
    a[i] = (int16_t)((msb << 8) | lsb) >> 6;  // sign-extends the 10-bit value
  }
  return true;
}

void secAccelSetup() {
  const uint8_t addrs[2] = {0x18, 0x19};  // BMA250 address, depending on the board's SDO pin
  for (int i = 0; i < 2 && !secAccelAddr; i++) {
    Wire.beginTransmission(addrs[i]);
    if (Wire.endTransmission() == 0) secAccelAddr = addrs[i];
  }
  if (!secAccelAddr) {
    secDebugText("no accelerometer found: is the Accelerometer TinyShield on the stack?");
    return;
  }
  secAccelWrite(0x14, 0xB6);  // soft reset
  delay(5);
  secAccelWrite(0x0F, 0x05);  // range +-4 g (128 counts per g): a firm tap doesn't max out
  secAccelWrite(0x10, 0x0D);  // filter bandwidth 250 Hz: keeps the sharp edge of a tap
  secDebugText("accelerometer found");
}

// ---------- Double-tap detection ----------
int secPrev[3];
bool secHavePrev = false;
unsigned long secLastSampleMs = 0;
unsigned long secLastBusyMs = 0;     // last time the watch was not still
unsigned long secDeadUntilMs = 0;    // ignoring ringing until then
unsigned long secFirstTapMs = 0;     // 0: waiting for a first tap
unsigned long secDoubleTaps = 0;

// ---------- Week 2 Approval State Variables ----------
char pendingChal[128];
bool waitingForApproval = false;
unsigned long chalStartTime = 0;
const unsigned long APPROVAL_TIMEOUT_MS = 10000; // 10 seconds

// Status check required by Pair C for remote.ino
bool securityIsApprovalActive() {
  return waitingForApproval;
}

// Week 2: Triggered upon double-tap
void secOnDoubleTap() {
  secDoubleTaps++;
  secDebug("DOUBLE TAP ", secDoubleTaps);

  if (waitingForApproval) {
    char resp[5 + 64 + 1];
    bool ok = secMakeResponse(pendingChal, resp);
    if (ok) {
      sendLine(resp);  // Send HMAC signature upon approval
    } else {
      sendLine("RESP:REJECT");
    }
    waitingForApproval = false;
    uiShowResult(ok);  // OPEN or DENIED, then back to the previous screen
  }
}

bool secTapSample(const int *a, unsigned long now) {
  if (!secHavePrev) {
    memcpy(secPrev, a, sizeof(secPrev));
    secHavePrev = true;
    return false;
  }
  int jerk = abs(a[0] - secPrev[0]) + abs(a[1] - secPrev[1]) + abs(a[2] - secPrev[2]);
  memcpy(secPrev, a, sizeof(secPrev));

  bool wasStill = now - secLastBusyMs >= SEC_QUIET_BEFORE_MS;
  if (jerk >= SEC_QUIET_JERK) secLastBusyMs = now;

  if (secFirstTapMs && now - secFirstTapMs > SEC_MAX_GAP_MS) {
    secFirstTapMs = 0;
  }
  if (now < secDeadUntilMs || jerk < SEC_TAP_JERK) return false;
  secDeadUntilMs = now + SEC_DEAD_MS;

  if (!secFirstTapMs) {
    if (!wasStill) return false;
    secFirstTapMs = now;
    return false;
  }
  unsigned long gap = now - secFirstTapMs;
  secFirstTapMs = 0;
  if (gap < SEC_MIN_GAP_MS) return false;
  return true;
}

void securitySetup() {
  secAccelSetup();
}

// Week 2 Loop: Handle 10-second timeout
void securityLoop() {
  unsigned long now = millis();

  // Check for timeout (before the accelerometer check, so a watch without one still rejects)
  if (waitingForApproval && (now - chalStartTime > APPROVAL_TIMEOUT_MS)) {
    waitingForApproval = false;
    sendLine("RESP:REJECT");  // Timeout rejection
    uiShowResult(false);
  }

  if (!secAccelAddr) return;

  if (now - secLastSampleMs < SEC_SAMPLE_MS) return;
  if (now - secLastSampleMs > 4 * SEC_SAMPLE_MS) secHavePrev = false;
  secLastSampleMs = now;
  int a[3];
  if (!secAccelRead(a)) { secHavePrev = false; return; }
  if (secTapSample(a, now)) secOnDoubleTap();
}

// Week 2 OnLine: Prompt user with "Approve?" instead of answering instantly
bool securityOnLine(const char *line) {
  if (strncmp(line, "CHAL:", 5) != 0) return false;
  
  strncpy(pendingChal, line, sizeof(pendingChal) - 1);
  pendingChal[sizeof(pendingChal) - 1] = '\0';
  waitingForApproval = true;
  chalStartTime = millis();

  uiShowApproval();  // "Approve?" with a 10 s countdown bar

  return true;
}