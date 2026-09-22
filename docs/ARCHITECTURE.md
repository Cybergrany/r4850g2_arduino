# Firmware boundaries and UI contracts

`Application` composes adapters and optional UIs. Its loop services serial,
controller, then local display/input. Core configuration, protocol, discovery,
controller, and storage code are ordinary C++11 without Arduino dependencies.

| Directory | Responsibility |
| --- | --- |
| `src/config/` | Deployment template, board wiring, limits, configuration types and validation |
| `src/can/` | Injectable `CanTransport` plus MCP2515 adapter and bounded transmission |
| `src/protocol/` | Huawei frame classification, registers, numeric encoding |
| `src/psu/` | Discovered physical devices, stable slot bindings, group allocation, apply/recovery state machines |
| `src/storage/` | Versioned journal over `ByteStorage`; Arduino adapter uses standard EEPROM |
| `src/ui/` | Serial commands, diagnostic rendering, ILI9341 layout/renderer and wheel navigation/editing |
| `src/app/` | Startup composition and cooperative scheduling |
| `test/` | Production code exercised with simulated hardware/time |

A future CAN adapter implements `CanTransport`; a future memory device implements
`ByteStorage`. Neither change requires rebuilding group logic inside the UI.
The MCP2515/arduino-CAN adapter currently serves one physical CAN interface.

## Persistent model versus observed hardware

There is one parallel DC bus, one online voltage, one offline voltage, and up to
eight disjoint named source groups. Group configuration contains membership,
online total current, and offline total current. A member is a stable controller
slot with a six-byte identity binding and an explicit rated current.

`Configuration::operating` is separate from drafts: it contains the authorized
voltage, per-slot authorized currents, and validity masks. Queueing a validated
online apply authorizes its intended values; the asynchronous result separately
reports what was acknowledged. A partial apply authorizes only its recipients.
Saving drafts never authorizes them. Changing membership/bindings invalidates
affected current authorization, without sending writes or changing live peers.
There is no hidden total-current ceiling or feedback redistribution loop.
Voltage is still one shared value; its per-slot authorization mask prevents a
replacement or newly added slot inheriting reboot permission from an old profile.
Until all configured identities are explicitly authorized for that voltage,
automatic restoration waits for a complete manual voltage/all apply.

`Discovery` holds `PSU_MAX_UNITS + 4` physical `Psu` observations, including
unbound devices. A `Psu` contains runtime address, reported identity, identity
samples/lease, observation epoch, telemetry, raw alarm bits, and freshness.
Addresses and telemetry are never persisted. Lookups by identity reject ambiguity.
Address changes, identity changes, stale-to-live transitions and conflicts
invalidate epochs. Slots retain their config when count shrinks; existing group
membership must first be edited to permit shrinking.

`Deployment.h` is the installation definition file. Its unique ID prevents
EEPROM from a previous installation authorizing control after firmware is moved.
The compiled group/binding template starts with no operating authorization or
auto-resume; runtime commissioning and explicit save normally supply these.

## Public backend hooks

All public C++ slot/group indexes are **zero-based**, unlike serial slot numbers.
Setpoints are unsigned **centivolts/centiamps**, not floats. Membership uses one
bit per slot. Public getters expose const state; changes use validated APIs.

```cpp
controller.setCount(2);
// id1/id2 come from verified Discovery entries; rating is commissioned explicitly.
controller.bind(0, id1, 5000, now);
controller.bind(1, id2, 5000, now);
controller.setGroup("GROUP1", 0x03);  // slots 0 and 1
const int8_t group = controller.groupIndex("GROUP1");
controller.setVoltage(5400);         // one global voltage
controller.setCurrent(group, 5500);  // total 55 A, shares 27.50 A

const auto plan = controller.preview(psu::Operation::All, -1, now);
// Render every plan.issues[i], its recipients, voltage and current[i].
if (plan.blocker == psu::Issue::None) {
  const auto result = controller.queue(plan, false, now);
  // Check result, then poll report() until active is false. Queue != hardware success.
  (void)result;
}
```

Real callers must check **every** mutation result before continuing; the example
omits repeated error handling to show the sequence. Configuration changes are
atomic in RAM and return Busy while a job runs. The old independent-pack
`modifySingle`/`modifyRange` APIs have been removed.

