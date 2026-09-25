# Wire protocol (controlling PC <-> Teensy over Serial1)

Binary, little-endian, no ASCII. One mouse update is one 8-byte write.

## Frame

```
[0xAB][type][len][payload 0..255][sum]
```

`sum = (type + len + payload bytes) & 0xFF`

The reader is a state machine: HUNT -> TYPE -> LEN -> PAYLOAD -> SUM.
On a bad checksum or unknown type, discard the frame and go back to
HUNT. `len` is known before the payload is read, so a 0xAB byte inside a
payload is not a framing hazard except while resyncing.

## Host -> Teensy

| type | name        | len | payload                                   |
|------|-------------|-----|-------------------------------------------|
| 0x00 | PANIC       | 0   | -                                         |
| 0x01 | KEY_DOWN    | 1   | HID usage code                            |
| 0x02 | KEY_UP      | 1   | HID usage code                            |
| 0x03 | MOUSE_MOVE  | 4   | x:u16, y:u16 (pixels in current screen)   |
| 0x04 | MOUSE_BTN   | 1   | button mask, absolute state               |
| 0x05 | MOUSE_WHEEL | 2   | vert:i8, horiz:i8                         |
| 0x06 | SCREEN_SIZE | 4   | w:u16, h:u16                              |
| 0x07 | PING        | 0   | -                                         |

Button mask: bit0 left, bit1 right, bit2 middle, bit3 back, bit4 forward.

## Teensy -> host

The white wire makes Serial1 bidirectional, so there is a back-channel.
Same framing.

| type | name | len | payload                  |
|------|------|-----|--------------------------|
| 0x80 | PONG | 1   | protocol version         |
| 0x81 | NACK | 1   | reason code              |

Reason codes: 1 bad checksum, 2 unknown type, 3 bad length,
4 disarmed, 5 key rollover full.

## Reserved type codes (never on the wire)

| type | name       | len | payload                                        |
|------|------------|-----|------------------------------------------------|
| 0x7F | FILE_DELAY | 4   | micros:u32, time since the previous file frame |

FILE_DELAY exists only inside `.msdr` files (see below). It is a
file-format record that borrows the frame encoding so a file is a plain
sequence of frames, and it is **never valid on the wire**:

- The host must not send it. A sender replaying a file sleeps for the
  delay and skips the frame.
- The firmware does not know the code. If one arrives anyway it is
  handled exactly like any other unknown type: NACK(2) while armed, or
  the throttled NACK(4) while disarmed, with no HID side effects either
  way. The parser resyncs on the next frame as usual.
- Future wire message types must not take 0x7F. It is reserved in this
  table so the wire and file namespaces cannot collide.

The payload is a little-endian u32 of microseconds elapsed since the
previous frame in the file (0 means "no gap"; 0xFFFFFFFF is about 71
minutes). The checksum is the normal `(type + len + payload) & 0xFF`.
Adding or removing FILE_DELAY records does not change the protocol
version, because the wire protocol is unchanged.

## `.msdr` files

`.msdr` is the container the host client uses to save a message
sequence for later replay. It is defined by the client, not the
firmware; it is described here only so FILE_DELAY has context.

```
"MSDR"   4 bytes   magic
u8       1 byte    file format version
u8       1 byte    protocol version the frames were recorded against
frames   ...       wire frames back to back, each [0xAB][type][len][payload][sum]
```

The 6-byte header is followed by frames exactly as they would appear on
Serial1, interleaved with FILE_DELAY records that carry the gap before
the frame that follows them. A replayer reads frames in order: on
FILE_DELAY it sleeps for `micros` and emits nothing; on anything else it
writes the frame bytes to the wire unchanged. A file with no FILE_DELAY
records is a valid file that replays as fast as the link allows.

## Keyboard state lives in the firmware

Raw HID usage codes go on the wire; the firmware owns the report. This
avoids mapping to Teensy's `KEY_*` / `MODIFIERKEY_*` constants, which
are a mix of `usage | 0xF000` and bit flags.

- usage 0xE0..0xE7 -> modifier bit `1 << (usage - 0xE0)`
- anything else -> one of six key slots
- duplicate KEY_DOWN is idempotent; KEY_UP on an unheld key is a no-op
- slots full -> NACK(5), not a silent drop

Emit with the low-level API, which takes raw usage bytes directly:

```cpp
Keyboard.set_modifier(mods);
Keyboard.set_key1(slot[0]);  // ... through set_key6
Keyboard.send_now();
```

KEY_DOWN and KEY_UP stay separate messages. Collapsing them into a
"keystroke" makes holding a modifier while dragging impossible.
Mouse buttons are the mirror image: one absolute mask, so a dropped
frame self-corrects on the next update and PANIC is trivially correct.

## Coordinates

`Mouse.moveTo(x, y)` is in *screen pixels*, scaled by whatever was last
given to `Mouse.screenSize(w, h)`. Without that call the core assumes
**1366x768** and your coordinates land in the wrong place. Send
SCREEN_SIZE at connect and on any resolution change. The core clamps
each axis to 128..7680.

