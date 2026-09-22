# Firmware boundaries

`src/main.cpp` contains only Arduino's entry points. `Application` wires the
objects together and services serial input, the controller, then the local UI
on every loop.
The root `.ino` is an Arduino IDE sketch marker; Arduino compiles the sources
under `src/` recursively.

| Directory | Responsibility |
| --- | --- |
| `src/config/` | Build options, board wiring, charger limits/defaults, config validation |
| `src/can/` | `CanTransport` interface and MCP2515 / arduino-CAN adapter |
| `src/protocol/` | Huawei frame IDs, register definitions, fixed-point encoding |
| `src/psu/` | One `Psu` per configured slot; telemetry, staged config, command status; fleet controller |
| `src/storage/` | Versioned EEPROM journal through an injectable `ByteStorage` interface |
| `src/ui/` | Serial parser, encoder input, display rendering, local menu workflow |
| `src/app/` | Startup and composition of hardware adapters and optional UIs |
| `test/` | Host tests with simulated CAN, EEPROM, and serial streams |

The controller, protocol, PSU model, and memory manager are ordinary C++11 with
no Arduino dependencies. A future CAN device implements `CanTransport`; a new
storage device implements `ByteStorage`. Substitute these at the application
composition boundary. The current MCP2515 adapter uses the existing arduino-CAN
library's singleton, so it supports one CAN interface per controller.

## Configuration and addressing

The firmware reserves eight PSU objects (`PSU_MAX_UNITS` in `BuildOptions.h`),
with one slot configured initially. `count` selects the active prefix of those
slots. Every object has its own online and offline setpoints, rated current,
CAN software address, enabled flag, telemetry, Ah estimate, and command result.
Unused slots retain their configuration when the count is reduced.

Software addresses are 1..127. Slot numbers in the console are 1..8, independently
of the address assigned to each slot. `set 2 address 7` changes the controller's
mapping of slot 2 to address 7; it does **not** reprogram the PSU's address.
Duplicate addresses within the configured slots are rejected, including disabled
slots. Address changes clear old telemetry and command state.

The seven-bit address is bits 22..16 of the extended CAN ID. For example:

| Operation | Address 1 | Address 2 |
| --- | --- | --- |
| Request telemetry | `108140FE` | `108240FE` |
| Write setting | `108180FE` | `108280FE` |
| Telemetry reply | `1081407F` | `1082407F` |
| Setting ACK | `1081807E` | `1082807E` |

Address zero is a broadcast address. Normal controller operations enumerate
configured, enabled objects and use addressed frames, including `all` operations.
They do not write to unconfigured units just because those share the bus.

The PSUs negotiate software addresses. These are not immutable physical-device
identities. Verify the address-to-unit mapping after changing the connected
units or their power-up order, particularly when different batteries or loads
need different settings. Automatic discovery, address assignment, and serial
number binding are not implemented by this firmware or the Craig Peacock
reference. Inspect incoming IDs with `raw on`, then configure the mappings.

## Single, range, and bank control

```cpp
// Public C++ ranges are zero-based, end-exclusive. Values use V and A.
controller.modifySingle(0, psu::Parameter::Voltage, 54.0f);
controller.modifyRange(0, 3, psu::Parameter::Current, 5.0f);
controller.applyRange(0, 3); // Three PSUs, each limited to 5 A.
```

`modifySingle` and `modifyRange` stage settings without CAN or EEPROM writes.
Every member of a range validates before any setting changes. Updating current
or rated current also checks that both online and offline current limits fit
within 120% of the configured rating and the configured charger ceiling.

Ranges support a parallel bank by applying a common voltage and per-unit current
limit, while single operations support separate loads or per-unit adjustment.
For three units and a 15 A total limit, set each unit to 5 A. There is no firmware
feedback loop for active current sharing or group-level charge termination;
the original firmware also had neither. Disabled objects keep their config and
receive telemetry, but are skipped for polling and writes. Disabling a slot
does **not** turn its PSU output off.

## Applying settings and polling

Apply queues voltage then current for each selected PSU. Persist queues the
online pair followed by that object's offline pair. At most one write is
outstanding across the controller. An ACK must match its PSU address, register,
and raw value. Replies for other units or older values cannot complete it.

Commands have a 250 ms minimum spacing and a 750 ms ACK timeout. A rejection,
timeout, or transport failure stops the remaining writes for that PSU; the next
selected PSU is still processed. There are no automatic retries of writes.
`status` exposes the last register, raw value, and outcome for every object.
`QUEUED` is not confirmation that hardware accepted the settings. Range writes
are sequential and cannot provide atomic changes or rollback on physical PSUs.