| Hook | UI responsibility / data |
| --- | --- |
| `configuration()` | Global drafts, deployment, groups, bindings, ratings, authorized profile |
| `setGroup`, `removeGroup`, `setCount`, `bind`, `unbind` | Commissioning and membership changes; no CAN settings sent |
| `checkCurrent(group,total)` | Structured capacity error with limiting slot, requested share and maximum |
| `setCurrent`, `setVoltage` | Stage validated online/offline drafts; no automatic clamping |
| `groupStatus(group,now)` | Membership/responding/ready/missing masks and requested/authorized totals |
| `busStatus(now)` | Configured slots versus fresh responders, verified identities and broadcasting units |
| `issue(slot,now)` | Missing, unbound, unassigned, conflict, identity pending, data stale, not ready, deployment mismatch |
| `voltageSynchronized(slot)`, `currentSynchronized(slot)` | Runtime ACK flags; clear on boot/identity loss and affected writes |
| `freshMetric`, `summarize`, `sessionTotal` | Per-field freshness, partial sums/means/maxima, and volatile session totals |
| `groupCurrentMaximum(group)` | Exact accepted maximum using the same allocation/validation as serial |
| `configurationRevision()` | Invalidate an edit when another interface changes configuration |
| `discovery()` | Read-only observed devices, identity verification, current addresses, epochs, registry counters |
| `deviceForSlot`, `metric` | Last observed measurements; `issue`/timestamps must also be checked for freshness |
| `preview(op,group,now,partial)` | Snapshot of recipients, missing members, shares, per-slot blockers, configuration/topology revisions |
| `queue(plan,confirmed,now)` | Revalidate the plan and queue work, or return Changed/Blocked/ConfirmRequired/Busy |
| `report()` | Monotonic operation sequence, active operation, requested/recipient/succeeded/failed/skipped masks, per-slot command state/register/value |
| `scan`, `requestPoll`, `requestDescription` | Read-only bus diagnostics |
| `observeFrames` | Optional raw/description observer; parsing and state transitions remain in the core |

Serial and the local UI consume these types directly. It must distinguish requested,
authorized, acknowledged, measured, and stale data. `report().units` retains each
slot's last command state even when another group's operation runs. Operation
masks describe the most recent job. They are bounded state, not a historical log.
A plan is not a hardware transaction or lock: it expires after 15 seconds and
must be revalidated by `queue` when the operator confirms it.

For partial current, preview `Operation::GroupCurrent` with `partial=true`, show
missing members and unchanged original shares, and ask for explicit confirmation.
Pass `confirmed=true` only for that confirmed plan. The backend rejects stale
configuration/topology/recipients and all non-missing blockers. No UI should
rewrite membership or group totals to implement the override.

## Address discovery and eligibility

Discovery sends read-only INFO probes to 1–127 and listens to unsolicited
traffic. It neither writes to broadcast address 0 nor assigns/negotiates PSU
addresses. It retains the identity register as six opaque bytes; printed
barcode/model descriptions remain separate commissioning evidence.

Two matching identity samples at least 100 ms apart establish identity, with a
15-second lease. Duplicate identities at recent addresses block routing. The
registry recycles observations stale for more than 30 seconds, and exposes an
overflow counter if full. An identity unavailable on a particular firmware is a
blocking compatibility limitation, not permission to fall back to address-only
control. See [protocol sources](PARITY.md).

An apply recipient must be bound, assigned, unique, freshly identified, have
fresh telemetry and unsolicited current/status broadcasts, and report ready.
Freshness is `max(5000 ms, pollMs * 3)`. The observed ready byte is intentionally
a narrow check, not a complete fault decoder; raw `0x0183` data is exposed without
guessing that every nonzero value means a fault.

Read requests are spaced at least 25 ms apart, allowing a telemetry reply burst
to progress before the next request. Known devices get round-robin due INFO/DATA
requests; idle addresses are probed at most every 100 ms using remaining slots.
Due periodic reads take priority over manual reads, so repeated `poll all`
commands cannot starve identity leases or other members' telemetry.
This accommodates eight one-second DATA schedules plus three-second identity
refreshes. During applies the sweep pauses, but identity/telemetry refresh
continues. Poll intervals remain targets under actual bus load/faults.
Manual polls queue a mask and resolve one recipient per slot, rather than
sending the entire group in one call. `requestPoll`/`requestDescription` return
queue acceptance; later transport errors or vanished poll targets increment
`readFailures()` and produce an explicit serial error. Reads never set PSU values.

## Apply, identity guard, and recovery

