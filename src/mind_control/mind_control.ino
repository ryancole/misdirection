void setup() {
  pinMode(2, INPUT_PULLUP);
}

void loop() {
  if (digitalRead(2) == LOW) {
    Keyboard.println("hello from teensy");
    Mouse.moveTo(500, 500);
    delay(500);
  }
}