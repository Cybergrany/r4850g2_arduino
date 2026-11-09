# Validation

Target: Arduino Mega 2560, eight-slot capacity. Validation covers the parallel
backend, ILI9341/touch UI, serial, cooperative scheduling, persistence and isolated
simulation. No firmware upload or physical validation of this patch was performed.

## UI formatting and recovery (2026-11-09)

- Shared decimal formatter regression: leading/trailing zeros, rounding, negative
  temperatures, engineering-unit fallback, narrow fields and fullscreen digits.
  `tools/check_avr_formats.py` rejects unsupported AVR printf conversions in
  authored production sources and runs automatically with the host suite.
- The production `Ili9341Display.cpp` is compiled against a recording display/SPI
  surface at 500 kHz, 1, 2, 4 and 8 MHz. Tests compare every final pixel with an
  independent classic-font raster, including large digits, blank styles, rapid
  reversals and scene changes at transaction boundaries. They check transfer
  size/count bounds, transaction closure, idle behaviour, microsecond/millisecond
  rollover and observable cooperative-budget overruns.
- The synthetic three-row button highlight needs **28 transfers at 8 MHz**, versus
  **1,248** for the previous renderer redrawing all 78 cells as one-row strips.
  This is a transfer-count result, not measured Mega latency. Simulated wire timing excludes AVR raster/ISR costs.
- Controller/UI host suite: **1,108,327 checks** with snapshot exports. Tests
  cover Config/Ack independence, affected-group routing, no-PSU operation,
  returned units, suspended edits, confirmed global retry,
  stale incident/configuration/binding/expiry reviews, newer OFF requests,
  journal completion ownership/failure/newer drafts and thermal/sensor guards.
  Touch/simulator integration: **1,086 checks** with snapshot exports, exercising
  the actual button hit targets, availability return/Ack, cancelled save review
  and verified asynchronous recovery save.
- Existing real ClickEncoder, controller/EEPROM interruption and touch/calibration
  regressions remain enabled. All nineteen Mega profiles and all three debug
  isolation checks pass; current sizes are below.

The wheel/alert build also includes remote commit `38673a9`, enabling the D26
ENABLE_IN switch; that combined profile was rebuilt after integration.