At most one setting write awaits an ACK. Settings are separated by at least
250 ms. Before **every** setting, the controller queries the destination's INFO
identity and requires the expected identity/epoch and readiness before writing.
A 750 ms identity timeout sends no setting. A write then has a separate 750 ms
ACK timeout; an ACK must match the original address/identity epoch, register,
raw value, and recognized accepted/rejected status.

Global online/offline operations have two phases: voltage for all recipients,
then current for all recipients. Any voltage failure blocks the current phase.
The full voltage recipient set is checked again at the phase boundary; losing an
earlier recipient before the last voltage ACK also blocks current. Skipped
current phases retain an Incomplete per-unit result instead of reporting success.
Each member is attempted sequentially and results remain explicit; no rollback
is claimed. Group-current operations only send currents using the authorized
voltage and require recipient voltage synchronization plus a completed global
voltage commissioning. Thus a partly failed voltage change cannot be followed
by an apparently successful group current apply hiding the inconsistency.

Failure does not clear faults, redistribute current, or stop unaffected units.
Transport failures can have uncertain physical delivery. A new explicit apply
can retry after inspection. Accepted apply intent remains separate from success;
this distinction is also preserved when saving the operating profile.

A same-session authorized member whose identity/readiness is lost becomes
pending for recovery. A known returning identity must pass all eligibility
checks again. Recovery waits for the full configured installation to be ready;
all returning recipients acknowledge authorized voltage before authorized
currents are restored. Units authorized only for voltage receive no invented
current setting. The failed attempt is latched until an explicit apply or a real
loss/recovery transition. No offline defaults are written by recovery.

`configure(config,true)` is for boot: it arms saved operating settings only if
`autoResume` is enabled and deployment matches. Console load uses
`configure(config,false)`, which never replays settings. There is no automatic
EEPROM write when applying or reconnecting; persistence is explicit.

Identity guards narrow address-reuse races but cannot create atomic hardware
identity+write transactions. Very short outages, a reset without observable
readiness loss, duplicate/spoofed IDs, or unsupervised physical rewiring cannot
be fully resolved by this protocol. Correct commissioning, common PSU fallback
voltage, and any required physical interlocks remain installation concerns.

## I/O and UI isolation

The MCP2515 ISR only copies complete frames into a fixed ring (15 usable entries
by default). Decode, storage and UI run in the main loop. `rx-drops` is a
saturating overflow counter. The adapter owns TX buffer 0, clears stale TX
completion, and bounds transmit/abort handling to the configured 20 ms timeout.
It preserves RX flags and never overwrites a still-busy TX buffer. The library
continues to supply initialization and receive handling.

Transmission is cooperative: `send` starts one frame, `service(now)` checks it
once per loop, and `transmitState()` exposes pending/sent/failed. No main-loop
spin waits for arbitration or completion. The controller retains the purpose and
job identity of that send so a delayed transport failure cannot be attributed to
another operation. CAN processing runs first in `Application::tick`, followed
by one ready EEPROM step, serial, and the optional local UI. RXB0 rollover into
RXB1 is enabled. Hardware overflow flags are counted and cleared independently
of software-ring drops; these latched events are not an exact lost-frame count.

Serial drains at most 32 input bytes and transmits at most 48 buffered output
bytes per loop, limited further by `availableForWrite()`. A 384-byte output ring
and split report fragments avoid waiting for UART space. Input remains serviced
under output backpressure; one complete command can wait for response space,
and excess commands are explicitly discarded through EOL without executing a
suffix. Echo, backspace, CR/LF/CRLF, idle expiration, Ctrl-X/C reset,
Ctrl-U line clear, and prompt redraw remain available. Description control bytes
are sanitized. A console reset also cancels a pending serial confirmation, but
cannot cancel a submitted controller job. See [serial lifecycle](SERIAL.md).
The frame observer copies only relevant frames into a four-entry console trace
queue; it performs no printing. Trace overflow drops diagnostic entries and
warns the operator, leaving the controller's ACK/state processing intact. The
console reports trace drops separately from CAN receive drops. Custom Stream
adapters must implement `availableForWrite()`; the Mega HardwareSerial does.

The local UI is separated into `UiModel` (navigation, editing, confirmations and
operation ownership), `UiView` (320x240 layout), `UiFrame` (585-byte text/style
scene), and the injected `Display` interface. `Ili9341Display` keeps a second
585-byte scene cache and a 32-byte glyph raster. There is no full pixel buffer,
heap-allocated canvas, or String-based menu. A future display can replace the
renderer; a different resolution can also replace the layout without changing
navigation/backend policy. SSD1306 support and its dependency are removed.