Configuration changes, load/defaults, and EEPROM save are blocked while an apply
job is active. A saved `bootapply 1` queues online settings once on boot; the
default is off. Loading saved settings from the console stages them only.
Neither action implicitly writes offline settings. Reconnection does not
automatically reapply values; the PSU can revert to its own defaults after its
CAN timeout. Use `apply` again or explicitly configure offline values.

Polling is independent of menu state and partial serial input. Requests are
staggered across configured slots, normally giving each enabled PSU one request
per second. Disabled slots retain their place in the schedule. Extended data
frames are used, correcting the original sketch's RTR request.

The MCP2515 interrupt callback only copies complete eight-byte data frames into
a fixed ring (15 usable entries by default). Decoding and all UI output run in
the main loop. The ring's saturating overflow counter appears as `rx-drops`.
Long reports emit a row per loop; description fragments stream without storing
the entire description. Raw tracing can still overrun serial throughput on a
busy bus. The adapter uses arduino-CAN for initialization and reception, but
owns TX buffer 0 directly so transmission is bounded by `canTransmitTimeoutMs`
(default 20 ms). It clears stale TX completion, packs the extended frame, and
requires TX0IF for success. Errors or timeout request a per-buffer abort by
clearing TXREQ; it never waits indefinitely for abort completion or overwrites
a still-busy buffer. Register SPI transactions use the library's registered
interrupt masking. See the [MCP2515 datasheet, sections 3.4/3.6](https://ww1.microchip.com/downloads/en/DeviceDoc/MCP2515-Family-Data-Sheet-DS20001801K.pdf).

A frame already transmitting can finish despite an abort request: a transport
failure is not proof that the PSU did not receive it. Writes are not retried
automatically. Transmission remains synchronous for this bounded interval;
a fully nonblocking adapter can still use the same controller/UI APIs.

The serial console echoes input by default (`echo off` disables it), treats
CRLF as a single Enter, and prints a prompt after startup and command output.
Input draining is limited to 32 bytes per loop. Long reports pause while a line
is being edited. Raw frames and asynchronous ACKs start on a fresh line and
restore the prompt plus any echoed input. Device description control characters
are replaced with `?` and NUL padding is omitted. Descriptions stream until their
final fragment; other output or typing can split the description across lines.
An empty Enter prints a new prompt. Input inactivity expires a pending command
into discard-through-newline state; Ctrl-X/C resets console state and Ctrl-U
clears input immediately. None of these operations modifies controller state,
EEPROM, or queued PSU jobs. `hello` restores a quiet console at a clean command
boundary. The UART cannot infer USB connection state; see [serial lifecycle](SERIAL.md).
Optional display startup probes its
I2C address and skips an absent display. Wire transactions have a configured
25 ms timeout, so an I2C fault cannot cause an indefinite startup wait.

## Controller EEPROM

`MemoryManager` uses standard Arduino `EEPROM.read`, `EEPROM.update`, and
`EEPROM.length` through `ArduinoEeprom`. Only explicit `save` or the local
Save EEPROM action writes it. On the Mega, all bytes above 511 remain untouched.

Two 256-byte slots reserve exactly 512 bytes. With eight PSU objects, each record
uses 116 bytes: 16 header bytes, 4 controller bytes, and 12 bytes per PSU. Integer
fields have explicit little-endian encoding, independent of compiler struct
padding. Setpoints are centivolts/centiamps. Session telemetry and Ah totals are
not persisted.

| Relative offset | Contents |
| --- | --- |
| 0 | Commit marker `A5`, written last |
| 1 | Schema version `1` |
| 2..3 | Record size |
| 4..7 | Generation counter, wrapping comparison |
| 8..9 | CRC-16/CCITT-FALSE of the record except bytes 0, 8, 9 |
| 10..13 | `R48C` magic |
| 14..15 | Reserved, zero |
| 16 | Configured slot count |
| 17 | Apply-online-settings-on-boot flag |
| 18..19 | Poll interval in milliseconds |
| 20 onward | PSU records: address, enabled, online V/A, offline V/A, rated A |

Saving invalidates the older slot, writes its new header/payload/checksum,
commits it last, and verifies the result. The previous complete record remains
available if a write is interrupted. Boot chooses the newest valid record;
invalid, incompatible, or blank EEPROM falls back to compiled defaults without
silently overwriting EEPROM. `load` failure leaves RAM unchanged. `defaults`
resets RAM; follow it with `save` to make the reset survive reboot. Ordinary
hardware brownout protection is still needed for physical EEPROM integrity.

Changing `PSU_MAX_UNITS` changes record size and invalidates the old schema layout.
All supplied build profiles use the same capacity, so their EEPROM is compatible.
There is no migration from the original sketch: it had no controller EEPROM
format. A save is synchronous and may pause main-loop work for a few hundred
milliseconds; it is disallowed while awaiting a command ACK.
