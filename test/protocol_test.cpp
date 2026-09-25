// Host-side tests for the misdirection firmware's parser and dispatch.
//
// The sketch is #included directly with a stub Arduino core underneath
// it, so the code under test is the exact source that gets flashed. No
// hardware is needed. Build and run with etc/test.ps1.

#include "stubs/Arduino.h"

namespace fake {
  uint32_t             nowMs = 1000;
  int                  pinLevels[64] = { 0 };
  std::deque<uint8_t>  rx;
  std::vector<uint8_t> tx;
}
FakeSerial   Serial1;
FakeKeyboard Keyboard;
FakeMouse    Mouse;

#include "../src/misdirection/misdirection.ino"

#include <stdio.h>
#include <string>
#include <vector>

// ------------------------------------------------------------- harness

static int g_failures = 0;
static int g_checks = 0;

static std::string hex(const std::vector<uint8_t> &v) {
  std::string s;
  char buf[4];
  for (size_t i = 0; i < v.size(); i++) {
    snprintf(buf, sizeof buf, "%02X", v[i]);
    if (i) s += ' ';
    s += buf;
  }
  return s.empty() ? "(none)" : s;
}

#define CHECK(cond)                                                          \
  do {                                                                       \
    g_checks++;                                                              \
    if (!(cond)) {                                                           \
      g_failures++;                                                          \
      printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);               \
    }                                                                        \
  } while (0)

#define CHECK_TX(expected)                                                   \
  do {                                                                       \
    g_checks++;                                                              \
    std::vector<uint8_t> e = (expected);                                     \
    if (fake::tx != e) {                                                     \
      g_failures++;                                                          \
      printf("  FAIL %s:%d: tx\n    want %s\n    got  %s\n", __FILE__,       \
             __LINE__, hex(e).c_str(), hex(fake::tx).c_str());               \
    }                                                                        \
  } while (0)

static std::vector<uint8_t> encode(uint8_t type, std::vector<uint8_t> payload = {}) {
  std::vector<uint8_t> f = { SOF, type, (uint8_t)payload.size() };
  uint8_t sum = (uint8_t)(type + payload.size());
  for (uint8_t b : payload) { f.push_back(b); sum = (uint8_t)(sum + b); }
  f.push_back(sum);
  return f;
}

static std::vector<uint8_t> nack(uint8_t reason) { return encode(RSP_NACK, { reason }); }
static std::vector<uint8_t> pong()               { return encode(RSP_PONG, { PROTOCOL_VERSION }); }

static std::vector<uint8_t> fileDelay(uint32_t micros) {
  return encode(0x7F, { (uint8_t)micros, (uint8_t)(micros >> 8),
                        (uint8_t)(micros >> 16), (uint8_t)(micros >> 24) });
}

static std::vector<uint8_t> moveRel(int16_t dx, int16_t dy) {
  return encode(MSG_MOUSE_MOVE_REL, { (uint8_t)dx, (uint8_t)(dx >> 8),
                                      (uint8_t)dy, (uint8_t)(dy >> 8) });
}

static void arm(bool on) { fake::pinLevels[PIN_ARM] = on ? LOW : HIGH; }

// Fresh firmware: boot, discard the boot PONG, armed by default.
static void reset() {
  fake::rx.clear();
  fake::tx.clear();
  fake::nowMs += 10000;   // step past any NACK(4) throttle window
  arm(true);
  setup();
  panic();                // clear key/button state left by a previous test
  Keyboard = FakeKeyboard();   // zero the counters *after* panic()'s own pushes
  Mouse    = FakeMouse();
  fake::tx.clear();
}

static void send(const std::vector<uint8_t> &bytes) {
  for (uint8_t b : bytes) fake::rx.push_back(b);
  loop();
}

static bool noHidActivity() {
  return Keyboard.sends == 0 && Keyboard.releaseAlls == 0 &&
         Mouse.moves == 0 && Mouse.relMoves == 0 &&
         Mouse.buttonSets == 0 && Mouse.scrolls == 0;
}

#define TEST(name) static void name(); struct name##_reg { name##_reg() { tests().push_back({ #name, name }); } } name##_inst; static void name()

struct TestCase { const char *name; void (*fn)(); };
static std::vector<TestCase> &tests() { static std::vector<TestCase> t; return t; }

// --------------------------------------------------------------- tests

TEST(boot_emits_pong) {
  fake::tx.clear();
  arm(true);
  setup();
  CHECK_TX(pong());
}

TEST(ping_gets_pong) {
  reset();
  send(encode(MSG_PING));
  CHECK_TX(pong());
}

TEST(bad_checksum_gets_nack_1) {
  reset();
  std::vector<uint8_t> f = encode(MSG_PING);
  f.back() ^= 0xFF;
  send(f);
  CHECK_TX(nack(NACK_CHECKSUM));
}

TEST(key_down_updates_report) {
  reset();
  send(encode(MSG_KEY_DOWN, { 0x04 }));
  CHECK(fake::tx.empty());
  CHECK(Keyboard.sends == 1);
  CHECK(Keyboard.keys[0] == 0x04);
}

// MOUSE_MOVE_REL (0x08): relative delta, split into +/-127 steps, never
// coalesced.

TEST(move_rel_small_is_one_report) {
  reset();
  send(moveRel(10, -5));
  CHECK(fake::tx.empty());
  CHECK(Mouse.relMoves == 1);
  CHECK(Mouse.relSteps == std::vector<int8_t>({ 10, -5 }));
  CHECK(Mouse.moves == 0);   // must not touch the absolute path
}

