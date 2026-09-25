// Minimal stand-in for the Teensy core so src/misdirection/misdirection.ino
// can be compiled and driven natively. Only what the sketch touches is
// modelled; everything records what the firmware did so a test can
// assert on it. Not a general Arduino emulator.
#pragma once

#include <stdint.h>
#include <stddef.h>
#include <deque>
#include <vector>

#define LOW          0
#define HIGH         1
#define INPUT        0
#define OUTPUT       1
#define INPUT_PULLUP 2
#define LED_BUILTIN  13

namespace fake {
  extern uint32_t             nowMs;          // what millis() returns
  extern int                  pinLevels[64];  // digitalRead()/digitalWrite()
  extern std::deque<uint8_t>  rx;             // bytes waiting for the firmware
  extern std::vector<uint8_t> tx;             // bytes the firmware wrote
}

inline void     pinMode(uint8_t, uint8_t) {}
inline int      digitalRead(uint8_t pin)       { return fake::pinLevels[pin]; }
inline void     digitalWrite(uint8_t pin, int v) { fake::pinLevels[pin] = v; }
inline uint32_t millis()                       { return fake::nowMs; }

struct FakeSerial {
  void   begin(uint32_t) {}
  int    available() { return (int)fake::rx.size(); }
  int    read() {
    if (fake::rx.empty()) return -1;
    uint8_t b = fake::rx.front();
    fake::rx.pop_front();
    return b;
  }
  size_t write(uint8_t b) { fake::tx.push_back(b); return 1; }
};

struct FakeKeyboard {
  uint8_t mods = 0;
  uint8_t keys[6] = { 0, 0, 0, 0, 0, 0 };
  int     sends = 0;
  int     releaseAlls = 0;

  void set_modifier(uint8_t m) { mods = m; }
  void set_key1(uint8_t k) { keys[0] = k; }
  void set_key2(uint8_t k) { keys[1] = k; }
  void set_key3(uint8_t k) { keys[2] = k; }
  void set_key4(uint8_t k) { keys[3] = k; }
  void set_key5(uint8_t k) { keys[4] = k; }
  void set_key6(uint8_t k) { keys[5] = k; }
  void send_now() { sends++; }
  void releaseAll() { releaseAlls++; }
};

struct FakeMouse {
  uint16_t screenW = 0, screenH = 0;
  uint16_t x = 0, y = 0;
  int      moves = 0;
  int      buttonSets = 0;
  int      scrolls = 0;
  uint8_t  buttons[5] = { 0, 0, 0, 0, 0 };
  int8_t   scrollV = 0, scrollH = 0;

  void screenSize(uint16_t w, uint16_t h) { screenW = w; screenH = h; }
  void moveTo(uint16_t nx, uint16_t ny) { x = nx; y = ny; moves++; }
  void set_buttons(uint8_t l, uint8_t m, uint8_t r, uint8_t b, uint8_t f) {
    buttons[0] = l; buttons[1] = m; buttons[2] = r; buttons[3] = b; buttons[4] = f;
    buttonSets++;
  }
  void scroll(int8_t v, int8_t h) { scrollV = v; scrollH = h; scrolls++; }
};

extern FakeSerial   Serial1;
extern FakeKeyboard Keyboard;
extern FakeMouse    Mouse;
