// Step 4: naive join. Byte in on Serial1 -> keystroke out on USB HID.
// Throwaway code whose only job is to prove the whole chain works.
// The real framed protocol replaces this in step 5.
//
// Tools -> USB Type must include Keyboard (an absolute pointing device
// is not needed yet, but selecting the final combination now means one
// less variable to change later).
//
// Safety:
//   * pin 2 to GND is the arm gate. Ungated, anything that burps on the
//     UART types into the target PC.
//   * byte 0x00 is the panic valve: release everything. It is honored
//     even when disarmed, so a stuck key can always be cleared.
//
// From PuTTY, whatever you type should appear on the target PC.

const uint32_t BAUD = 115200;
const uint8_t  PIN_ARM = 2;   // low = armed
const uint8_t  PANIC = 0x00;

static bool armed() { return digitalRead(PIN_ARM) == LOW; }

void setup() {
  pinMode(PIN_ARM, INPUT_PULLUP);
  pinMode(LED_BUILTIN, OUTPUT);
  Serial1.begin(BAUD);
  Serial1.println("uart->hid bridge ready (ground pin 2 to arm)");
}

void loop() {
  while (Serial1.available()) {
    uint8_t b = Serial1.read();

    if (b == PANIC) {
      Keyboard.releaseAll();
      Mouse.set_buttons(0, 0, 0);
      Serial1.println("panic: released all");
      continue;
    }

    if (!armed()) {
      Serial1.println("disarmed: byte dropped");
      continue;
    }

    digitalWrite(LED_BUILTIN, HIGH);
    Keyboard.write(b);   // ASCII in, keystroke out
    digitalWrite(LED_BUILTIN, LOW);
  }
}