TEST(move_rel_large_is_split_into_127_steps) {
  reset();
  send(moveRel(300, 0));
  CHECK(Mouse.relMoves == 3);
  CHECK(Mouse.relSteps == std::vector<int8_t>({ 127, 0, 127, 0, 46, 0 }));
  CHECK(Mouse.relSumX == 300 && Mouse.relSumY == 0);
}

TEST(move_rel_extremes_sum_exactly) {
  reset();
  send(moveRel(-32768, 32767));
  CHECK(Mouse.relSumX == -32768);
  CHECK(Mouse.relSumY == 32767);
  // ceil(32768 / 127) = 259 reports; the longer axis sets the count.
  CHECK(Mouse.relMoves == 259);
}

TEST(move_rel_zero_sends_nothing) {
  reset();
  send(moveRel(0, 0));
  CHECK(Mouse.relMoves == 0);
  CHECK(fake::tx.empty());
}

TEST(move_rel_is_not_coalesced) {
  // Two frames in one pass both apply; absolute MOUSE_MOVE would keep
  // only the last.
  reset();
  std::vector<uint8_t> stream = moveRel(5, 5);
  std::vector<uint8_t> second = moveRel(-2, 3);
  stream.insert(stream.end(), second.begin(), second.end());
  send(stream);
  CHECK(Mouse.relMoves == 2);
  CHECK(Mouse.relSumX == 3 && Mouse.relSumY == 8);
}

TEST(move_rel_bad_length_is_nack_3) {
  reset();
  send(encode(MSG_MOUSE_MOVE_REL, { 1, 2 }));
  CHECK_TX(nack(NACK_LENGTH));
  CHECK(noHidActivity());
}

TEST(move_rel_disarmed_is_dropped) {
  reset();
  arm(false);
  send(moveRel(50, 50));
  CHECK_TX(nack(NACK_DISARMED));
  CHECK(noHidActivity());
}

TEST(move_rel_vectors_match_spec) {
  CHECK(moveRel(10, -5)        == std::vector<uint8_t>({ 0xAB, 0x08, 0x04, 0x0A, 0x00, 0xFB, 0xFF, 0x10 }));
  CHECK(moveRel(300, 0)        == std::vector<uint8_t>({ 0xAB, 0x08, 0x04, 0x2C, 0x01, 0x00, 0x00, 0x39 }));
  CHECK(moveRel(-32768, 32767) == std::vector<uint8_t>({ 0xAB, 0x08, 0x04, 0x00, 0x80, 0xFF, 0x7F, 0x0A }));
}

// FILE_DELAY (0x7F) is a .msdr file record and never valid on the wire.
// The firmware must not know it: it goes down the unknown-type path.

TEST(file_delay_armed_is_nack_unknown_type) {
  reset();
  send(fileDelay(16667));
  CHECK_TX(nack(NACK_TYPE));
  CHECK(noHidActivity());
}

TEST(file_delay_disarmed_is_nack_disarmed) {
  // Same as every other non-config message: while disarmed the arm gate
  // answers first, so an unknown type gets NACK(4), not NACK(2).
  reset();
  arm(false);
  send(fileDelay(1000));
  CHECK_TX(nack(NACK_DISARMED));
  CHECK(noHidActivity());
}

TEST(file_delay_any_length_is_still_unknown_type) {
  // Length is only validated for known types; a zero-length 0x7F is
  // rejected for its type, not its length.
  reset();
  send(encode(0x7F));
  CHECK_TX(nack(NACK_TYPE));
  CHECK(noHidActivity());
}

TEST(file_delay_does_not_desync_parser) {
  reset();
  std::vector<uint8_t> stream = fileDelay(0xFFFFFFFF);
  std::vector<uint8_t> key = encode(MSG_KEY_DOWN, { 0x04 });
  stream.insert(stream.end(), key.begin(), key.end());
  send(stream);
  CHECK_TX(nack(NACK_TYPE));
  CHECK(Keyboard.sends == 1);
  CHECK(Keyboard.keys[0] == 0x04);
}

TEST(file_delay_vectors_match_spec) {
  // The bytes PROTOCOL.md and etc/protocol-vectors.json publish for a
  // .msdr encoder, pinned here so the three cannot drift.
  CHECK(fileDelay(0)          == std::vector<uint8_t>({ 0xAB, 0x7F, 0x04, 0x00, 0x00, 0x00, 0x00, 0x83 }));
  CHECK(fileDelay(1)          == std::vector<uint8_t>({ 0xAB, 0x7F, 0x04, 0x01, 0x00, 0x00, 0x00, 0x84 }));
  CHECK(fileDelay(1000)       == std::vector<uint8_t>({ 0xAB, 0x7F, 0x04, 0xE8, 0x03, 0x00, 0x00, 0x6E }));
  CHECK(fileDelay(16667)      == std::vector<uint8_t>({ 0xAB, 0x7F, 0x04, 0x1B, 0x41, 0x00, 0x00, 0xDF }));
  CHECK(fileDelay(0xFFFFFFFF) == std::vector<uint8_t>({ 0xAB, 0x7F, 0x04, 0xFF, 0xFF, 0xFF, 0xFF, 0x7F }));
}

// ---------------------------------------------------------------- main

int main() {
  for (const TestCase &t : tests()) {
    int before = g_failures;
    t.fn();
    printf("%s %s\n", g_failures == before ? "ok  " : "FAIL", t.name);
  }
  printf("\n%d checks, %d failures\n", g_checks, g_failures);
  return g_failures ? 1 : 0;
}
