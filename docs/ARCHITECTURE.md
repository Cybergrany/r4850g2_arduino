# Firmware boundaries and UI contracts

`Application` composes adapters and optional UIs. Its loop services the
controller, storage, serial, then local display/input. Core configuration, protocol, discovery,
controller, and storage code are ordinary C++11 without Arduino dependencies.

| Directory | Responsibility |
| --- | --- |
| `src/config/` | Deployment template, board wiring, limits, configuration types and validation |
| `src/can/` | Injectable `CanTransport` plus MCP2515 adapter and bounded transmission |
| `src/protocol/` | Huawei frame classification, registers, numeric encoding |
| `src/psu/` | Discovered physical devices, stable slot bindings, group allocation, apply/recovery state machines |
| `src/storage/` | Versioned journal over `ByteStorage`; Arduino adapter uses standard EEPROM |
| `src/ui/` | Serial commands, diagnostic rendering, ILI9341 layout/renderer, wheel/touch navigation and calibration |
| `src/debug/` | Optional simulated PSU transport and volatile byte storage |
| `src/app/` | Startup composition and cooperative scheduling |
| `src/io/` | Optional CHG_ALLOW cutoff, four-wire fan PWM and alert-output adapters |
| `src/util/` | Literal/constant storage helpers for AVR flash and host builds |
| `test/` | Production code exercised with simulated hardware/time |

A future CAN adapter implements `CanTransport`; a future memory device implements
`ByteStorage`. Neither change requires rebuilding group logic inside the UI.
The MCP2515/arduino-CAN adapter currently serves one physical CAN interface.

## Persistent model versus observed hardware

There are up to nine identity-bound PSU slots and eight disjoint named groups.
Configured and authorized voltage/current are per PSU. Offline voltage and group
offline totals remain separate explicit defaults. A shared `SettingScope` and
`SettingFields` interface validates, previews and applies global/group/unit edits.
Global current sets the same total per group; group current replaces allocations
with deterministic equal shares. Group totals are derived from member targets.

`Configuration::operating` contains per-unit authorized V/A and validity masks;
configured drafts, thermally effective current and acknowledged values remain
separate. Authorization does not claim physical delivery. Latest accepted field
updates replace the affected members. Membership edits preserve individual drafts
and invalidate affected current authorization without redistributing live current.

OFF applies prepare targets for the next explicit ON and retain zero hold. Initial
release still requires the required members' voltage authorization and ACKs;
each target is compared with that PSU's authorized voltage. Identity changes,
thermal stops, availability and readiness guards remain in force. Profiles map
stable identities to configured targets; optional participation is a separate,
explicit OFF operation. [Operator semantics and EEPROM map](PROFILES.md).

`Discovery` holds twelve physical `Psu` observations at the default nine-slot
capacity, including three spare entries for unbound devices/address changes.
Builds below eight slots retain their earlier capacity plus four spare entries. A `Psu` contains runtime address, reported identity, identity
samples/lease, observation epoch, telemetry, raw alarm bits, and freshness.
Addresses and telemetry are never persisted. Lookups by identity reject ambiguity.
Address changes, identity changes, stale-to-live transitions and conflicts
invalidate epochs. Slots retain their config when count shrinks; existing group
membership must first be edited to permit shrinking.

Repeated slot queries share identity-to-discovery indexes and source identity/
lease validation for the same timestamp and discovery revision. Configuration,
time or revision changes clear the cache. Every associated incoming frame
refreshes source validation before ACK/readiness handling, including a source
becoming fresh again within the same millisecond. Missing, unbound and ambiguous
results use distinct sentinels. Telemetry values, per-field freshness, readiness,
authorization and queued-write permission are still checked from current state.
Discovery checks expiry/conflicts each tick, comparing each fresh identity pair once.

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
controller.set({ScopeKind::Group, 0}, SettingFields::Voltage, 5400);
controller.setCurrent(group, 5500);  // total 55 A, shares 27.50 A
controller.setAutoResume(true);     // live enable; first wait for held-at-zero-awake

