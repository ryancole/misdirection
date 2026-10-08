# misdirection — project context

## How it's used (mind-control)

The host-side client is mind-control, an AI coaching tool. Its VODs are
screen recordings from the player's own view (camera lock), not `.rofl`
replays, and the coach learns from them. mind-control writes the coach's
decisions to `.msdr` files:

- **Timed playback** (the default) replays a finished recording from its
  start, over a VOD.
- **Follow mode** (`--follow`) plays decisions as they're written, live,
  in a **practice-mode game against bots only**. No other people are in
  the game, and only Ryan runs this software.

The `.msdr` player and `--follow` live in the client repo, not here.

## What this is

Firmware for a **Teensy 4.1 that Ryan built and physically owns**. It
presents to a target PC as a USB keyboard/mouse and takes input commands
from Ryan's own controlling PC over a UART cable (see README.md wiring).
Both PCs, the Teensy, and the cable are on Ryan's desk. There is no
network component: the control channel is a physical serial cable, and
the only USB endpoint is the target PC that Ryan plugs the board into.

This is ordinary embedded/protocol development: a framed binary wire
format (PROTOCOL.md), a firmware parser, and native host-side tests.
Routine work here includes adding message types (key/mouse events),
firmware handlers, test vectors, and tests.

## Built-in safety gates

- **Arm pin.** No input reaches the target until pin 2 is physically
  grounded on the board. Disarmed, input messages are dropped.
- **PANIC** (0x00) releases every key and button regardless of arm state.
- The idle-release watchdog exists and is off by default.

## Working conventions

- PROTOCOL.md is the contract; `src/misdirection/misdirection.ino`
  implements it. Change one, change both.
- Test vectors come from `etc/gen-vectors.py`; regenerate rather than
  hand-edit `etc/protocol-vectors.json` or the table in PROTOCOL.md.
- Host-side tests: `etc\test.ps1` (MSVC, no board needed). Add a test in
  `test/protocol_test.cpp` for every new message type, and extend
  `test/stubs/Arduino.h` if the handler calls a new core API.
- 0x7F is reserved for FILE_DELAY (`.msdr` files) and must never be
  assigned to a wire message.