The header averages fresh configured-unit output voltages and sums fresh current
and output power. Per-metric timestamps and an expiring freshness mask prevent
other replies from keeping an old measurement fresh, including across millis
rollover. `metric()` retains its last-value API; `freshMetric()` is the stricter
presentation hook. Partial sums carry `*`; no available readings show `--`.
Session Ah is accumulated in the controller for verified bound slots and survives
observation recycling and address changes. It resets for reboot/reset-ah or a
changed identity, removed slot or deployment, and is never stored in EEPROM.

Display initialization and the only full-screen clear run before CAN starts.
Runtime drawing uses at most four transactions per service call, each carrying
24 RGB565 pixels (two rows of a glyph); the CAN interrupt can run between SPI
transactions. Cached cells are committed only after their entire glyph has been
sent, even if the scene changes mid-glyph. Missing display ID disables local
controls while serial/CAN remain available. Optional PWM dimming consumes the
first wake interaction. Read-only builds cycle group pages automatically.
After composing a scene, `Display::invalidate()` schedules one comparison pass.
`FrameChanges` scans at most 32 cells per loop with a comparison-based cursor
wrap, and does no scanning once that pass is complete. Mid-glyph invalidation
retains a full new pass so the completed old glyph cannot hide a newer value.

`UiModel` owns the sequence of its operation and retains its result masks; another
serial job cannot be mistaken for its successful apply. Edits/confirmations are
invalidated by configuration revisions and confirmation topology/expiry checks.
A successful local apply and serial `save` snapshot EEPROM via `startSave`.
Application owns `stepSave`: at most one byte update per loop, only when the
EEPROM is ready, so neither path waits for a previous write. Both consumers
observe their completion token and verification result, even if another save
has since begun. The synchronous `save` helper remains for offline callers/tests.
Missing-member overrides
remain explicit and never redistribute load. Final apply/save errors are visible
even when the operator leaves the progress screen. See [local UI](LOCAL_UI.md).

## EEPROM schema 2 and migration

`ArduinoEeprom` wraps the standard `EEPROM.length/read/update` methods. Only
serial `save` or a successful local apply/save writes. Two 256-byte journal slots reserve bytes 0–511;
integer encoding is explicit little-endian and independent of struct padding.
At eight-slot capacity the record is **216 bytes**, whether one or eight slots
are active. Telemetry, observations, sessions, pending jobs and ACK history are
not persisted.

| Relative offset (8-slot build) | Contents |
| --- | --- |
| 0 | Commit marker `A5`, written last |
| 1 | Schema `2` |
| 2–3 | Record length |
| 4–7 | Generation, wrap-aware comparison |
| 8–9 | CRC-16/CCITT-FALSE excluding bytes 0, 8, 9 |
| 10–13 | Magic `R48C` |
| 14 | Operating voltage authorization flag |
| 15 | Per-slot voltage authorization mask |
| 16–19 | Deployment ID |
| 20–23 | Draft online/offline voltage |
| 24–28 | Poll interval, count, auto-resume, reserved zero |
| 29–132 | Eight groups: 8 name bytes, membership mask, online/offline totals |
| 133–196 | Eight slots: 6 identity bytes, rated current |
| 197–199 | Authorized voltage and current-valid mask |
| 200–215 | Eight authorized per-slot currents |

Save invalidates the older destination slot, writes payload/checksum, commits
last, then verifies. Load validates CRC, schema and semantic configuration,
selecting the newest complete record. A failed read leaves caller RAM unchanged,
except that WrongDeployment deliberately returns an inspectable, gated profile.
No fallback case silently erases EEPROM. Save is synchronous and disallowed by
the serial UI during an apply.

Schema 1 remains readable. Uniform enabled configurations migrate to unbound,
unauthorized GROUP1 in RAM, with auto-resume off. Incompatible per-unit drafts
return LegacyNeedsReview unchanged and remain available through `legacy()`.
The first schema-2 save writes the other journal slot; interrupted migration
saves still recover the old record. A later save may overwrite that legacy
backup, so export it if it is needed. All supplied builds use the same capacity;
changing `PSU_MAX_UNITS` changes the record length and requires manual export/
recommissioning. This firmware does not reinterpret a different-size layout.