const auto plan = controller.preview(psu::Operation::All, -1, now);
// Render every plan.issues[i], its recipients, voltage[i] and current[i].
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
| `configuration()` | Per-unit drafts, deployment, groups, bindings, ratings, authorized targets |
| `setGroup`, `removeGroup`, `setCount`, `bind`, `unbind` | Commissioning/membership; binding starts background reconciliation; removal does not stop retired hardware |
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
This accommodates nine one-second DATA schedules plus three-second identity
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

Combined scoped online/offline operations have two phases: voltage for all recipients,
then current for all recipients. Any voltage failure blocks the current phase.
The full voltage recipient set is checked again at the phase boundary; losing an
earlier recipient before the last voltage ACK also blocks current. Skipped
current phases retain an Incomplete per-unit result instead of reporting success.
Each member is attempted sequentially and results remain explicit; no rollback
is claimed. Group-current operations only send currents using the authorized
voltage and require each recipient's authorized voltage and matching
voltage acknowledgement. Thus a partly failed voltage change cannot be followed
by an apparently successful group current apply hiding the inconsistency.

Failure does not clear faults, redistribute current, or stop unaffected units.
Transport failures can have uncertain physical delivery. A new explicit apply
can retry after inspection. Accepted apply intent remains separate from success;
this distinction is also preserved when saving the operating profile.

A same-session authorized member whose identity/readiness is lost becomes
pending for recovery. A known returning identity must pass all eligibility
checks again. Recovery waits for required participants to be ready; accepted
absences are excluded and their returns need explicit inclusion. For eligible returns,
all returning recipients acknowledge authorized voltage before authorized
currents are restored. Units authorized only for voltage receive no invented
current setting (reconciliation still explicitly prepares zero current). The failed attempt is latched until an explicit apply or a real
loss/recovery transition. No offline defaults are written by recovery.

`configure(config,true)` is for boot: it arms saved operating settings only if
`autoResume` is enabled and deployment matches. Console load uses
`configure(config,false)`, which holds the managed units through zero-current reconciliation
without replaying operating current. There is no automatic
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
queue; it performs no printing. The core annotates successful routine refresh
ACKs before completion clears their origin; serial omits them unless raw tracing
is enabled. The annotation is a callback argument, with no retained per-frame or
per-unit storage. Rejected/unmatched replies and protective corrections retain
their usual visibility. Trace overflow drops diagnostic entries and
warns the operator, leaving the controller's ACK/state processing intact. The
console reports trace drops separately from CAN receive drops. Custom Stream
adapters must implement `availableForWrite()`; the Mega HardwareSerial does.
Unsolicited control/availability status is sampled every 100 ms instead of
recomputing all members on every idle loop. Commands, ACK processing, alert
broadcasts and the protective state machines retain their existing scheduling.

The local UI is separated into `UiModel` (navigation, editing, confirmations and
operation ownership), `UiView` (320x240 layout), `UiFrame` (565-byte text/colour/outline
scene), and the injected `Display` interface. `Ili9341Display` keeps a second
565-byte scene cache, a 16-byte glyph raster and 16 bytes of changed-column masks. There is no full pixel buffer,
heap-allocated canvas, or String-based menu. A future display can replace the
renderer; a different resolution can also replace the layout without changing
navigation/backend policy. SSD1306 support and its dependency are removed.

The header shows a common voltage or explicit V:MIX and sums fresh current
and output power. Per-metric timestamps and an expiring freshness mask prevent
other replies from keeping an old measurement fresh, including across millis
rollover. `metric()` retains its last-value API; `freshMetric()` is the stricter
presentation hook. Partial sums carry `*`; no available readings show `--`.
Session Ah is accumulated in the controller for verified bound slots and survives
observation recycling and address changes. It resets for reboot/reset-ah or a
changed identity, removed slot or deployment, and is never stored in EEPROM.

