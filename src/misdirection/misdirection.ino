// Step 5: framed protocol on Serial1 -> USB HID.
//
// Wire format and message semantics are specified in PROTOCOL.md, which
// is the contract this firmware implements. Change one, change both.
//
// Tools -> USB Type: Keyboard + Mouse + Joystick (usb=hid). The plain
// mouse descriptor already carries an absolute collection, so moveTo()
// works without a Touch Screen variant.

// ---------------------------------------------------------------- config

const uint32_t BAUD             = 115200;
const uint8_t  PIN_ARM          = 2;      // low = armed
const uint8_t  PROTOCOL_VERSION = 1;

// Used until the host sends SCREEN_SIZE. The core's own default is
// 1366x768, which is a silent way to get every coordinate wrong.
const uint16_t DEFAULT_SCREEN_W = 1920;
const uint16_t DEFAULT_SCREEN_H = 1080;

// Release everything after this many ms of total UART silence while
// something is held. 0 disables. Off by default: with it on, the host
// MUST keepalive (a PING is enough) or a legitimate long modifier hold
// gets dropped out from under it. Turn it on once your host has a
// heartbeat and you want a dead controller to fail safe.
const uint32_t IDLE_RELEASE_MS = 0;

// Longest payload accepted. The protocol allows len up to 255; nothing
// defined needs more than 4, so a short buffer bounds the damage from a
// corrupt length byte.
const uint8_t MAX_PAYLOAD = 16;

// ------------------------------------------------------------- protocol

const uint8_t SOF = 0xAB;

enum : uint8_t {
  MSG_PANIC       = 0x00,
  MSG_KEY_DOWN    = 0x01,
  MSG_KEY_UP      = 0x02,
  MSG_MOUSE_MOVE  = 0x03,
  MSG_MOUSE_BTN   = 0x04,
  MSG_MOUSE_WHEEL = 0x05,
  MSG_SCREEN_SIZE = 0x06,
  MSG_PING        = 0x07,
  MSG_MOUSE_MOVE_REL = 0x08,
};

enum : uint8_t {
  RSP_PONG = 0x80,
  RSP_NACK = 0x81,
};

enum : uint8_t {
  NACK_CHECKSUM = 1,
  NACK_TYPE     = 2,
  NACK_LENGTH   = 3,
  NACK_DISARMED = 4,
  NACK_ROLLOVER = 5,
};

// ----------------------------------------------------------------- state

// The firmware owns the HID report. Raw usage codes come off the wire and
// go straight into keyboard_keys[] via set_key1..6, which sidesteps
// Teensy's mixed KEY_* / MODIFIERKEY_* constant encoding entirely.
static uint8_t keyMods = 0;
static uint8_t keySlots[6] = { 0, 0, 0, 0, 0, 0 };

static uint8_t buttonMask = 0;

// Mouse moves are the only high-rate message. Parse them all, remember
// the last one, emit a single report per loop pass -- replaying a burst
// that a bridge chip buffered just spends USB frames on stale positions.
static bool     movePending = false;
static uint16_t moveX = 0, moveY = 0;

static uint32_t lastRxMs = 0;

// ---------------------------------------------------------------- output

static void sendFrame(uint8_t type, const uint8_t *payload, uint8_t len) {
  uint8_t sum = (uint8_t)(type + len);
  Serial1.write(SOF);
  Serial1.write(type);
  Serial1.write(len);
  for (uint8_t i = 0; i < len; i++) {
    Serial1.write(payload[i]);
    sum = (uint8_t)(sum + payload[i]);
  }
  Serial1.write(sum);
}

static void sendNack(uint8_t reason) {
  sendFrame(RSP_NACK, &reason, 1);
}

static void sendPong() {
  const uint8_t v = PROTOCOL_VERSION;
  sendFrame(RSP_PONG, &v, 1);
}

// ------------------------------------------------------------- hid state

