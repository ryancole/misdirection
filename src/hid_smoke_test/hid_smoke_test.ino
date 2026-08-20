// Step 2: HID smoke test. Ground pin 2 to make it type and move.
//
// Keep this sketch. Reflashing it later tells you in seconds whether a
// fault is HID or the link.
//
// Set SCREEN_W/SCREEN_H to the TARGET PC's actual resolution. Without a
// screenSize() call the core assumes 1366x768 and moveTo() lands
// somewhere else entirely -- which looks exactly like a broken absolute
// pointer. Each axis is clamped to 128..7680.

const uint8_t  PIN_ARM   = 2;      // low = armed
const uint16_t SCREEN_W  = 1920;
const uint16_t SCREEN_H  = 1080;

void setup() {
  pinMode(PIN_ARM, INPUT_PULLUP);
  Mouse.screenSize(SCREEN_W, SCREEN_H);
}

void loop() {
  if (digitalRead(PIN_ARM) == LOW) {
    Keyboard.println("hello from teensy");

    // Centre is easy to eyeball. For a stricter check use (0, 0): the
    // pointer should pin to the very top-left corner, which catches a
    // scaling error that a centred move hides.
    Mouse.moveTo(SCREEN_W / 2, SCREEN_H / 2);

    delay(500);
  }
}