Display initialization and the only full-screen clear run before CAN starts.
Runtime drawing retains 8 MHz with a separate 1 MHz read cap. All selection
indicators use two-pixel cyan outlines; unfocused controls have dim outlines.
`UiFrame` stores nine grid rectangles in three bytes each. Three-bit text colours
pay for that list: each scene is 565 bytes, smaller than the former 585-byte
text/style scene. There is no additional full-scene or pixel framebuffer.

The outline renderer updates edges directly in at most 48-pixel transactions at
8 MHz, 24 at 4 MHz, and 12 below. Moving focus recolours the old and new edges
without changing the card/button text or interiors. Focus wins at shared edges.
Outlines extend into the surrounding gutters; every glyph retains its original
foreground colour and a one-pixel dark clearance wherever it meets an edge.

The painted cache tracks each completed outline. An in-flight edge operation
finishes its captured geometry before switching to a newer scene; it never
paints untracked rectangles from transient input. A changed layout erases old
edges, updates text, then draws the new outlines. A focus-only change prioritizes
edges and can skip the text scan using a bounded packed-scene comparison. A
partial glyph always finishes before an outline operation can use its text.
These rules cover rapid reversals, overlapping controls and page replacement.

Text still uses visible row/column comparisons and ignores empty foreground changes.
Each text strip is cropped to its changed horizontal span; the 2x/4x fonts
share identical row pairs, making room for column masks without more static RAM.
Unchanged interiors keep the normal fast glyph path; only cells touching a
painted outline need edge compositing. Pixels are prepared before SPI opens,
and every transaction closes before yielding. Uniform strips use the driver's
solid-colour writer. The 750 us cooperative budget and eight-transfer ceiling
above 2 MHz (two below) remain. One bounded work unit or interrupt can overrun;
`diag ui` reports the observed maximum. A prepared strip makes progress even
if preparation consumed the budget.

A cell enters the painted cache only after its required strips complete.
Invalidation during drawing schedules another comparison; completed scenes need
no idle scanning. The production renderer is checked against an independent
full-pixel reference and recorded SPI boundaries at all five clocks. Missing
display ID disables local controls while serial/CAN remain available.

Foreground navigation, initial pages and notifications can use an additional
**8 ms cooperative drawing window**, capped at twelve extra service calls.
Between every slice, `PsuController::serviceReceive` processes at most four
queued frames, expires discovery state and runs temperature protection when
frames arrived. It sends no requests or settings; normal `tick` retains command
scheduling and its deadlines. ACKs, identity changes and readiness observations
use the usual receive handlers. CHG_ALLOW and an OFF PSU_ACTIVE request reach
GPIOs during this window. RX interrupts remain enabled between the normal short
SPI transactions; no CAN frames are deliberately discarded.

The time limit is checked between bounded work units, so RX/protection work or
interrupts can overrun it. This is a rendering-priority window, not a CAN blackout
or hard real-time guarantee. Periodic telemetry keeps one render service per
main loop, and composition waits for an existing periodic paint to finish.
The UI phase maximum includes the CAN/protection work performed during drawing.

Text composition fills contiguous character/style ranges, preserving neighbouring packed colours and truncation. The group-current maximum is calculated exactly from
each member's ceiling and allocation rank (`ceiling * member_count + rank`),
avoiding a binary search on every configuration-screen composition. It retains
the same allocation rounding and limits used by `checkCurrent`.

Nonperiodic invalidations (input, navigation, alerts/results) retain separate
completed-change timing so a later telemetry-only comparison cannot overwrite
it. Timing starts after composition and includes the gaps between render calls;
it does not measure the entire input-to-pixel delay. Application also records
maximum control/I/O, storage/protection, serial and UI phase durations when both
serial and display are built. `diag ui reset` clears these timing maxima without
resetting CAN counters or interrupting drawing. Durations include interrupts.