The absolute report itself is 0..32767 on both axes (report ID 2); the
scaling from screen pixels happens inside the core.

## Coalescing

Mouse motion is the only high-rate message. Drain everything available
from Serial1 each pass, keep only the *latest* MOUSE_MOVE, and emit one
HID report per pass. A PL2303 that buffers writes until its latency
timer fires will deliver several moves at once; replaying all of them
just spends USB frames on stale positions.

## Bandwidth

A MOUSE_MOVE frame is 8 bytes. At 500 updates/sec that is 4 kB/s, or
40 kbaud with 8N1 framing — comfortable at 115200. Serial1 will run at
1 Mbaud+ if the PL2303 turns out to be the bottleneck; the UART is not
the limit.

## Panic

PANIC (0x00) is honored regardless of arm state: `releaseAll()`,
`set_buttons(0,0,0,0,0)`, and clear the local modifier/slot state so
firmware and host agree again. Byte 0x00 alone is also treated as a
panic by the naive step-4 bridge, so the same habit works at both
stages.

## Host contract

Points a host author will otherwise have to discover by experiment.

**Keys are HID usage codes, not ASCII and not Windows virtual-key
codes.** `a` is 0x04, not 0x61 and not 0x41. Mapping from whatever your
capture layer produces is the host's job; the firmware deliberately does
no translation, which is what keeps layouts and dead keys out of it.

**Connect sequence.** The firmware emits a PONG frame at boot, so a host
that sees one knows the board just reset. Send SCREEN_SIZE before the
first MOUSE_MOVE; until then coordinates are interpreted against the
firmware's 1920x1080 default.

**Nothing takes effect until pin 2 is grounded.** While disarmed, input
messages are dropped and the firmware answers NACK(4), throttled to one
per 500 ms so a streaming host does not swamp the return channel.
PANIC, PING, and SCREEN_SIZE are honored regardless.

**Fire and forget.** There is no per-frame ACK and you should not wait
for one -- a round trip per mouse update would cost more than the moves
are worth. NACK arrives only on error, asynchronously. Use PING/PONG for
liveness if you want it.

**Payloads over 16 bytes are rejected** with NACK(3) and the parser
resyncs. The framing allows `len` up to 255, but nothing defined needs
more than 4, so a corrupt length byte cannot swallow a long run of
stream.

**Serial settings:** 115200 8N1, no flow control. DTR/RTS are not used.

**The idle-release watchdog is off by default** (`IDLE_RELEASE_MS = 0`).
Turn it on only once the host sends a keepalive, or a legitimate long
modifier hold will be released out from under it.

## Test vectors

Encode these and compare bytes; no hardware required. Checksums are
`(type + len + payload) & 0xFF`.

The same vectors are in `etc/protocol-vectors.json` in machine-readable
form, with each frame's decoded field values alongside its bytes, so a
test suite in any language can assert both directions. Both come from
`etc/gen-vectors.py`; regenerate rather than hand-editing either.

| Message | Bytes |
|---------|-------|
| PANIC                        | `AB 00 00 00` |
| KEY_DOWN a (usage 0x04)      | `AB 01 01 04 06` |
| KEY_UP a                     | `AB 02 01 04 07` |
| KEY_DOWN LeftShift (0xE1)    | `AB 01 01 E1 E3` |
| KEY_UP LeftShift             | `AB 02 01 E1 E4` |
| MOUSE_MOVE to (960, 540)     | `AB 03 04 C0 03 1C 02 E8` |
| MOUSE_MOVE to (0, 0)         | `AB 03 04 00 00 00 00 07` |
| MOUSE_BTN left down          | `AB 04 01 01 06` |
| MOUSE_BTN all released       | `AB 04 01 00 05` |
| MOUSE_WHEEL up 1             | `AB 05 02 01 00 08` |
| MOUSE_WHEEL down 1           | `AB 05 02 FF 00 06` |
| SCREEN_SIZE 1920x1080        | `AB 06 04 80 07 38 04 CD` |
| PING                         | `AB 07 00 07` |
| PONG v1 (Teensy -> host)     | `AB 80 01 01 82` |
| NACK checksum (Teensy -> host) | `AB 81 01 01 83` |
| FILE_DELAY 0 us (file only)  | `AB 7F 04 00 00 00 00 83` |
| FILE_DELAY 1 us (file only)  | `AB 7F 04 01 00 00 00 84` |
| FILE_DELAY 1000 us (file only) | `AB 7F 04 E8 03 00 00 6E` |
| FILE_DELAY 16667 us (file only) | `AB 7F 04 1B 41 00 00 DF` |
| FILE_DELAY 0xFFFFFFFF us (file only) | `AB 7F 04 FF FF FF FF 7F` |

The FILE_DELAY rows are for checking a `.msdr` encoder and decoder only;
sending one to the firmware gets a NACK, which the firmware test suite
in `test/` pins down.

A shift-drag, end to end, is just these in order: KEY_DOWN 0xE1,
MOUSE_BTN 0x01, a run of MOUSE_MOVE, MOUSE_BTN 0x00, KEY_UP 0xE1.