static void pushKeyReport() {
  Keyboard.set_modifier(keyMods);
  Keyboard.set_key1(keySlots[0]);
  Keyboard.set_key2(keySlots[1]);
  Keyboard.set_key3(keySlots[2]);
  Keyboard.set_key4(keySlots[3]);
  Keyboard.set_key5(keySlots[4]);
  Keyboard.set_key6(keySlots[5]);
  Keyboard.send_now();
}

static void pushButtons() {
  // Note the argument order: left, MIDDLE, right, back, forward. The
  // wire mask is bit0 left, bit1 right, bit2 middle.
  Mouse.set_buttons(buttonMask & 0x01,
                    (buttonMask >> 2) & 0x01,
                    (buttonMask >> 1) & 0x01,
                    (buttonMask >> 3) & 0x01,
                    (buttonMask >> 4) & 0x01);
}

static bool keyDown(uint8_t usage) {
  if (usage == 0) return true;

  if (usage >= 0xE0 && usage <= 0xE7) {
    keyMods |= (uint8_t)(1 << (usage - 0xE0));
    pushKeyReport();
    return true;
  }
  for (uint8_t i = 0; i < 6; i++) {
    if (keySlots[i] == usage) return true;   // already held; idempotent
  }
  for (uint8_t i = 0; i < 6; i++) {
    if (keySlots[i] == 0) {
      keySlots[i] = usage;
      pushKeyReport();
      return true;
    }
  }
  return false;   // 6KRO full
}

static void keyUp(uint8_t usage) {
  if (usage == 0) return;

  if (usage >= 0xE0 && usage <= 0xE7) {
    keyMods &= (uint8_t)~(1 << (usage - 0xE0));
    pushKeyReport();
    return;
  }
  for (uint8_t i = 0; i < 6; i++) {
    if (keySlots[i] == usage) {
      keySlots[i] = 0;
      pushKeyReport();
      return;
    }
  }
  // Releasing a key that is not held is a no-op, not an error: it is
  // exactly what a host does when recovering from a dropped frame.
}

// A relative report carries at most +/-127 per axis, so a larger delta
// goes out as several reports. Applied immediately, not coalesced: each
// frame is distance, and dropping one loses it.
static void moveRelative(int16_t dx, int16_t dy) {
  while (dx != 0 || dy != 0) {
    int8_t sx = (int8_t)(dx > 127 ? 127 : dx < -127 ? -127 : dx);
    int8_t sy = (int8_t)(dy > 127 ? 127 : dy < -127 ? -127 : dy);
    Mouse.move(sx, sy);
    dx = (int16_t)(dx - sx);
    dy = (int16_t)(dy - sy);
  }
}

static void panic() {
  keyMods = 0;
  for (uint8_t i = 0; i < 6; i++) keySlots[i] = 0;
  Keyboard.releaseAll();
  pushKeyReport();

  buttonMask = 0;
  pushButtons();

  movePending = false;
}

// ---------------------------------------------------------------- parser

static bool armed() { return digitalRead(PIN_ARM) == LOW; }