`UiRecovery` resolves both incidents and operation results to current backend
state. A single bounded bookmark retains the interrupted editor/page and alert.
Recovery opening is read-only. Its separate confirmation captures configuration,
topology, availability, operation sequence, incident ID and expiry; the backend
rechecks its usual guards on execution. Global retry, explicit ON, sensor retry
and journal save reuse existing controller/storage APIs. A save owns its token
until verified completion; another operation cannot claim that result. Ordinary
notifications wait while the operator uses recovery; the live registry/GPIOs
continue updating. Identity binding and group creation remain serial operations.

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

## Startup and availability backend

`PsuAvailability.cpp` owns the AC permit, group observation windows and explicit
accept/exclude/include/require/probe decisions. `AvailabilityPlan` captures config,
topology and availability revisions, selected identities by stable slot and the
resulting participant mask. `previewParticipation` validates the complete desired selection. Serial aliases
adapt that shared plan; UI and profiles use it directly. `confirmAvailability`
revalidates before action; no presentation layer implements its own waiver policy.
`availabilityStatus` reports group readiness/review/settling; `participationStatus`
distinguishes runtime intent from remembered and legacy policy.
`startupStatus`, `availabilityReason`, `availabilitySettled`, `availabilityUnstable`,
`participants`, `excluded` and `availabilityReport` expose the same facts to
future renderers. `ChargeAllowOutput` is the only physical permit writer.

Remembered choices use the shared incremental `MemoryManager`. Successful
verification is followed by revalidation after this loop's CAN receive/control
processing. A changed snapshot can be persisted without activating its decision;
the result distinguishes these states. Session acceptances and inclusions remain
RAM-only in the existing session masks; normal configuration serialization never
includes them. Explicit remembered intent updates `disabled` and clears affected
legacy `acceptedAbsent` bits in the private writer snapshot, then in live config
only after verification. Profiles patch that same snapshot in their one final save,
then activate participation without releasing a new thermal contribution. EEPROM
schema, record sizes, calibration locations and profile format are unchanged.
Binding/membership changes invalidate affected exceptions. All current
allocations still use full configured membership, never participant count.

`PsuControl` separates wake readiness from stopping deadlines, reasserts the
zero/fan/voltage baseline after readiness, and refreshes volatile V/I through
the paced identity/ACK worker. Refreshes do not overwrite user operation reports
or repeat persistent defaults/fan writes. AC-loss transitions and negotiation
invalidate stale routing/setting proofs. [Operator sequence and limits](STARTUP.md).

## EEPROM schema 6 and migration

`ArduinoEeprom` uses standard Arduino EEPROM access. The Mega's 4096-byte map is
specified in [PROFILES.md](PROFILES.md). Controller records use two 512-byte slots
at 512/1024; eight profiles use two 128-byte copies each from 1536. Calibration
retains its 40-byte records at 216 and 472. The default nine-unit controller record
is 247 bytes, encoded explicitly in little-endian order.

| Controller offset | Contents |
| --- | --- |
| 0–13 | Commit marker, schema 6, length, generation, CRC, R48C magic |
| 14–19 | Encoded capacity, configured count, deployment ID |
| 20–25 | Offline voltage, poll interval, auto-resume/thermal flags, reserved |
| 26–33 | Absence permission, authorized V/A masks, explicit disabled mask |
| 34–97 | Eight groups: six-byte packed name and offline total |
| 98–241 | Nine units: identity, rating, configured V/A, authorized V/A |
| 242–246 | Four-bit group index per slot; zero unassigned |

Profiles have R48P magic, version/generation/CRC, deployment, identity count,
12-byte name and nine ten-byte identity/V/A entries. Offsets 122–123 hold optional
participation in stored identity order, and byte 124 says whether it was captured.
Profiles contain no enable preference, thermal marker or saved absence permission.

One cooperative writer snapshots once, writes commit last and verifies readback.
Protection can cancel an uncommitted profile write; completion tokens distinguish
its cancellation from a later protection save. Ordinary configuration and
calibration retain the shared writer gate. Read scratch is reused only while idle.
`load()` preserves caller data on failure; `loadCandidate()` uses a caller-owned
temporary to validate candidates without a nested configuration copy. Its failure
may modify that temporary. Only accepted results reach controller configuration.
Save-only absence overrides alter the private serialized snapshot, not live RAM.

