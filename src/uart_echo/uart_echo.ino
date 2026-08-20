// Step 3: UART only. No HID used here.
//
// Set Tools -> USB Type to "Serial" for this test. Nothing in this
// sketch touches Keyboard/Mouse, so the board cannot type on its own.
//
// Wiring (Adafruit 954, 3.3V logic):
//   black  GND -> Teensy GND
//   green  TXD -> Teensy pin 0 (RX1)
//   white  RXD -> Teensy pin 1 (TX1)
//   red    5V  -> leave disconnected
//
// Test with PuTTY on the controlling PC: serial, 115200 8N1, no flow
// control, local echo OFF (so what you see came back from the Teensy).
//
//   no response  -> TX/RX not crossed, or wrong COM port
//   garbage      -> baud mismatch
//   nothing, LED dark -> check ground

const uint32_t BAUD = 115200;

void setup() {
  pinMode(LED_BUILTIN, OUTPUT);
  Serial1.begin(BAUD);

  // Proves TX works before you type anything.
  Serial1.println();
  Serial1.println("teensy uart echo ready @115200");
}

void loop() {
  while (Serial1.available()) {
    int c = Serial1.read();

    digitalWrite(LED_BUILTIN, HIGH);

    // Echo the raw byte, then its hex value so non-printables are
    // visible. Drop the hex once you trust the link.
    Serial1.write((uint8_t)c);
    Serial1.print(" [0x");
    if (c < 0x10) Serial1.print('0');
    Serial1.print(c, HEX);
    Serial1.println(']');

    digitalWrite(LED_BUILTIN, LOW);
  }
}
