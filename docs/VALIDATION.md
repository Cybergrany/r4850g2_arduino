# Validation

Target: Arduino Mega 2560, eight-slot capacity. Parallel backend, ILI9341 UI,
touch/calibration, CAN-free simulation and cooperative scheduling validation was
extended with AC-cutoff startup, source availability, remembered exclusions,
post-ready reconciliation and volatile-setting refresh on 2026-10-28. The
zero-current readiness fix adds regressions for the reported G2 bench trace.
No firmware upload or physical validation of this fix was performed here.

```sh
python3 tools/test_host.py
python3 tools/test_touch.py
python3 tools/test_fan_output.py
python3 tools/test_charge_allow.py
PLATFORMIO_CORE_DIR=/home/dave/psu-can-workspace/.platformio pio run \
  -e mega2560 -e mega2560_serial -e mega2560_display \
  -e mega2560_local -e mega2560_minimal \
  -e mega2560_touch -e mega2560_touch_wheel -e mega2560_touch_local \
  -e mega2560_ui_debug -e mega2560_touch_debug -e mega2560_touch_debug_local \
  -e mega2560_touch_enable -e mega2560_touch_fan -e mega2560_serial_fan \
  -e mega2560_serial_cutoff -e mega2560_touch_cutoff -j 4
python3 tools/check_debug_build.py
git diff --check
```

The absolute cache path isolates this workspace's PlatformIO packages; elsewhere
omit it or choose another writable cache. Pass the same cache to
`check_debug_build.py --core-dir PATH` when using a different location.
All sixteen profiles build successfully
with the pinned Atmel AVR platform 5.3.0, Arduino AVR framework 5.4.0, AVR GCC
7.3.0, CAN 0.3.1, Adafruit ILI9341 1.6.3, Adafruit GFX 1.12.6,
Adafruit BusIO 1.17.4, ClickEncoder d6d5738fdf, and TimerOne 1.2.0.
A clean build retains ClickEncoder's existing constructor member-order warnings.

| Profile | Static RAM (bytes / 8192) | Flash (bytes / 253952) |
| --- | ---: | ---: |
| `mega2560` | 6573 | 121774 |
| `mega2560_serial` | 4906 | 81898 |
| `mega2560_display` | 6548 | 116394 |
| `mega2560_local` | 5542 | 85136 |
| `mega2560_minimal` | 3785 | 37474 |
| `mega2560_touch` | 6781 | 131006 |
| `mega2560_touch_wheel` | 6806 | 132452 |
| `mega2560_ui_debug` | 6575 | 123416 |
| `mega2560_touch_debug` | 6783 | 132308 |
| `mega2560_touch_local` | 5750 | 91986 |
| `mega2560_touch_debug_local` | 5752 | 92414 |
| `mega2560_touch_enable` | 6781 | 131248 |
| `mega2560_touch_fan` | 6786 | 131630 |
| `mega2560_serial_fan` | 4911 | 82182 |
| `mega2560_serial_cutoff` | 4907 | 81986 |
| `mega2560_touch_cutoff` | 6787 | 131716 |

The full touch/serial/enable/fan/cutoff build uses 82.8% static RAM and 51.9% flash,
leaving 1405 bytes for stack/runtime use. Touch and wheel leave 1386 bytes;
omitting serial leaves 2442 bytes in the production touch profile. The serial-only
cutoff build leaves 3285 bytes. Fixed UI/parser strings and deployment templates
now live in flash on AVR. Static RAM excludes worst-case stack/interrupt nesting.
There is no full colour framebuffer; two text/style scenes use 1170 bytes.
Worst-case stack and display/CAN timing still require a hardware check. EEPROM reserves only
512 bytes, with two 256-byte slots and a 216-byte schema-3 record at this capacity.
Touch calibration adds independent 40-byte records in the unused slot tails;
schema-2 records remain readable, with no absence exceptions. Older firmware
does not understand schema 3; review the [downgrade caveat](ARCHITECTURE.md).

All three debug ELFs passed `avr-nm` checks: simulated CAN and RAM adapters are
present; physical CAN/MCP2515, EEPROM, cutoff and fan PWM symbols are absent. Their
dependency graphs also omit CAN. This verifies software isolation, not touch
hardware or production CAN timing.

## Host coverage

The tests compile production configuration, protocol, discovery, controller,
EEPROM, serial parser, and MCP2515 transmit code with simulated hardware/time.
Compiler flags include `-Wall -Wextra -Werror` and AddressSanitizer/
UndefinedBehaviorSanitizer. LeakSanitizer is disabled for the ptrace sandbox;
address and undefined-behaviour checks remain enabled.

Both suites pass: **638730 host checks** and **1004 touch/simulator checks** with
UI snapshots exported (counts include export assertions). UI navigation, formatting and persistence use the production
model/layout code; the host suite does not simulate the actual ILI9341, SPI
interrupt masking or ClickEncoder hardware. Its assertion total is dominated by checking every EEPROM
access and interruption boundary, not hundreds of thousands of independent cases.
Meaningful scenarios include:

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
readable text equivalents. All 41 preview frames execute with bounded display
cells and accessible text in a Node canvas/DOM adapter. It is not an LCD test. Touch host tests substitute
raw coordinates and display/time/serial adapters; they do not emulate the ADC,
electrical SPI transactions or physical display response.

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
coverage are retained through the schema-3 migration.

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