Schemas 2–5 expand shared online voltage and group totals into per-unit values,
preserving identities, authorization, permissions and thermal stops. Schema 1
retains explicit review for incompatible settings. The current count is preserved;
extra capacity stays unbound. After verification of a new controller record, both
legacy commit markers at 0/256 are retired. An interrupted migration retains a
complete valid old/new record. Calibration bytes are never retired or overwritten.

Older firmware cannot read schema 6. Export and recommission deliberately before
downgrading; firmware downgrade is not a supported way to clear protection.

## Touch and simulation boundaries

`UiAction` expresses semantic navigation/edit actions shared by the wheel and
XPT2046. `UiLayout` supplies the same cell rectangles to rendering and hit tests.
The full-screen editor uses four cached character cells per enlarged digit;
`Ili9341Display` clips a scaled flash-font glyph into each cell. RAM scene size
is unchanged; pixel strips remain bounded. `Display::pending()` gates newly drawn
screens; `TouchGesture` captures a target, cancels dragging and consumes input
across page changes or simultaneous wheel events.

`Touch.cpp` contains hardware-independent mapping, five-point calibration,
gesture handling and the separate two-record calibration journal. `Xpt2046Input`
samples a fixed SPI burst in the main loop. `TouchConfig.h` owns wiring/timings.
`MemoryManager::auxiliaryBusy` gates the one shared byte writer; application
services both storage jobs, but at most one is active. Charger records occupy their separate schema-6 slots at 512/1024; calibration
retains its original locations. Calibration has its own version/CRC,
sequence, hardware profile and rotation, independent of deployment identity.

With `PSU_UI_DEBUG`, Application instantiates `SimulatedCan` and `RamStorage`.
No hardware CAN or Arduino EEPROM adapter is compiled into these builds. The
simulator synthesizes bounded INFO/DATA/current/ACK traffic for the actual
controller and discovery state machines. It stores operating V/A, standby/fan bits and a
single pending response; no large receive history or heap is needed. Scenario
selection resets the simulated controller only after pending work finishes,
primes healthy identities/operating settings, then introduces the selected fault.
The UI and console hold stable controller references across this in-place reset;
UI navigation and console observers are reset together. Calibration writes and
ordinary settings use the same RAM byte device in debug builds.

See `docs/TOUCH.md` for commands, timing, storage and reset semantics. A SIM mark
is permanent on all debug screens, and hardware diagnostics are explicitly
labelled simulated. `tools/check_debug_build.py` verifies linked isolation.

## Reconciliation and enable ownership

`PsuControl.cpp` owns per-unit reconciliation, fan hysteresis, and a single
identity-checked control transaction. It shares the controller's cooperative TX
slot and 250 ms write spacing with ordinary profile jobs. A pending stop takes
priority; telemetry and identity requests continue while commands or fresh
post-ACK observations are awaited. Control failures are distinct from user job
results and remain visible through `unitControl(slot)`.

`EnableControl` resolves the boot OR and later debounced switch edges/serial
commands. `setAutoResume` is deliberately allowed during an apply so OFF can
preempt it. `sampleEnableInput` accepts logical active state; Application alone
reads the configured GPIO, and never in debug builds. Enable events increment
the configuration revision, invalidating pending UI edits/confirmations.

`controlStatus(now)` exposes global activity and the blocking slot/issue;
`enableControl()` exposes effective request/source and raw/debounced input.
`psuActive(now)` exposes whether any participant has an authorized positive
effective CAN limit and has crossed the release barrier without a local control,
identity, freshness or restore block. It includes readiness qualification and
does not depend on measured current or every peer being healthy. The stateless
`io/PsuActiveOutput` adapter is sampled after controller work and again after
serial/UI actions, so an OFF in either path lowers the output in that loop.
Debug builds expose the logical query without physical GPIO writes.
`reconcile()` explicitly retries from zero current. Normal OFF preserves awake
state; hardware standby is reserved for an unconfirmed-stop fault fallback. No new persistent fields or
EEPROM schema are added: save snapshots the existing autoResume preference, and
fan thresholds/pins live in `ControlConfig.h`. See [CONTROL.md](CONTROL.md) for
ordering, exact thresholds, failure handling and hardware limits.