Use the [bench comparison](#ui-redraw-bench-comparison) before claiming hardware
latency, loaded CAN behaviour or PSU response.

## Wheel acknowledgement repair (2026-11-09)

The bundled encoder fixes a reproduced missing-short-click failure. Validation:

- Real driver/adapter regression: 19,652 checks; the unpatched driver failed
  the first short-click assertion. GPIO samples and timer callbacks are simulated.
- Controller/UI suite: 1,102,576 checks, including acknowledgement of the startup
  missing-members popup without changing the charging/absence decisions.
- Touch/simulator integration: 1,023 checks (without snapshot exports).
- Eight Mega builds passed: `mega2560`, `mega2560_local`,
  `mega2560_touch_wheel`, `mega2560_wheel_alerts`, `mega2560_serial`,
  `mega2560_ui_debug`, `mega2560_touch_debug`, `mega2560_touch_debug_local`.
- All three debug-isolation checks passed; encoder-disabled builds exclude
  ClickEncoder. No additional static RAM: `mega2560_wheel_alerts` uses 6,738
  bytes RAM and 144,680 bytes flash. The touch/wheel build retains 1,224 bytes
  of static RAM headroom. These builds retain the configured 8 MHz drawing clock.

On the bench, release the wheel button and short-press/release **Acknowledge**.
The overview should return while the missing-member warning remains active.
Then check click-to-open and hold-to-return navigation. No PSU absence should
be accepted and no current enabled by acknowledging the notification.

The build matrix and size table below include the UI formatting/recovery patch.

```sh
python3 tools/test_host.py
python3 tools/test_touch.py
python3 tools/test_renderer.py
python3 tools/check_avr_formats.py
python3 tools/test_fan_output.py
python3 tools/test_charge_allow.py
python3 tools/test_alert_outputs.py
python3 tools/test_wheel_alert_guard.py
PLATFORMIO_CORE_DIR=/home/dave/psu-can-workspace/.platformio pio run \
  -e mega2560 -e mega2560_wheel_alerts -e mega2560_serial -e mega2560_display \
  -e mega2560_local -e mega2560_minimal \
  -e mega2560_touch -e mega2560_touch_wheel -e mega2560_touch_local \
  -e mega2560_ui_debug -e mega2560_touch_debug -e mega2560_touch_debug_local \
  -e mega2560_touch_enable -e mega2560_touch_fan -e mega2560_serial_fan \
  -e mega2560_serial_cutoff -e mega2560_touch_cutoff \
  -e mega2560_serial_alerts -e mega2560_touch_alerts -j 4
python3 tools/check_debug_build.py
git diff --check
```

The absolute cache path isolates this workspace's PlatformIO packages; elsewhere
omit it or choose another writable cache. Pass the same cache to
`check_debug_build.py --core-dir PATH` when using a different location.
All nineteen profiles build successfully
with the pinned Atmel AVR platform 5.3.0, Arduino AVR framework 5.4.0, AVR GCC
7.3.0, CAN 0.3.1, Adafruit ILI9341 1.6.3, Adafruit GFX 1.12.6,
Adafruit BusIO 1.17.4 and TimerOne 1.2.0. ClickEncoder d6d5738fdf is now
[bundled with a single-click fix](../src/vendor/ClickEncoder/README.md).
A clean build retains ClickEncoder's existing constructor member-order warnings.

| Profile | Static RAM (bytes / 8192) | Flash (bytes / 253952) | Static RAM headroom |
| --- | ---: | ---: | ---: |
| `mega2560` | 6771 | 158550 | 1421 |
| `mega2560_serial` | 4997 | 100220 | 3195 |
| `mega2560_display` | 6740 | 149764 | 1452 |
| `mega2560_local` | 5828 | 115892 | 2364 |
| `mega2560_minimal` | 4054 | 46320 | 4138 |
| `mega2560_touch` | 6973 | 168100 | 1219 |
| `mega2560_touch_wheel` | 7004 | 169730 | 1188 |
| `mega2560_ui_debug` | 6780 | 161124 | 1412 |
| `mega2560_touch_debug` | 6982 | 170670 | 1210 |
| `mega2560_touch_local` | 6030 | 122322 | 2162 |
| `mega2560_touch_debug_local` | 6039 | 122570 | 2153 |
| `mega2560_touch_enable` | 6973 | 168410 | 1219 |
| `mega2560_touch_fan` | 6978 | 168832 | 1214 |
| `mega2560_serial_fan` | 5002 | 100640 | 3190 |
| `mega2560_serial_cutoff` | 4998 | 100292 | 3194 |
| `mega2560_touch_cutoff` | 6979 | 168882 | 1213 |
| `mega2560_serial_alerts` | 4999 | 100454 | 3193 |
| `mega2560_touch_alerts` | 6981 | 169134 | 1211 |
| `mega2560_wheel_alerts` | 6774 | 159226 | 1418 |

Compared with the immediately preceding commit `f26cd5c`, display profiles add
**36 bytes** of total static RAM; headless profiles do not grow. This meets the
128-byte growth target. The largest profile uses **7004 bytes**, leaving **1188
bytes** (more than 1 KiB) for the stack and runtime state. The wheel/alert bench
profile leaves **1418 bytes**. Replacing the GFX-derived scratch glyph with a
small direct classic-font raster offsets most of the recovery bookmark and
render-statistics state. No scene buffer was added.

Static RAM excludes stack/interrupt nesting. No full colour framebuffer is used;
two text/style scenes occupy 1170 bytes. Worst-case stack and loaded display/CAN
timing still require a hardware check. EEPROM remains within 512 bytes: two
256-byte slots with **216-byte schema-4 records**. Calibration still uses the
40-byte tails at 216 and 472. Schemas 2/3 remain readable; older firmware cannot
enforce schema-4 thermal stops. Review the [migration/downgrade limits](ALERTS.md#persistence-migration-and-limits).

All three debug ELFs passed `avr-nm` checks: simulated CAN and RAM adapters are
present; physical CAN/MCP2515, EEPROM, cutoff, fan PWM, alert and PSU_ACTIVE GPIO symbols are absent. Their
dependency graphs also omit CAN. This verifies software isolation, not touch
hardware or production CAN timing.

The `mega2560_wheel_alerts` ELF includes the wheel service and physical MCP2515
driver, without the physical XPT2046 driver. At the retained 8 MHz clock a maximum
four-row strip carries 48 pixels plus 11 address/command bytes: **107 us of wire
time**, excluding driver/CPU/interrupt overhead. At 4 MHz/two rows this is 118 us;
at 2 MHz/one row, 140 us; at 1 MHz, 280 us; at 500 kHz, 560 us. SPI is released
between strips. A 750 us budget is checked between bounded work units; a work
unit or interrupt can overrun it. `diag ui` exposes actual observed maxima.
These wire calculations and synthetic tests do not prove hardware timing.
Keep unused T_CS HIGH at 3.3 V if its SPI lines remain connected. CAN bitrate and
scheduling, EEPROM schema and the 1 MHz display read cap are unchanged.

## UI redraw bench comparison

Use the same Mega, level shifting, 8 MHz draw clock, PSU count, normal polling and
serial settings for each run. Record the baseline's highlight latency with a
video if it predates `diag ui`. The patch's timing counters start at boot; full
page changes can dominate `scene-max-ms`, so record `scene-ms` while repeating
an ordinary menu highlight as well as the overall maximum.

1. With the usual commissioned setup and CAN traffic running, send:
   ```text
   watch off
   raw off
   diag ui
   diag bus
   ```
2. Turn between Tools menu choices for 30 seconds, including quick reversals.
   Check that filled highlights move cleanly. Also exercise group-page changes,
   PSU details, and editing `54.10 V` / fractional current without applying unless
   intended. Use touch release/press and wheel clicks after each navigation.
3. While repeating highlights, sample `diag ui` and `diag bus`. Record a second
   bus snapshot after the run. Compare **counter increases**, especially
   `rx-drops`, `hw-overflow-events`, `tx-errors`, `loop-max-us` and command results.
   Trace drops alone are diagnostic-output congestion, not CAN receive loss.
4. Repeat the same sequence on the patched firmware. Aim for ordinary menu
   highlight completion **below 100 ms at 8 MHz**. Large filled group tiles and
   full-page transitions move more pixels; measure those separately. A phone
   video can corroborate input-to-highlight time, which `scene-ms` measures only
   from composition to completed drawing. Check that telemetry stays fresh and
   the established bench apply/stop cycle has no command timeouts.
5. With PSUs absent, open the warning's Config shortcut: choose Wait and return
   to the same incident; Ack must leave WARNING active and accept no absences.
   Test Continue without only through its separate review/confirmation. In SIM,
   exercise failed saves, thermal stops, sensor retries and calibration; they
   must retain their backend guards and must not affect physical CAN/GPIOs.

| Measurement | Baseline | Patched |
| --- | --- | --- |
| Ordinary highlight latency, ms | bench measurement | bench measurement |
| Latest/max completed scene, ms | if available | `diag ui` |
| Maximum render-call / SPI transaction, us | if available | `diag ui` |
| RX drops / hardware overflows added | `diag bus` delta | `diag bus` delta |
| TX errors / command timeouts added | bus/status results | bus/status results |

A clean host run and build do not replace this loaded bench comparison.

## Host coverage

The wheel regression now runs the actual bundled ClickEncoder and `WheelInput`
against GPIO samples and its registered 1 ms timer callback. The original driver
fails the first short-click assertion; the corrected driver must also pass
repeated clicks, held-button suppression, rotation, queued-click cancellation,
release bounce and the alert guard across timer rollover. The UI model separately
checks that a wheel click dismisses a missing-members popup without accepting
absences, enabling output, clearing the active warning or starting a save.
These checks do not emulate AVR interrupt timing or establish hardware response.

The tests compile production configuration, protocol, discovery, controller,
EEPROM, serial parser, and MCP2515 transmit code with simulated hardware/time.
Compiler flags include `-Wall -Wextra -Werror` and AddressSanitizer/
UndefinedBehaviorSanitizer. LeakSanitizer is disabled for the ptrace sandbox;
address and undefined-behaviour checks remain enabled.

Both suites pass: **1102539 host checks** and **1054 touch/simulator checks** with
UI snapshots exported (counts include export assertions). UI navigation, formatting and persistence use the production
model/layout code; the host suite does not simulate the actual ILI9341, SPI
interrupt masking or ClickEncoder hardware. Its assertion total is dominated by checking every EEPROM
access and interruption boundary, not hundreds of thousands of independent cases.
Meaningful scenarios include:

- PSU_ACTIVE follows effective enable and any released positive allocation:
  boot/no profile/zero drafts, staged versus applied zero, OFF before stop ACK,
  switch/serial precedence, no-load operation, readiness ACK pending/timeout,
  stale telemetry, sensor latch/retry, thermal reduction/shutdown, identity/AC/CAN
  loss, rejected current commands, latched AC cutoff, accepted absence and return
  before/after inclusion. Healthy peers retain HIGH. GPIO tests cover LOW-before-
  OUTPUT, all eight independent warning/error/active combinations, disabled pins,
  pin collisions and no physical output in debug. `diag control` uses the same
  backend query. This output adds no controller state or persistence fields.
- Exact telemetry/INFO/setting IDs and payloads; malformed frames; rated-current
  conversion, signed temperatures, efficiency, available current, and Ah.
- Identity corroboration, invalid identity, collisions, stale addresses,
  address relocation, unbound devices, registry capacity, and ready/not-ready.
- Disjoint group membership, equal shares and rounding, configured capacity
  rejection without clamping, and staged-versus-authorized voltage/current.
- Two- and eight-unit applies; global voltage before current; explicit offline
  defaults; per-write identity probe timeout and replacement detection.
- Wrong register/address/value ACKs, rejection, transport failure, timeout,
  partial physical success, loss between voltage ACKs and current phase, recovery
  after a loss during an apply, and retained error states across other group jobs.
- Missing-member block without writes; explicit partial confirmation; original
  shares; confirmation expiry and config/topology changes; unoverrideable faults.
- Known-identity recovery to a different address using authorized settings,
  never later drafts; boot auto-resume off/on; load-only staging; wrong deployment;
  membership changes never triggering automatic redistribution; rebinding and
  saving never granting the replacement the old unit's boot authorization.
- Independent identity/data scheduling at long poll intervals; wrap-safe
  freshness; no repeated fault-clearing writes while readiness remains unchanged.
- Every EEPROM update interruption on initial and third save, and first schema-2
  migration save; fallback from corrupted latest record; readback failure;
  wrong deployment; invalid config; too-small storage; all accesses within 512 bytes.
- All eight groups/identities/ratings, draft and operating values round-trip;
  uniform legacy migration with identity/auto-resume disabled; mixed legacy
  preservation and inspection without silent rewriting.
- Serial group commissioning and commands; decoded diagnostics; malformed
  numbers; capacity explanations; partial-apply warning, confirmation and Ctrl-X
  cancellation; current/OVP ACKs and final E-Label fragment.
- Character echo/backspace/DEL, CRLF split across ticks, tabs, overflow draining,
  reconnect without output observation, idle timeout before consuming new bytes,
  timeout rollover, raw-output prompt redraw, and active jobs surviving console reset.
- Three-group and two-charger paging; empty/deleted groups; returning to the
  selected PSU; edit cancellation and backend clamping; exact mixed-rating group
  capacity including the centiamp remainder.
- Initial voltage commissioning before group current; UI apply/ACK/save ordering;
  runtime ACKs versus saved requests after reboot; independent serial changes
  invalidating an edit; operation ownership and missing-member No/Yes/expiry.
- Missing-member partial applies retain shares and warn with member numbers;
  voltage and not-ready members have no override; CAN apply failure does not save;
  EEPROM failure is distinguished from accepted live settings; changes made during
  a save remain unsaved and the UI reports the earlier snapshot.
- Incremental EEPROM writes alter at most one byte per step, retain the initial
  snapshot, reject overlapping saves, and pass the existing interruption coverage.
- Per-field freshness despite continuing unrelated replies; partial/missing sums,
  signed temperature maxima and millis rollover; volatile session Ah retained
  across absence/address relocation and reset for a replacement identity.
- Actual layout snapshots for group pages, aligned charger values including W,
  green OK/error detail, group configuration and partial confirmation. Numeric
  formatting preserves units, uses k/M where needed and handles invalid readings.
- MCP2515 success/arbitration/errors/abort/no completion/stuck TXREQ, bounded
  timeout, rollover, preserved RX flags, and invalid frame rejection.
- Cooperative TX checks return while pending, preserve telemetry processing,
  prevent overlapping transmissions and attribute later failures to the correct
  identity/setting job. Queued read failures are reported to the operator.
- Eight-unit INFO/DATA scheduling while scanning, paced manual polls, vanished
  queued targets, and identity refresh under continuously requested manual polls.
- Stalled/three-byte UART capacity never blocks CAN processing or overfills the
  simulated UART. Console trace loss is reported separately; startup and reports
  fit the output buffer. Backpressured input, extra pasted lines, split suffixes,
  Ctrl-X cancellation and CRLF are exercised without unintended commands.
- Busy EEPROM causes no reads/writes until ready. Serial saves retain a snapshot,
  write at most one byte per pass, report later edits as unsaved, and reject
  conflicting commands. Save tokens distinguish an earlier completed UI save
  from a subsequent in-progress save.
- Display comparison work is bounded per pass, stops completely when idle, and
  finds a new value even when a scene changes during an older glyph's rendering.
- Five-target affine calibration, unstable samples, timeout rollover and mapped
  bounds; every interrupted calibration write preserves the previous transform
  and charger record. Calibration and charger saves cannot overlap.
- Captured touch targets, drag cancellation, stable press/release qualification,
  held adjustment repeats, page-paint gating and simultaneous wheel input. Real
  local apply/ACK/save works through raw touch samples, with inert diagnostics.
- The production application and serial extension select all simulator scenarios;
  missing-member confirmation, rejection, timeout, transport/partial failure,
  failed save, changed edit and earlier saved snapshot reach their UI results.
  Simulated save/restart/reset and disabled auto-resume retain their semantics.

The [generated preview](ui-preview.html) uses frames exported from those tests
and the pinned Adafruit GFX font. Zero-hold, pending-wake and failure layouts use
that font; captured screens exercise enlarged editor digits and
readable text equivalents. All 45 preview frames execute with bounded display
cells and accessible text in a Node canvas/DOM adapter. It is not an LCD test. Touch host tests substitute
raw coordinates and display/time/serial adapters; they do not emulate the ADC,
electrical SPI transactions or physical display response.

## Alert and thermal fault injection

- Exact 65/70/75/60 C boundaries, input-only overheating, strongest-level
  hysteresis, percentages applied once to authorized shares, cooling restoration
  and maximum cooling overriding manual AUTO.
- Both-sensor gating, live sensor loss stopping only its PSU, explicit recovery,
  and sensor retry leaving an unrelated setting failure blocked.
- Unknown readings pausing hot time, a known hot peer continuing it, cool readings
  resetting it, the full 15-minute limit and millis rollover.
- Hot units disappearing, remembered absence permissions not releasing a new
  incident, new explicit acceptance releasing it, and hot returns contributing
  again before inclusion.
- Protection changes during apply/INFO guards/ACK waits and readiness qualification:
  stale ACKs cannot synchronize newer targets, reductions retain the readiness
  deadline, OFF wins, and rejected reductions take the bounded fault fallback.
- Interrupted protection writes at every byte, schema-2/3 migration, retained
  216-byte records/calibration tails, normal-save/calibration contention, failed
  persistence, held-ON boot switches, load retention and OFF/reheating overtaking
  a pending resume save. Automatic persistence preserves unsaved drafts.
- Deduplication, 30-second popup pacing, 60-second rearming, repeated severity
  flapping, simultaneous incidents, active identity replacement, serial reconnect,
  acknowledgement without clearing protection, and no modal queue after dismissal.
- Independent GPIO levels, LOW-before-OUTPUT, debug isolation, pin collisions,
  actual wheel-adapter old-gesture draining, touch edits interrupted by alerts,
  stable displayed incidents, separate Resume/Retry actions, calibration deferral
  and protected editor/background-save state.
- Recent CAN-loss/discovery events remain observable after diagnostic counter
  saturation and clear only after the event-free interval.
- Periodic serial summaries: exact cadence boundaries, interval changes/off,
  error-before-warning ordering, acknowledged active repeats, cleared-once and
  all-clear output, retained short incidents and no repeated cleared history.
  Reconnect/reset preserves the period and acknowledgement. Command/report
  priority, valid partial input, expired/discarded input and millis rollover
  are exercised. All 15 sources flap while control status also changes, with
  UART capacity held at zero then three bytes per tick; CAN continues, input
  drains, commands execute and output does not overrun. Broadcast state adds
  12 bytes per serial-enabled build and no EEPROM fields.

Follow the [alert/thermal bench sequence](ALERTS.md#bench-sequence) for GPIO levels,
reductions, shutdown persistence and acknowledgement checks. Host/simulator
results are software evidence; physical PSU response still needs the bench.

## Physical bench checks still required

Use [the serial commissioning guide](GETTING_STARTED.md) for the actual command
sequence. Run initial communication checks without a battery/load; introduce
power tests only after commissioning appropriate limits and defaults.

1. Boot without USB, reconnect, and exercise the [serial lifecycle checks](SERIAL.md).
   Verify input works with CAN disconnected and with optional display absent.
   Check CS/INT/SPI, module crystal and termination if transmit errors rise.
2. With every intended PSU powered, inspect `diag bus`, `diag group NAME` and
   `raw on` briefly. Verify distinct stable identities, data replies, unsolicited
   broadcasts, correct ready-byte interpretation and no growing receive drops.
3. Label physical units and source groups. Confirm identity mapping individually,
   then repeat different power-up orders and address changes. A known identity
   must stay bound to its intended slot/group; an unknown replacement must not
   receive a setting before explicit adoption.
4. Apply modest selected limits and check ACKs plus a meter/load. Validate current
   scaling and actual source-group loading under parallel operation. A successful
   command or telemetry frame alone does not prove electrical behaviour.
5. Remove CAN from a member during preview, identity guard, and setting ACK wait.
   Inspect blocking/results, existing outputs, and the explicit partial workflow.
   Restore it at a changed address; verify the authorized profile returns without
   applying subsequently saved drafts or redistributing peers' current.
6. Test a not-ready unit and a rejecting unit. Check bounded attempts and clear
   errors, then recovery after a genuine readiness transition. Check that a
   partly failed global voltage apply prevents group-current changes until fixed.
7. Save profiles with auto-resume off/on, reboot with all/missing/replacement units,
   and observe actual write traffic. Test wrong deployment ID and legacy review
   before relying on stored profiles. Do not overwrite the only legacy copy
   before exporting it if it must be retained.
8. Set PSU offline defaults explicitly. Test controller/CAN loss and PSU power
   cycling, including the interval before discovery can verify a returning unit.
   Confirm hardware fallback is compatible with the common parallel bus.
9. Record `diag bus` before/after raw/watch output, EEPROM save, and display/wheel
   activity with all units broadcasting. Inspect `loop-max-us`, `rx-high-water`,
   `rx-drops`, `hw-overflow-events`, `trace-drops` and `output-overruns` as described
   in the [scheduling checks](GETTING_STARTED.md#checking-scheduling-under-load).
   Distinguish deliberately dropped console traces from actual CAN losses.
   Simulated tests do not validate real arbitration, UART/USB buffering, SPI
   faults, interrupt latency or worst-case loop load.
10. Where required, test EEPROM power interruption under the board's actual
    brownout configuration. The software journal tests do not model analogue
    brownout corruption or provide an electrical interlock.

11. Wire the selected SPI ILI9341 module per [the local UI guide](LOCAL_UI.md).
    Confirm logic levels, shared MISO release, separate CS, display ID, rotation,
    text readability and backlight drive. Boot with it absent: serial/CAN must
    still work and the wheel must not issue blind settings.
12. Exercise rapid wheel turns, short click, hold/release, cancellation, paging,
    optional idle dim/wake and serial edits during a local edit. A hold must not
    also commit a short click. Check the display-only build's automatic paging.
13. With all PSUs broadcasting, navigate repeatedly and perform local apply/save
    operations while serial remains in use. Verify ACK/error screens, EEPROM
    readback, no unexpected settings and CAN drop/error counters. Measure loop
    timing and free stack under this load; bounded transfer size alone does not
    prove hardware interrupt latency or electrical bus behavior.
14. Follow [touch wiring and calibration](TOUCH.md). Test corners, center, light
    contact, dragging, held +/- and release on the actual panel. Verify a contact
    cannot carry through to Apply/Yes on a new page. Check wheel/touch coexistence
    and dim/wake if those options are enabled. Save calibration, power-cycle a
    production build and verify both calibration and charger configuration.
15. Start `mega2560_touch_debug` without an MCP2515: exercise Tools, scenarios,
    faults and RAM save/restart. Confirm the SIM mark on every screen and volatile
    saves after physical reset. Then use a production touch profile with every
    PSU broadcasting; monitor `touch status` and `diag bus` during sustained input,
    calibration and saving. Record actual stack margin and receive/loop counters.

## Fan, enable and reconciliation coverage

The host suite also verifies exact byte-3 standby/fan frames and complete echoed
ACK payloads; first-boot zero/fan/voltage/wake with autoresume OFF; per-unit wake
at zero despite missing peers; and the required-participant barrier before positive current.
It checks inherited standby, saved-profile versus draft isolation, live OFF
preemption and rapid ON/OFF edges. Normal runtime OFF/ON sends no standby frame;
an OFF during initial wake still completes fresh awake/zero observations. Boot
OR and latest-wins/debounced switch coverage includes millis wrap and a serial
command during initial debounce. EEPROM interruption and calibration-tail
coverage are retained through the schema-4 migration.

Fault injection covers missing/readdressed units and identity lease expiry;
rejected zero/MAX/wake/fallback; bounded missing ACKs and late ACKs; missing fresh
current; nonzero current after zero-limit wake; unexpected output while held,
including during a pending wake identity check; explicit
reconciliation recovery; fan temperature/requested/measured-current triggers;
invalid/stale sensors; 30-second hysteresis; no repeat writes at steady demand;
and a pending AUTO overtaken by a new hot reading. A stop-to-held phase change
cannot replenish the fan retry budget. UI assertions check source labels, color,
fault details, pending versus confirmed wake, and continued telemetry. Simulator tests run with
ENABLE_IN, external PWM and CHG_ALLOW enabled at compile time but without GPIO APIs,
proving the debug application paths remain virtual. Fan tests cover preserved
AUTO/MAX mode and cooldown time across serial/switch edges, maximum-temperature
aggregation, stale/missing-member fallback, S-curve endpoints/monotonicity,
per-unit current overrides and 30-second release. The physical adapter harness
checks all six supported Mega pins, both polarities, all 256 duty commands,
phase-correct mode/TOP/prescaler, DC endpoints, unselected timer/channel
isolation, boot-full polarity, changed-only writes, 100 ms cadence and millis
wrap. All six pin variants also compile against the real AVR register headers.

## AC startup and availability coverage

The host suite drives the production startup controller through inhibited,
allowed and latched-cutoff states. It verifies the two-second inhibit, invalid
configuration/probe policy, explicit CAN-initialization retry, eight-second
readiness, separate stop/readiness deadlines, post-ready reassertion and
battery-backed CAN with absent AC. Alarm bit 29 and observed address negotiation
invalidate stale epochs and confirmations. A missing INFO response alone does
not imply failed stopping; actual unexpected current still trips the cutoff.

Availability tests cover the grace and stable-group intervals, unstable sources,
whole-group and individual selection, new missing members, saved and session
exceptions, rejected faults, explicit zero-held rejoining and waiver revocation.
Current remains divided across full configured membership; excluded units retain
the latest authorized share. An all-excluded apply updates intent with zero
hardware successes. Serial/wheel/touch use the same confirmation transaction.
The touch suite also exercises actual application save/restart and rejoining.

Every byte interruption during a remembered decision preserves a complete old
or new record. Save/readback failure, a simultaneous CAN return, configuration
changes and confirmation expiry never silently activate a stale decision.
Schema-2 migration supplies no exceptions; session waivers do not leak into an
ordinary save. Background refresh sends only authorized volatile V/I or held
zero, leaves operator reports intact and does not retry a failed explicit apply.

The GPIO harness verifies LOW-before-OUTPUT, changed-only writes and conflicting
pin rejection. These checks do not establish relay timing or electrical fail-safe
behavior. Follow [STARTUP.md](STARTUP.md#bench-acceptance), including physical
LOW during reset/upload, simultaneous power-up, independent source loss/return,
battery removal, real fallback defaults and cooling with enable OFF. The
simulator scenarios and generated previews do not replace those bench checks.
Compile checks reject CAN, ENABLE_IN, unsupported PWM and encoder Timer1 conflicts.

Manual AUTO/MAX/policy tests cover all/group/slot/range selection, malformed
commands, exact fan-only writes, accepted-mode deduplication, all-or-none
validation with a missing member, connection/address recovery, explicit policy
and reconciliation reset, EEPROM exclusion, and console reconnect retention.
AUTO is cancelled by serial/switch enable before a high-current restore; an
unsent AUTO cannot overtake that change. Rejected and missing fan ACKs retain
the normal fault latch/two-attempt budget, including a late ACK after failure.
The production debug application accepts the same commands and reports matched
requested/ACKed modes. These tests do not measure real fan RPM.

Before using the new sequence on hardware, verify zero `0x0103`, `0x0132` and
`0x0134` acceptance on the actual G2 firmware, actual zero/standby current,
zero-current retention across wake, readiness timing, and audible/physical
Auto/Max fan response. With `autoresume off`, wait for `held-at-zero-awake` and
`zero-confirmed=1 awake=1`. Test live OFF during apply and loss of CAN during stop:
no unconfirmed stop may appear as a successful zero hold. Check confirmed fault
standby is reported separately. Exercise the wired D26 switch, serial overrides and power cycling with
the switch held ON (boot OR deliberately permits restarting). Confirm absence
and replacement cases with all intended PSUs, and measure stack/interrupt margin
in the chosen Mega profile. Software simulation does not establish these results.

External fan validation still requires the selected four-wire fan and its
input interface: measure 25 kHz and polarity, check 0%/100% levels, startup at low
duty, full-duty cooling and cooling through an enable change. Confirm the fan's
actual behavior at zero duty; not all models stop. The host harness proves
register programming and requested demand, not electrical waveforms or motor
operation. Check CAN/display/serial activity with the external fan running.

## Zero-current readiness regression

The G2 bench reported repeated zero/fan/voltage/wake sequences with fresh CAN,
zero measured current, status `0x400`/`0x600` and a toggling output-ready flag.
The production-controller harness now covers:

- A cold not-ready PSU receives its full startup allowance before fresh benign
  status can establish zero hold. Eighty seconds of readiness/status toggles
  cause no repeat wake or MAX commands; fan cooldown completes normally.
- A 5 A apply and saved-profile restore from zero wait for matched current ACK
  and fresh output readiness. Only the requested current is written. Applying
  zero or switching OFF during startup returns to confirmed zero hold.
- Never-ready output latches the readiness timeout, without periodic current
  retransmission; the existing standby fallback or fitted cutoff remains
  latched. Missing/unknown status, actual AC absence and unexpected held current
  retain their blocking or fault paths.
- Serial diagnostics distinguish zero, waiting, active and fault; a slow UART
  still receives complete fault messages without buffer overruns. UI snapshots
  show the starting banner, pending output and complete reconciliation instruction.

These are simulated observations, not measurements of loaded G2 behavior. The
next bench run must establish stable `zero-confirmed=1 awake=1`, then accepted
test limits, fresh output-ready status and actual current/voltage at the load.
Check `diag control`, `telemetry GROUP1` and `diag bus`; do not treat a setting ACK
as proof of delivered current. Finish with `autoresume off` and verify zero hold.

## Sustained output-readiness regression

The later loaded bench run delivered current after the loose DC connection was
corrected. Separately, the controller harness reproduced a ready pulse promoting
`Active`, followed by a not-ready frame restarting reconciliation about 0.6 s
after the current write. This patch addresses that software edge; it does not
attribute the loose-connection incident to the firmware.

The production-controller regressions now cover:

- A single pulse and sustained flapping remain in the original startup window,
  without re-zero/wake writes or repeated positive-current refreshes. At least
  three post-ACK frames spanning 2000 ms, with gaps at most 1000 ms, qualify.
- Cached snapshots, same-timestamp bursts, and ready/not-ready/ready frames in
  one CAN drain cannot falsely complete qualification. A gap resets its interval.
- Pre-ACK broadcasts, duplicate/late/rejected/missing ACKs, absent broadcasts and
  stale voltage/current data cannot substitute for qualification. Finishing
  exactly at the fixed deadline faults instead of briefly reporting running.
- Staged changes remain inert. A second positive/voltage apply is blocked;
  increasing the polling interval cannot extend an existing deadline. Zero
  applies and OFF interrupt startup. AC loss, link loss and identity replacement
  retain their reconciliation/safety paths.
- A readiness timeout survives CAN disappearance, re-verification and AC edges;
  return permits only bounded fault stopping, with lost stop proof explicitly
  unconfirmed. Failed stopping keeps the original cause. ON cannot clear it;
  explicit reconcile restores the authorized profile, not newer drafts.
- Two PSUs qualify independently without exclusions or redistribution; an
  already-qualified group remains configurable. The eight-unit job and simulator
  complete; startup timers also work across `millis()` wrap.
- Serial and the PSU detail UI distinguish acceptance, verification and measured
  output. Progress remains intact through a throttled UART; the group card shows
  `VERIFY`, and the controller does not report running before the first write.

The new loaded bench check remains outstanding: flash `mega2560_serial`, apply
the intended test limits, inspect `diag control` / `diag psu 1` for qualification
followed by `current-state=active`, and independently check delivered voltage and
current. Transient ready pulses must not produce repeated zero/wake sequences.
Then request OFF and confirm zero hold. The fan policy was not changed.
