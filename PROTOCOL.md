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