Readiness-loss counters occupy one byte per configured-capacity slot, separately
from `UnitControl` so ordinary route/phase resets cannot renew them. Configuration
changes map those counters by stable identity using a nine-byte local snapshot;
profiles or temporary CAN addresses do not own them. Internal startup/probe
reconciliation calls `reconcile(false)` and preserves an exhausted budget and its
fault. Operator `reconcile()` / guarded UI Retry ALL clears the session counters.
The third qualified-output loss exceeds the default two-restart budget and uses
the existing fault-stop path, alert source and recovery controls.

Fan hysteresis and accepted mode survive software reconciliation and enable
events; identity/connection changes invalidate them. Cooling is evaluated in
every control phase. `externalFanStatus(now)` folds per-unit policy and the
hottest fresh output temperature into one logical PWM demand, using the small
fixed-point curve in `FanPolicy.h`. Missing telemetry requests full duty.
`io/FanOutput` owns the optional pin, dedicated Timer4/5 25 kHz PWM, polarity,
100 ms policy cadence and changed-duty compare writes. It adds no ISR and leaves
Timer0/time and Timer1/wheel untouched. Application initializes it before display/CAN setup; debug builds
exclude physical fan access. Neither adapter nor policy modifies EEPROM.

Wake-at-zero is a per-unit preparation step independent of effective enable.
`wakeAllowed(slot,now)` requires that unit's verified route, zero observation and
fan ACK before clearing inherited standby. `releaseAllowed(now)` separately
requires complete installation readiness, awake/zero observations and common
voltage before any held unit becomes Online. Every zero stop observes new
current after its ACK; every initial wake also observes a new ready broadcast.
`UnitControl::zeroConfirmed`, `awakeConfirmed` and `standbyConfirmed` distinguish
normal holding, wake verification and exceptional hardware fallback.

`setFanOverride(mask, mode, now)` validates the entire slot selection using
`fanOverrideIssue` before changing any member. `UnitControl::fanOverride` is
volatile; effective `fanRequested` uses the existing identity/ACK/retry worker.
Manual AUTO is restricted to confirmed awake zero hold while disabled, and is
cleared before enable can release a high-current profile. Removing an override
returns conservatively to MAX and normal cooldown. Configuration/identity resets
cannot transfer an override to a replacement. Serial exposes the override and
ACK separately; future UI controls can use the same backend API.

## Shared alerts and thermal limits

`PsuThermal.cpp` derives effective online current from the authorized per-unit
allocation and the strongest thermal level. `PsuAlerts.cpp` maps controller,
availability and storage state to fixed incidents in `AlertManager`. Serial,
`UiAlerts`/`UiAlertView` and `AlertOutputs` consume that same state.
Acknowledgement never changes the protective condition. See [ALERTS.md](ALERTS.md)
for policy, incident lifetimes, persistence, exclusions and the bench procedure.

Protection updates run before actuation scheduling. They yield to bounded in-flight
ACK waits, supersede unsent values, and use the same INFO/identity/epoch checks as
other writes. `currentAccepted` records raw ACKs separately from synchronization
against the current effective target. Sensor-loss latches and prior hot
contributions survive ordinary route/reconciliation resets. Explicit binding
changes reset slot incident state.

`serviceProtection` shares the MemoryManager writer with reviewed saves and
calibration; it edits saved protection state without snapshotting RAM drafts.
The application services it before/after the cooperative EEPROM step and checks
completion tokens. A stale clear-save cannot enable after a newer OFF/hot event.
The AVR build disables SRAM switch-table conversion to preserve static headroom;
explanatory text remains in flash.