static void handleFrame(uint8_t type, const uint8_t *p, uint8_t len) {
  // Honored regardless of arm state. A stuck modifier has to be
  // clearable without anyone walking over to the board.
  if (type == MSG_PANIC) {
    if (len != 0) { sendNack(NACK_LENGTH); return; }
    panic();
    return;
  }
  if (type == MSG_PING) {
    if (len != 0) { sendNack(NACK_LENGTH); return; }
    sendPong();
    return;
  }
  // Config, not input: safe to accept while disarmed so the host can set
  // up before anyone grounds the pin.
  if (type == MSG_SCREEN_SIZE) {
    if (len != 4) { sendNack(NACK_LENGTH); return; }
    Mouse.screenSize((uint16_t)(p[0] | (p[1] << 8)),
                     (uint16_t)(p[2] | (p[3] << 8)));
    return;
  }

  if (!armed()) {
    // Throttled: a host that keeps streaming into a disarmed board would
    // otherwise get a NACK per frame and swamp the return channel.
    static uint32_t lastDisarmedNack = 0;
    uint32_t now = millis();
    if (now - lastDisarmedNack > 500) {
      lastDisarmedNack = now;
      sendNack(NACK_DISARMED);
    }
    return;
  }

  switch (type) {
    case MSG_KEY_DOWN:
      if (len != 1) { sendNack(NACK_LENGTH); return; }
      if (!keyDown(p[0])) sendNack(NACK_ROLLOVER);
      break;

    case MSG_KEY_UP:
      if (len != 1) { sendNack(NACK_LENGTH); return; }
      keyUp(p[0]);
      break;

    case MSG_MOUSE_MOVE:
      if (len != 4) { sendNack(NACK_LENGTH); return; }
      moveX = (uint16_t)(p[0] | (p[1] << 8));
      moveY = (uint16_t)(p[2] | (p[3] << 8));
      movePending = true;      // coalesced; emitted once per loop pass
      break;

    case MSG_MOUSE_MOVE_REL:
      if (len != 4) { sendNack(NACK_LENGTH); return; }
      moveRelative((int16_t)(p[0] | (p[1] << 8)),
                   (int16_t)(p[2] | (p[3] << 8)));
      break;

    case MSG_MOUSE_BTN:
      if (len != 1) { sendNack(NACK_LENGTH); return; }
      buttonMask = p[0];       // absolute state, so a dropped frame
      pushButtons();           // self-corrects on the next update
      break;

    case MSG_MOUSE_WHEEL:
      if (len != 2) { sendNack(NACK_LENGTH); return; }
      Mouse.scroll((int8_t)p[0], (int8_t)p[1]);
      break;

    default:
      sendNack(NACK_TYPE);
      break;
  }
}

static void feed(uint8_t b) {
  enum { RX_HUNT, RX_TYPE, RX_LEN, RX_PAYLOAD, RX_SUM };
  static uint8_t state = RX_HUNT;
  static uint8_t type, len, got, sum;
  static uint8_t payload[MAX_PAYLOAD];

  switch (state) {
    case RX_HUNT:
      if (b == SOF) state = RX_TYPE;
      break;

    case RX_TYPE:
      type  = b;
      sum   = b;
      state = RX_LEN;
      break;

    case RX_LEN:
      len = b;
      sum = (uint8_t)(sum + b);
      if (len > MAX_PAYLOAD) {
        // Almost certainly a corrupt length. Resyncing costs at most one
        // real frame; trusting it would cost up to 255 bytes of
        // swallowed stream.
        sendNack(NACK_LENGTH);
        state = RX_HUNT;
      } else {
        got   = 0;
        state = len ? RX_PAYLOAD : RX_SUM;
      }
      break;

    case RX_PAYLOAD:
      payload[got++] = b;
      sum = (uint8_t)(sum + b);
      if (got >= len) state = RX_SUM;
      break;

    case RX_SUM:
      if (b == sum) handleFrame(type, payload, len);
      else          sendNack(NACK_CHECKSUM);
      state = RX_HUNT;
      break;
  }
}

// ------------------------------------------------------------------ main

void setup() {
  pinMode(PIN_ARM, INPUT_PULLUP);
  pinMode(LED_BUILTIN, OUTPUT);

  Mouse.screenSize(DEFAULT_SCREEN_W, DEFAULT_SCREEN_H);
  Serial1.begin(BAUD);

  // In-protocol "firmware is up" announcement. Deliberately not an ASCII
  // banner -- a host decoder should never have to skip human text.
  sendPong();

  lastRxMs = millis();
}

void loop() {
  bool got = false;
  while (Serial1.available()) {
    feed((uint8_t)Serial1.read());
    got = true;
  }
  if (got) lastRxMs = millis();

  if (movePending) {
    Mouse.moveTo(moveX, moveY);
    movePending = false;
  }

  digitalWrite(LED_BUILTIN, armed() ? HIGH : LOW);

  if (IDLE_RELEASE_MS > 0 && (keyMods || keySlots[0] || buttonMask)) {
    if (millis() - lastRxMs > IDLE_RELEASE_MS) {
      panic();
      lastRxMs = millis();
    }
  }
}
