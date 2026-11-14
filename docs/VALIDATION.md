# Validation

Target: Arduino Mega 2560, nine-slot capacity. Validation covers the scoped PSU
backend, ILI9341/touch UI, serial, cooperative scheduling, persistence and isolated
simulation. No firmware upload or physical validation of this patch was performed.

## Target/actual voltage header; wheel bench profile (2026-11-13)

The header now shows authorized target/actual voltage, e.g. `54.0/54.1V`.
Targets use exact agreement; actual readings are averaged only if every fresh
sample lies within +/-0.25 V of that mean, inclusive. Either side can show MIX
or --; partial coverage retains `*`. Draft edits do not change the authorized
target. The smaller A/W fields preserve units and partial markers using the
existing bounded precision/engineering formatting; page count remains visible.

`mega2560_touch_wheel` is deprecated and removed from `platformio.ini`. The
current hardware profile is **mega2560_wheel_alerts**. Touch-only profiles remain
available. There are now **21 shipped environments**. Older sections below are
validation history; their touch/wheel figures and old build lists describe the
firmware at that time, not the current supported matrix.

Validation passed on these sources:

- Ordinary host **3,373,665 checks** and profiled host **3,501,722**, including
  the supplied nine-unit 54.04–54.16 V readings, exact mixed targets, staged
  versus authorized values, +/-0.25 V boundaries, skewed averages, partial and
  missing data, timer rollover and maximum nine-unit A/W totals without clipping.
- Touch **1,393 checks**, real wheel **19,652**, production renderer at five
  clocks with **91 captured scenes**, route-search budget, AVR formats and stack
  parser. Regenerated previews include the normal, mixed and partial headers.
- All **21 Mega builds**, memory limits, three UI-debug isolation checks and
  observer isolation. The AVR complete-redraw simulation also passes; its group
  highlight completes in about 89 ms with nominal SPI, not a hardware guarantee.

| Build | Flash bytes | Static RAM | Static free | Direct runtime peak | Estimated remaining margin |
|---|---:|---:|---:|---:|---:|
| mega2560_wheel_alerts | 204348 | 7194 | 998 | 606 | **320** |
| mega2560_wheel_profile | 211524 | 7337 | 855 | 608 | **175** |

Static RAM is unchanged. The header uses non-inlined helpers and a bounded
12-byte text buffer; its temporaries finish before profile-detail composition.
This also lowers the compiler's peak direct runtime path by 35 bytes in both
wheel builds. Margins retain the corrected audit's 60-byte interrupt and 12-byte
argument/EEPROM allowance, production-load scratch constraints and uncertainty.
The wheel/alerts complete header path is 498 bytes, below its 606-byte peak.
These remain source/disassembly estimates; retain `diag memory` in bench runs.

Build the current bench target with:

```sh
PLATFORMIO_CORE_DIR=/home/dave/psu-can-workspace/.platformio pio run -e mega2560_wheel_alerts
python3 tools/analyze_avr_stack.py .pio/build/mega2560_wheel_alerts/firmware.elf
```

No EEPROM reset is required. On hardware, check the target against authorized
values in `diag control`, and actual against fresh per-unit readings in
`telemetry all`. Capture `diag ui`, `diag bus` and `diag memory` while navigating
and viewing profiles. An individual voltage difference can produce target MIX;
missing telemetry produces a partial marker or --. Neither indication changes
charging policy or substitutes for PSU acknowledgements.

## Per-PSU current and combined bench build (2026-11-13)

Normal global/group current now sets the same limit on every selected PSU.
The editor, fullscreen touch editor, serial help and global review say per PSU;
group monitoring remains a total. Mixed targets show their per-unit range, and
the selected maximum is the lowest existing per-unit limit. Invalid serial
requests leave every draft unchanged. No EEPROM/profile format or stored value
is converted; the explicit advanced offline-current command still uses a group
total. Existing scripts must adopt the new normal-current meaning. See
[upgrade semantics and commissioning](PROFILES.md#upgrading-from-group-total-current-controls).

The combined build includes the reviewed CAN scheduling changes and stack fixes
described below. Validation on the final sources passed:

- Ordinary host **3,372,895 checks**, profiled host **3,500,959**. New cases cover
  nine PSUs in unequal 3/2/4-member groups, global/group/individual precedence,
  independent voltage/current fields, no rounding division, mixed edits and
  cancellation, atomic capacity rejection, OFF preparation, excluded members
  retaining their targets, and unchanged offline allocation. Existing profile,
  migration, interruption, protection, identity and acknowledgement tests pass.
- Touch/simulator **1,393 checks**, real wheel **19,652**, production renderer at
  five clocks and **84 captured UI scenes**, route/transport, output GPIO,
  bus-log/stack-parser, format and isolated-storage checks.
- All **22 Mega profiles**, physical memory checks, all three UI-debug isolation
  checks and observer isolation. simavr executes the actual wheel/profile ELF
  with INT held LOW: one receive interrupt, advancing clock/UART/TFT. The AVR
  complete-redraw benchmark also passes; nominal SPI simulation is not loaded
  bus or hardware latency proof.

| Combined build | Flash bytes | Static RAM bytes | Static free bytes |
|---|---:|---:|---:|
| mega2560 | 203420 | 7191 | 1001 |
| mega2560_wheel_alerts | 204044 | 7194 | 998 |
| mega2560_wheel_profile | 211222 | 7337 | 855 |
| mega2560_touch_wheel | 215002 | 7424 | 768 |
| mega2560_serial_profile | 130038 | 5467 | 2725 |
| mega2560_can_observer | 17282 | 1902 | 6290 |

Per-PSU settings add no static RAM. The corrected final runtime direct peaks
remain **641 / 643 / 657 bytes** for wheel/alerts, wheel/profile and touch/wheel.
Including the documented 60-byte interrupt and 12-byte argument/EEPROM allowance
leaves estimated margins **285 / 140 / 39 bytes**. The touch estimate is narrow:
exercise profile/participation saves, recovery and calibration under CAN traffic
and capture `diag memory`. See the source constraints and uncertainty below;
these estimates do not establish the cause of a previous bench failure.

Bench comparison:

1. Keep enable OFF. Capture `config`, `preview all`, `diag bus`, `diag ui` and
   `diag memory`. Existing stored per-unit targets should match the previous
   firmware; do not erase EEPROM for this update.
2. Stage `set current all 1`, then `preview all current`: every selected PSU
   should show `target A=1.00`. A three-member group has 3 A configured total.
   Set one group's current, then one member's current; verify only those selected
   targets change and that its UI shows MIXED. Cancel a mixed edit and check that
   it changes nothing. Try an invalid serial setting and verify atomic rejection.
3. After reviewing voltage and every target, use the existing OFF preparation,
   explicit ON, stop and profile/reboot tests in [PROFILES.md](PROFILES.md#bench-sequence).
   Physical acceptance and delivery still require PSU telemetry/measurement.
4. Follow the [nine-unit receive-pressure soak](BUS_PROFILING.md#software-queue-pressure-comparison).
   Include idle and navigation periods, normal diagnostics, profile operations
   and the smallest memory watermark. Capture growing receive-loss counters and
   their new phase/gap fields; the patch does not suppress real loss alerts.

## Bounded software-queue pressure handling (2026-11-13)

Intermediate receive passes expand from four to at most twelve frames only when
backlog reaches eight, stopping early with two remaining. Four-frame discovery
and thermal checkpoints are retained. Optional periodic UI composition waits for
an empty queue; input, notifications, explicit redraws and bounded painting
remain active. Added safe boundaries bracket composition. No receive processing
runs reentrantly inside a controller loop or a view's live references.

Ordinary `diag bus` now reports actual receive-attempt gaps, the phase when the
maximum gap ended, and the phase captured at the latest software-ring loss. These
are distinct from phase/loop totals and from profile frame queue age. See the
[bench comparison and interpretation](BUS_PROFILING.md#software-queue-pressure-comparison).

Validation passed:

- Ordinary host **3,371,301 checks**, profiled host **3,498,832**: bounded
  continuous replenishment, low/high-pressure boundaries, room for a following
  burst with nine identities, INFO/ACK behind telemetry, unchanged readiness
  deadline, newer OFF versus a stale positive ACK, input-only overheating and
  negotiation. A hot first four-frame batch remains latched when later frames
  cool during the same extended pass.
- Touch/simulator **1,351 checks**: repeated periodic deferral keeps input armed,
  wheel and touch navigation redraw, a new alert remains actionable without
  automatic acknowledgement, and periodic refresh resumes once the queue clears.
- Production MCP2515/observer regressions: bounded IRQ/refill/held-LOW/re-arm,
  FIFO, software/hardware loss, clock rollover/backstep and loss context captured
  before the reporting phase. No larger ring or per-frame storage was added.
- Production renderer at 500 kHz / 1 / 2 / 4 / 8 MHz, real wheel **19,652 checks**,
  route-search budget, AVR format and bus-log parser checks. Layout/pixels and
  generated preview scenes are unchanged.
- All **22 Mega builds**, physical memory limits, three UI-debug isolation checks
  and observer isolation. The actual wheel/profile ELF also passes the simavr
  held-LOW interrupt regression; its clock, UART and TFT continue progressing.

The following figures describe the CAN/stack changes before the per-PSU current
change; use the combined build table above for the delivered firmware.

| Build | Flash bytes | Static RAM bytes | Static free bytes |
|---|---:|---:|---:|
| mega2560_wheel_alerts | 204864 | 7194 | 998 |
| mega2560_wheel_profile | 212044 | 7337 | 855 |
| mega2560_touch_wheel | 215818 | 7424 | 768 |
| mega2560_serial_profile | 130222 | 5467 | 2725 |
| mega2560_can_observer | 17282 | 1902 | 6290 |

Ordinary production builds add **14 static bytes**; profile/observer builds add
**five**, reusing their existing receive clock fields. Debug builds add three
bytes. No additional scene, queue or configuration copy is introduced.

**Stack-audit correction:** all earlier stack estimates and positive margins in
this document, including the 765/668-byte figures, are superseded by this audit.
`analyze_avr_stack.py` accepted partial
hexadecimal operands: the touch UI's `0x6A` allocation was read as `0x6`, hiding
100 bytes; other frames were affected too. The parser now accepts either case
and rejects malformed operands instead of accepting a prefix. Regressions in
`python3 test/test_avr_stack.py` exercise the production parser.

A separate rebuild of `d65554a`, compared with `10b4d72`, found the same existing
serial participation-save direct chain in both: 978 bytes for wheel/profile and
993 for touch/wheel, before interrupt/argument allowances. The actual
`SerialConsole::execute` frame was 450 bytes and `validCurrentRecord` held a
267-byte configuration plus saved registers. Touch-driven apply also retained
unrelated button/calibration and multiple preview arrays. These were reachable
exhaustion risks, not a consequence of the added receive calls.

The follow-up keeps topology snapshots, serial startup/apply/profile parsing,
calibration solving and recovery-return copies in separate non-inlined helpers.
Button arrays are released before dispatching their actions. UI/profile plan
preparation and error previews finish before another plan or EEPROM validation
is allocated. Validation order, mutations, confirmations and save ownership are
unchanged; no shared scratch, extra static state or packing was introduced.

The corrected production runtime comparison below includes `main`, saved
registers and three-byte calls. Margins subtract a further **60 bytes** for the
registered CAN interrupt and **12 bytes** for outgoing arguments and virtual
EEPROM reads. Negative historical margins identify the risk that prompted the
fix; these are not measurements of a particular previous bench failure.

| Build | d65554a free / direct peak / margin | 10b4d72 free / direct peak / margin | After stack fix free / direct peak / margin |
|---|---:|---:|---:|
| wheel_alerts | 1012 / 978 / -38 | 998 / 978 / -52 | 998 / 641 / **285** |
| wheel_profile | 860 / 978 / -190 | 855 / 978 / -195 | 855 / 643 / **140** |
| touch_wheel | 782 / 993 / -283 | 768 / 993 / -297 | 768 / 657 / **39** |

The final largest direct runtime path is profile-detail composition:
`main -> Application::tick -> LocalUi::tick -> UiView::compose -> UiLayout::extras
-> MemoryManager::profileTargets -> newestProfile -> read32 -> read16`.
In touch/wheel, complete serial-command, protection-migration and boot paths are
603, 558 and 520 bytes respectively. Boot dispatch can only enter calibration;
it cannot follow the raw call graph into profile/application actions.
Production `loadCandidate` and migrated protection pass caller-owned scratch,
so the raw graph's nested `validCurrentRecord` candidate is unreachable there.
Ordinary saves legitimately validate a separate candidate and remain included.

The final complete `main -> Application::tick -> LocalUi::tick -> serviceReceive`
paths are **315 / 334 / 318 bytes** for wheel/profile, touch/wheel and wheel/alerts.
The thermal/discovery branch is deepest. Actual transport receive and serial
frame-observer callbacks have direct chains of 46 and 26 bytes and do not exceed
it; their arguments fit registers. CAN IRQ overlap is 38 bytes in the registered
handler plus 16 in the external-interrupt wrapper, a three-byte indirect call
and the three-byte hardware return address. Encoder/Timer0/UART handlers are
smaller and do not enable interrupt nesting. EEPROM's actual read implementation
has no local frame; the allowance covers its indirect call and the extra outgoing
arguments on profile reads. The serial extension only exposes diagnostics,
calibration and touch-debug navigation in production, not profile/apply actions.
No reachable allocating `new`/`malloc` path was found.

The final 22 builds, host/profiled-host, touch, real wheel, renderer, receive,
route-budget, format, parser and debug/observer-isolation checks above were rerun
after the stack changes. The actual wheel/profile ELF passes
`python3 tools/test_avr_interrupt_guard.py --simavr-prefix /tmp/r4850-simavr/usr`:
one IRQ entry under held LOW, progressing UART/TFT, and no invalid clock samples.
Reproduce the disassembly audit with `python3 tools/analyze_avr_stack.py
.pio/build/mega2560_touch_wheel/firmware.elf` (and the wheel/profile and wheel/alerts
ELFs); apply only the documented production argument/boot constraints when
reviewing its deliberately unpruned output.

These are source/disassembly estimates, not measured worst-case guarantees. Keep
`diag memory` in the bench capture, including profile loads/saves, recovery,
calibration and normal CAN traffic; investigate a near-zero watermark. The
advisory 1 KiB policy remains, and software/simavr results are not PSU hardware
timing or electrical-response proof.

## Idle CAN interrupt starvation guard (2026-11-13)

The first hardware run of the preceding patch reported almost stalled UI/serial
with PSU AC off. Diagnostics showed no received frames, a 32 us maximum receive
handler, 14,832 invalid clock samples and a 996 us maximum backstep. A short
individual handler does not rule out starvation from immediate LOW-level
re-entry. The original host SPI fake invoked each interrupt only once, missing
this failure mode. The physical cause of the asserted interrupt remains to be
confirmed with the new pin/flag diagnostics; these observations do not establish
that the MCP2515's documented READ RX BUFFER flag clear is faulty.

The adapter now detaches its interrupt if INT remains LOW after one bounded
snapshot. Foreground receive passes continue polling with other interrupts
enabled, preserve queued frames, and re-arm only after INT releases. No settings,
enable, fan, persistence or readiness-retry policy is changed. `diag bus` exposes
the yield count, polling state, sampled pin level and flags at the last yield.
Its extra output has its own bounded row rather than overflowing the serial
response reservation.

Validation passed:

- Production RX/observer host tests now model LOW-level retriggering, an idle
  stuck pin, continuous buffer refills, FIFO preservation with a full ring,
  bounded foreground polling, and arrival during interrupt re-arm.
- Ordinary host **3,368,449 checks** and profiled host **3,495,980 checks**; AVR
  format, renderer at five clocks, touch **1,310 checks**, real wheel **19,652
  checks**, bus-log parser, route budget, and debug/observer isolation.
- All **22 Mega builds** and physical flash/RAM bounds.
- `tools/test_avr_interrupt_guard.py` executes the built wheel/profile Mega ELF
  in simavr with idle MCP/TFT register models and INT4 continuously asserted.
  The pre-fix ELF reproduces repeated IRQ entry and starvation. The patched ELF
  enters the CAN handler once, boots, answers `diag bus`, advances the display,
  and retains Timer0 progress with zero invalid clock samples. This validates
  actual AVR instructions/masking; simulated SPI speed is not a bench latency
  measurement. The fixture explicitly models LOW-level INT4 retriggering, which
  simavr 1.6's PE4 implementation does not generate by itself.

To run the optional AVR test, install `libsimavr-dev` and build
`mega2560_wheel_profile`, then run `python3 tools/test_avr_interrupt_guard.py`.
`--simavr-prefix` supports locally extracted packages; `--elf` and
`--expect-starvation` allow checking the pre-fix ELF without flashing it.

| Build | Flash bytes | Static RAM bytes | Static free bytes |
|---|---:|---:|---:|
| mega2560_wheel_alerts | 202744 | 7180 | 1012 |
| mega2560_wheel_profile | 210270 | 7332 | 860 |
| mega2560_touch_wheel | 213614 | 7410 | 782 |
| mega2560_serial_profile | 129944 | 5462 | 2730 |
| mega2560_can_observer | 17186 | 1897 | 6295 |

Static state grows by six bytes including the added virtual-table entry. The
wheel/profile direct main-chain estimate remains 765 bytes; receive interrupt
overlap is approximately 60 bytes including wrapper/return addresses. The
remaining arithmetic margin is 35 bytes before indirect/argument uncertainty;
the existing detailed stack caveats still apply. Use `diag memory` during bench
stress rather than treating this estimate as proof of runtime margin.

Follow the USB-only and nine-unit checks in [BUS_PROFILING.md](BUS_PROFILING.md).
Hardware confirmation of responsiveness and the interrupt's underlying cause
is still required.

## Integration review (2026-11-12)

Reviewed the receive-buffer reads against MCP2515 DS20001801K section 12.4 and
traced interrupt masking, queue ownership, retry lifetime and UI confirmation.
Clarified that failed readiness recovery from zero hold shares the retry bound;
added a controller regression proving that it remains OFF, stops retrying after
the bound and only returns to zero hold after explicit retry. The existing
benign-zero-hold regression still consumes no retries. This review follow-up
changes comments, documentation and tests only.

Independent reruns passed: ordinary host **3,367,502 checks**, profiled host
**3,495,033 checks**, production RX/observer, renderer at all five SPI clocks,
touch **1,310 checks**, real wheel **19,652 checks**, parser, route budget and AVR
format checks. All **22 Mega profiles**, memory checks and debug/observer isolation
passed. Firmware sizes match the table below; the documented narrow stack margins
and hardware validation requirements still apply.

## Bounded output-readiness recovery (2026-11-12)

`PSU_OUTPUT_READY_RECOVERIES=2` permits two automatic zero/wake/profile restarts
when a reconciled unit loses acceptable readiness. This includes zero hold when
the benign-idle checks fail; benign zero-hold changes do not consume a retry.
The next loss latches the existing bounded fault-stop path. Brief ready periods,
OFF/ON, CAN/AC/address recovery and configuration reloads cannot renew the budget.
Explicit serial `reconcile` or
the guarded **Retry ALL** UI review resets the counters; acknowledging the alert
does not. The counters follow stable identities within the controller session.
EEPROM layout, fan policy and startup electrical sequencing are unchanged.

- Ordinary host suite: **3,367,319 checks**; profiled suite: **3,494,803**.
  Regression cases repeatedly qualify for more than two seconds, lose readiness,
  and verify that automatic restore stops at the configured bound. They cover
  healthy peers, CAN/AC loss, OFF/ON, configuration reload/reordering, internal AC
  reconciliation, timer rollover, failed standby fallback and fitted AC cutoff.
- The real controller fault drives serial and UI tests: acknowledgement preserves
  ERROR/fault/count, Retry ALL opens with Cancel selected, a newer OFF invalidates
  the review, and a fresh retry preserves OFF and restores zero hold. A focused
  build with `PSU_OUTPUT_READY_RECOVERIES=0` passes **6,296 checks**.
- Touch/simulator: **1,350 checks**. Production renderer: **82 exported scenes**
  plus transfer/pixel regressions at all five SPI clocks. Updated previews include
  the restart-limit alert, PSU details and retry review. Route-budget and AVR
  format checks pass; real-wheel/RX adapter validation is recorded below.
- All **22 Mega profiles** build; physical memory limits, all three UI-debug
  isolation checks and observer isolation pass. Reproduce with
  `PLATFORMIO_CORE_DIR=../.platformio pio run -e <environment>` for every
  `[env:...]` in `platformio.ini`, then `python3 tools/check_memory.py`.

| Environment | Flash bytes | Static RAM | Static free |
| --- | ---: | ---: | ---: |
| `mega2560_touch_wheel` | 213036 | 7404 | 788 |
| `mega2560_wheel_alerts` | 202170 | 7174 | 1018 |
| `mega2560_wheel_profile` | 209718 | 7326 | 866 |
| `mega2560_serial_profile` | 129288 | 5456 | 2736 |
| `mega2560_can_observer` | 16736 | 1891 | 6301 |

The limiter adds **nine static bytes**, one counter per slot; together with the
receive/timing patch the controller builds add **19 bytes** over the baseline.
Its identity remapping uses a nine-byte local snapshot, not another configuration
copy. The linked remapping chain is 47 bytes and explicit reconciliation is 59
bytes; neither becomes the largest reachable path.

The fullest touch+wheel image retains the previously source-reviewed 668-byte
runtime direct-call peak through profile loading. As described in the detailed
audit below, production `loadCandidate`/protection loading pass existing scratch:
the raw graph's nested 256-byte candidate is unreachable on those paths. Allowing
about 12 bytes for outgoing arguments/virtual EEPROM calls and 54 bytes for the
registered RX interrupt leaves an estimated **54 bytes** in that build. The wheel
profiler's unpruned 765-byte main path leaves 47 bytes after that interrupt,
before indirect-call/argument uncertainty.
These are narrow estimates, not measured worst-case stack guarantees. Repeat
`diag memory` through boot, profile loads, alert/retry reviews and normal traffic;
keep the smallest watermark with the capture.

The [bench comparison](BUS_PROFILING.md#readiness-restart-limit-check) verifies the
operator-visible bound without assuming software ACKs prove physical PSU output.

## Bounded receive and clock diagnostics (2026-11-12)

The receive adapter replaces the library's unbounded ISR loop with a two-buffer
snapshot and burst transfers. Controller/application phase boundaries now service
RX, and routine reads defer to queued frames and the existing negotiation quiet
interval. No fan, readiness-recovery, allocation or persistence policy changes
are included in this change. See [the capture comparison](BUS_PROFILING.md#receivetiming-patch-comparison).

- Ordinary host suite: **3,358,431 checks**; profiled host suite: **3,485,315**.
  Added bounded-backlog scheduling and fresh INFO/ACK-behind-telemetry cases.
  Negotiation regression now requires zero application TX until quiet ends.
- Production RX adapter tests cover both buffers refilling immediately after
  CS release, a fixed maximum of three transactions / 31 SPI bytes per ISR,
  full-ring loss, malformed DLC, nonextended/RTR frames, negotiation counts,
  error-flag preservation, timestamp wrap/backsteps and observer transmit isolation.
- Production renderer passes at 500 kHz, 1/2/4/8 MHz, including a backwards clock
  sample after a completed transfer, final-pixel correctness and valid rollover.
  Invalid measurements remain visible through dedicated diagnostics; they cannot
  poison duration maxima or prolong a cooperative rendering slice.
- Touch/application simulation: **1,310 checks**. Route-search budget passes for
  two/nine configured units and partial/full presence. No extra scene buffer or
  configuration copy was introduced.
- All **22 Mega profiles** build and fit physical flash/RAM; the advisory 1 KiB
  warnings remain. Observer/three UI-debug isolation checks, real-wheel **19,652
  checks**, CSV parser regressions and AVR-format checks pass.

Representative affected builds:

| Environment | Flash bytes | Static RAM | Static free |
| --- | ---: | ---: | ---: |
| `mega2560_wheel_alerts` | 200892 | 7165 | 1027 |
| `mega2560_wheel_profile` | 208490 | 7317 | 875 |
| `mega2560_serial_profile` | 128466 | 5447 | 2745 |
| `mega2560_can_observer` | 16736 | 1891 | 6301 |

These add **10 static bytes** over their previous images. The linked wheel
profiler's unpruned direct main-loop chain is 765 bytes. The actual registered
RX handler is a 32-byte direct chain; including the indirect external-interrupt
wrapper/call and hardware return gives approximately 54 bytes of overlap. That
leaves only 56 bytes in the unpruned profiler estimate before other indirect
callee/argument uncertainty; the ordinary wheel build has 152 bytes more room.
`analyze_avr_stack.py` now explicitly includes our adapter's ISR rather than
mistaking the library's retained but unregistered virtual ISR path for its owner.
Continue using `diag memory` under the actual loaded workload; static graph
analysis and the previous observed watermark do not prove worst-case stack safety.

The unbounded old ISR is a concrete defect and a plausible source of missed
Timer0 overflows. The logs alone do not prove it caused every backwards clock
sample or UI pause. Hardware must confirm ISR duration, software/hardware loss,
ACK reception, display responsiveness and actual clock stability after this fix.

## Bounded bus profiling and isolated observer (2026-11-12)

The [capture guide](BUS_PROFILING.md) covers production profiling, the separate
observer, normal/delayed AC startup and late-unit arrival. This patch instruments
the existing policy; it does not change negotiation silence, fan hysteresis,
charging permission, retry timing or EEPROM format.

Validation passes:

- Ordinary host suite: **3,357,481 checks**. With `PSU_TEST_BUS_PROFILE=1`:
  **3,482,883 checks**, including unchanged setting-write inhibition during
  negotiation, visibility of the existing INFO-query gap, serial reconnect,
  command handling during event storms, and actual production CSV row bounds.
  Both runs use AddressSanitizer/UndefinedBehaviorSanitizer.
- New observer/transport tests exercise the production MCP2515 RX callback:
  software-ring saturation beyond the old 255 counter, negotiation counted even
  when dropped, invalid frames, queue-age/micros rollover, register readback and
  overflow-bit preservation. Observer tests cover zero CAN sends, failed CAN
  initialization, AC-review expiry/rollover, newer OFF, malformed/abandoned input,
  serial backpressure and the actual setup/loop LOW-before-OUTPUT and parked
  TFT/touch chip selects. Maximum-width summary/event rows fit the UART reserve.
- Decoder tests cover truncated rows, mixed console output, queued events,
  counter rollover, reboot and unit-snapshot fields.
- Touch/simulator **1,310 checks**, real wheel **19,652 checks**, production
  renderer at 500 kHz / 1 / 2 / 4 / 8 MHz, route-search budgets and AVR format
  checks pass. No UI scenes changed; previews were not regenerated.
- All **22 Mega environments** build: the original 19 plus the three diagnostic
  environments. All three UI-debug isolation checks pass. A new linked-image
  check confirms the observer omits controller, EEPROM, UI and transmitter state;
  ordinary production builds omit the optional recorder. The CAN library's
  retained virtual transmit symbol alone is not evidence of an observer send;
  the observer adapter rejects sends and the host test verifies that boundary.

| Environment | Flash bytes | Static RAM | Static free |
| --- | ---: | ---: | ---: |
| `mega2560_wheel_alerts` | 200030 | 7155 | 1037 |
| `mega2560_wheel_profile` | 207524 | 7307 | 885 |
| `mega2560_serial_cutoff` | 119978 | 5285 | 2907 |
| `mega2560_serial_profile` | 127066 | 5437 | 2755 |
| `mega2560_can_observer` | 16054 | 1881 | 6311 |

Profiling adds **152 static bytes** to either supplied production variant.
Ordinary build RAM is unchanged; a brief `diag profile` reply explains when the
recorder is not compiled. No configuration snapshots or framebuffers were added.

The linked wheel-profile direct-call graph has a **767-byte** raw maximum in
the existing UI settings path. The new summary/event formatter measures 101
bytes and the per-unit row 138 bytes before callers, below existing deep paths.
The RX callback has a 14-byte frame / 20-byte direct chain. Accounting for the
interrupt wrapper, its indirect library handler/callback and hardware return
gives the same approximately **53-byte** reviewed interrupt overlap. Combining
the unpruned 767-byte graph with that overlap leaves 65 bytes before outgoing
arguments and other indirect-call allowances. The source/argument exclusions
described below still apply, but this remains a narrow, unmeasured margin.
Use `diag memory` after the loaded capture; do not infer physical stack safety
solely from compilation or these static estimates. The serial profiler and
observer provide substantially more room for comparison. Adding profiling to
the already tight nine-unit touch builds needs its own stack audit; those are
not supplied diagnostic environments.

Relevant commands:

```sh
python3 tools/test_host.py
PSU_TEST_BUS_PROFILE=1 python3 tools/test_host.py
python3 tools/test_bus_profile.py
python3 -B test/test_bus_log.py
python3 tools/test_touch.py
python3 tools/test_renderer.py
python3 tools/test_wheel_alert_guard.py
python3 tools/test_route_budget.py
python3 tools/check_avr_formats.py
python3 tools/check_memory.py
python3 tools/check_observer_build.py
python3 tools/check_debug_build.py
python3 tools/analyze_avr_stack.py .pio/build/mega2560_wheel_profile/firmware.elf
```

Instrumentation adds CPU/SPI/UART work and can itself affect timing. Counts of
diagnostic loss and CAN loss are separate. The requested hardware captures,
physical fan response, loaded receive-service timing and stack watermark remain
bench checks; no firmware was uploaded during software validation.

## Quiet background refresh logging (2026-11-12)

Successful routine voltage/current refresh ACKs are now omitted from the serial
trace queue unless `raw on` is selected. The filter uses the matched control
transaction's origin, before completion clears it. Startup, operator applies,
fan changes, thermal corrections, rejected/unexpected replies, timeouts and
periodic warning/error summaries retain their existing visibility. No CAN
requests, deadlines, alert acknowledgement rules or EEPROM fields changed.

Validation passes: **3,357,481 host checks** with ASan/UBSan, **1,310 touch/simulator
checks** (without exporting snapshots), AVR printf validation, route-search
budgets, all **19 Mega builds** and all three debug-isolation checks. The new
production-controller/console tests exercise nine units for a simulated minute
with UART backpressure, continued zero-hold refresh writes without trace backlog,
positive-current refreshes, operator results, fan transitions, thermal corrections,
raw tracing, unmatched/unknown ACKs, rejection and timeout fault reporting.

All profiles retain their previous static RAM use. The touch+wheel build uses
**7,385 RAM / 210,886 flash bytes**, leaving **807 static RAM bytes**; wheel-alerts
uses **7,155 / 199,944**, leaving **1,037 bytes**. The linked receive path is 150
bytes before callers, with its actual observer callback requiring only 26 bytes
before its caller; it remains below the previously reviewed 668-byte profile-load
peak. The raw main-path audit remains 754 bytes with the same loader-argument
constraints described below. Physical receive loss, fan speed and the bench stack
watermark remain unverified. See [serial soak guidance](SERIAL.md).

## Availability UI and consistent participation (2026-11-11)

The [startup guide](STARTUP.md) covers the three-column overview, four-row
checkbox editor, guarded Controller actions, serial aliases, persistence rules
and reboot/profile bench sequence. The [profile guide](PROFILES.md) describes the
shared participation validation and single final verified controller save.
[Generated previews](ui-preview.html) include empty commissioning, nine-row
scrolling, a second group page, reviews, results and alert-to-config navigation.

Confirmed exclusion now persists across a return until explicit inclusion.
Remembered inclusion clears legacy absence permissions; session-only choices
remain separate from saved policy. The existing schema 6, journal addresses and
profile format are unchanged. Remembered participation, including profile
restoration, activates only after verified persistence and final revalidation.

Software validation passes:

- **3,239,224** host checks with AddressSanitizer/UndefinedBehaviorSanitizer:
  scoped precedence and field isolation; unequal group sizes and rounding;
  invalid-setting rejection; distinct authorized voltages through startup,
  refresh, apply and restore; OFF priority, identity and thermal protections.
- All eight named slots, staged versus authorized values, address independence,
  identity reorder/mismatch, current-rating/deployment rejection, generations,
  interrupted writes, corruption, migration and calibration/protection ownership.
  Participation defaults to No, captures session choices when selected, and
  restores changing available units require OFF, settled availability and zero hold.
  A profile cannot release a new missing-hot-unit thermal contribution.
- Remembered and session-only inclusion/exclusion, old absence permissions,
  returning excluded units, unrelated UI saves and all eight profile slots through
  reboot. Failed or stale saves do not activate intent. Fault injection includes
  every participation-journal interruption, calibration/protection contention,
  newer OFF during the final profile save, expiry and group removal.
- Nine-member checkboxes, four-row wheel/touch scrolling, more than three groups,
  empty commissioning, all-excluded operation, draft cancellation, Remember
  retention and alert recovery preserving the focused row and draft. Confirmed
  participation, never an unconfirmed checkbox draft, is captured in profiles.
- **1,350** touch/simulator checks, including real hit-box/gesture paths through
  names, participation, Cancel-first reviews, serial changes invalidating a
  confirmation, an alert arriving during a named save, and OFF load persistence.
  The existing readiness, thermal, recovery and calibration suites still run.
- Production renderer with **79 exported screens** at 500 kHz, 1, 2, 4 and 8 MHz;
  **19,652** real ClickEncoder checks; route-search budgets; GPIO, fan, cutoff, AVR printf and debug isolation.
  The compact AVR debug storage adapter also passes its own sanitizer test for
  controller/eight-profile/calibration mappings, failures and protection.
- All **19 Mega profiles** build. The nine-unit instruction-level rendering run
  gives 88.6 ms group selection, 85.5 ms charger selection, 50.5 ms Tools selection
  and 764.2 ms page replacement with priority drawing. These model AVR computation
  and nominal SPI; they do not measure hardware CAN/display interrupt load.

The three AVR UI-debug builds use four simulated units (two groups, 3/1) to leave
runtime room for eight in-RAM profiles. A nine-unit touch-debug diagnostic build
used **8,196 static bytes**, already beyond the Mega's 8,192-byte RAM before stack;
this reduction addresses a physical limit, not the advisory 1 KiB reserve.
Production and host fixtures retain nine.
The compact debug store retains only the latest controller/profile copy, while
host tests use all 4 KiB and both journal copies for interruption tests. It is a
UI simulation adapter, not an EEPROM power-loss model. Calibration retains both
40-byte copies, and no physical CAN, EEPROM or power GPIO links into debug builds.

### Flash and static RAM

Bytes, release builds with the repository's PlatformIO settings. Free RAM below
1 KiB is advisory; actual RAM/flash overflow remains a failed check. This patch
adds 21 static bytes to the full UI build (11 to serial): bounded participation
save metadata and the interrupted-draft bookmark, with no extra scene buffer or
configuration copy. The preceding scoped-profile baseline was 7,364 static bytes
in the touch+wheel build; the current figure is 7,385.

| Mega profile | Flash | Static RAM | Static free |
| --- | ---: | ---: | ---: |
| `mega2560` | 199256 | 7152 | 1040 |
| `mega2560_serial` | 119706 | 5284 | 2908 |
| `mega2560_display` | 186478 | 7121 | 1071 |
| `mega2560_local` | 149332 | 6129 | 2063 |
| `mega2560_minimal` | 53650 | 4263 | 3929 |
| `mega2560_touch` | 209264 | 7354 | 838 |
| `mega2560_touch_wheel` | 210840 | 7385 | 807 |
| `mega2560_ui_debug` | 201688 | 6736 | 1456 |
| `mega2560_touch_debug` | 211534 | 6938 | 1254 |
| `mega2560_touch_local` | 155636 | 6331 | 1861 |
| `mega2560_touch_debug_local` | 156118 | 5940 | 2252 |
| `mega2560_touch_enable` | 209572 | 7354 | 838 |
| `mega2560_touch_fan` | 209968 | 7359 | 833 |
| `mega2560_serial_fan` | 120102 | 5289 | 2903 |
| `mega2560_serial_cutoff` | 119844 | 5285 | 2907 |
| `mega2560_touch_cutoff` | 210070 | 7360 | 832 |
| `mega2560_serial_alerts` | 119958 | 5286 | 2906 |
| `mega2560_touch_alerts` | 210348 | 7362 | 830 |
| `mega2560_wheel_alerts` | 199898 | 7155 | 1037 |

### AVR stack audit and remaining margin

`tools/analyze_avr_stack.py` audits the **linked** ELF, including saved registers,
fixed local frames and three-byte Mega return addresses. It flags indirect calls
and cycles; it is not a complete stack proof. In the largest production build
(`mega2560_touch_wheel`, 807 bytes statically free):

- The former EEPROM audit's 606-byte load frame plus 232-byte validation frame
  no longer coexist. Boot/serial restoration use one caller-owned 256-byte
  configuration candidate. `loadCandidate` may modify that temporary on failure;
  the caller checks the result before configuring the controller. Normal `load`
  retains its failure semantics. Legacy schema-1 validation remains a separate
  112-byte candidate. Boot storage loading is separated from CAN/display startup.
- Record selection shares the writer's buffer only while idle. Protection and
  absence persistence no longer duplicate a full RAM configuration. Job address
  **and epoch** guards live in one controller-owned array instead of every plan;
  both checks remain in dispatch/ACK validation.
- Source-reviewed direct-call estimates, including the `main` caller, are about
  **509 bytes** for boot/migration, **550 bytes** for the largest serial path (participation save),
  **545 bytes** for migration during protection persistence, and **668 bytes**
  for wheel-driven profile review/loading. Target arrays are released before
  application plans are allocated. UI dispatch keeps mutually exclusive
  alert/recovery/navigation/profile frames separate. Calibration and ordinary
  rendering paths remain below the reviewed profile-load peak. The participation
  backend entries measure 453 bytes for confirmation/save, 262 bytes for final
  revalidation and 266 bytes for preview, before their respective callers; the
  complete application paths remain below the 668-byte profile-load peak.
- The raw graph reports a larger load path because `newestConfig` can allocate
  its own candidate when called without one. Production `loadCandidate` and
  migrated protection pass their existing candidate, so that nested allocation
  is unreachable there. Boot's calibration action also cannot load a profile.
  These are source/argument constraints, not blanket exclusions from the audit.
- The direct graph omits the virtual EEPROM read callees and outgoing arguments.
  Allowing another dozen bytes for those on the 668-byte review path, the
  largest reviewed ISR path is approximately **53 bytes**, including the hardware return address, saved registers, indirect
  MCP2515 callback and callees. Encoder/Timer0/UART paths are smaller; these
  handlers do not enable nested interrupts. That leaves approximately **74 bytes**
  on this reviewed estimate. No reachable allocating `new`/`malloc`
  call was found in the linked production path; the linked allocator symbol alone
  does not imply a heap allocation.

This is a narrow margin, with uncertainty in indirect/library calls and future
compiler changes. It is not a claim of a measured worst-case hardware watermark.
`diag memory` reports static free, current stack/heap gap and the contiguous
untouched pattern since boot. Painting occurs in `.init3`, before constructors,
above application data. A matching pattern can overestimate untouched space, and
unexercised paths remain unknown. Stress boot, serial load/defaults, all profile
reviews/loads/saves, alert recovery, calibration and thermal-stop persistence
under CAN traffic; record the smallest watermark. Investigate near-zero results
before relying on the fullest touch+wheel build in operation. The user-approved
1 KiB reserve remains a warning rather than a reason to remove production features.

Reproduce the relevant checks:

```sh
python3 tools/test_host.py
python3 tools/test_touch.py
python3 tools/test_renderer.py
python3 tools/test_wheel_alert_guard.py
python3 tools/test_route_budget.py
python3 tools/test_sim_storage.py
python3 tools/test_alert_outputs.py
python3 tools/test_fan_output.py
python3 tools/test_charge_allow.py
python3 tools/check_avr_formats.py
# Build every env listed in platformio.ini before these ELF checks:
python3 tools/check_memory.py
python3 tools/check_debug_build.py
python3 tools/analyze_avr_stack.py .pio/build/mega2560_touch_wheel/firmware.elf
python3 tools/test_avr_render.py --simavr-prefix /path/to/extracted/usr
```

Actual PSU delivery, EEPROM interruption on the board, physical input/output
behavior and measured RAM margin remain bench checks. The sections below record
previous changes and their historical build measurements.

## Outline selection throughout the UI (2026-11-11)

Groups, charger cards, settings, alerts, availability choices, recovery controls,
buttons and calibration targets now use two-pixel outlines. The selected control
is cyan; other controls have dim outlines. Text and status colours stay unchanged
when focus moves. There are no filled selection backgrounds. Existing confirmation,
acknowledgement, editor, touch-hit and CAN/protection semantics are retained.

The production-renderer fixture measures these complete selection moves at 8 MHz:

| Selection | Former filled pixels / transfers | Outline pixels / transfers |
| --- | ---: | ---: |
| Group cards | 36,864 / 768 | 2,336 / 52 |
| Charger cards | 49,920 / 1,040 | 3,168 / 72 |
| Two small buttons | text-colour change: 56 transfers | 1,440 / 36 |

Those are transfer counts and pixel volumes, not hardware latency. Complete AVR
instruction-level runs with nine simulated PSUs and nominal SPI costs give:

| Change | Filled UI at `0d24d39` | Outline UI |
| --- | ---: | ---: |
| Group selection | 415.0 ms | 88.4 ms |
| Charger selection | 552.0 ms | 85.3 ms |
| Tools selection | 50.0 ms | 50.3 ms |
| Group-to-charger page | 602.5 ms | 766.2 ms |

Large selection moves improve substantially; small-button latency is essentially
unchanged in this model. Full-page replacement is **slower** than the previous
filled implementation and remains a limitation. It erases tracked old edges,
updates text and adds the new outlines. These figures exclude physical CAN,
encoder and serial interrupt costs; software results do not establish loaded-bus
latency or zero receive loss. No firmware upload was performed.

Validation includes:

- Independent final-pixel comparisons at 500 kHz, 1, 2, 4 and 8 MHz, including
  all **61** exported UI screens, glyphs under edges, overlapping controls, changing
  text during drawing, rapid focus reversals, outline removal/reordering, page
  replacement, large digits, time rollover and expensive preparation.
- Transfers retain the same 48/24/12-pixel caps, closed SPI transactions and
  cooperative budgets. Foreground CAN RX/thermal servicing is unchanged.
- Compact three-bit colours, arbitrary-width writes across byte boundaries,
  outline packing/capacity and at most one focused control in captured scenes.
- Existing controller/storage/serial tests (**1,874,546 checks**), touch/simulator
  workflows (**1,276 checks**), real wheel/alert input (**19,652 checks**),
  route-search budgets, all nineteen builds and debug isolation.
- Complete redraw AVR limits: group/charger selection below 125 ms, Tools below
  75 ms and page replacement below 900 ms in the model. The priority path must
  still beat one-slice scheduling by at least threefold.

Two scenes now occupy **1,130 bytes**, down from 1,170. Extra renderer state fits
within that saving: display builds use **28 fewer static RAM bytes**. Colour
packing and the outline list do not affect EEPROM or configuration migration.
The generated [UI preview](ui-preview.html) shows the new controls. Repeat the
[bench comparison](#ui-redraw-bench-comparison), including selection reversals and
full-page changes, and compare CAN receive-loss and timeout counters separately.

## Foreground redraw scheduling (2026-11-10)

The next nine-responder bench log confirms a **17,854 ms** completed redraw,
control/UI phase maxima of **37,292 / 30,496 us**, and software receive drops
rising **121 -> 217**. Optimizing individual strips and counting fewer searches
was insufficient: each small drawing slice still waited through a full control
pass. The new path gives foreground changes consecutive slices while servicing
CAN RX and temperature protection between them. There is no deliberate RX loss.

An AVR instruction-level regression executes the production controller, UI
composition and renderer on an emulated Mega at 16 MHz with nominal 8 MHz SPI
wire delays. Both modes use the same current controller code and nine simulated
PSUs; the comparison isolates scheduling, rather than comparing desktop CPUs:

| Complete redraw | One drawing slice per main loop | Priority window |
| --- | ---: | ---: |
| Group overview highlight | 3033.1 ms | 415.0 ms |
| Charger list highlight | 3805.8 ms | 552.0 ms |
| Tools highlight | 297.8 ms | 50.0 ms |
| Group overview to charger page | 4136.9 ms | 602.5 ms |

These include AVR computation and Timer0 interrupts. They exclude physical
CAN/display/encoder/serial interrupt costs and do not establish hardware latency
or zero receive loss. The existing host renderer independently compares final
pixels and transaction boundaries at all supported clocks. The optional AVR
test requires the existing PlatformIO packages plus `g++` and `libsimavr-dev`:

```sh
python3 tools/test_avr_render.py
# Extracted development/runtime packages can be used without system installation:
python3 tools/test_avr_render.py --simavr-prefix /path/to/extracted/usr
```

The regression requires each priority redraw to take less than one third of its
one-slice counterpart, Tools under 100 ms, and the other complete redraws under
900 ms in that model. Host tests additionally check the actual Application's
8 ms/twelve-extra-slice ceilings, rollover, single-slice periodic updates,
completion before telemetry recomposition, same-millisecond source freshness,
pending ACK processing, stale-limit ACK rejection, hot input/output sensors,
bounded receive batches, thermal OFF and normal scheduler ownership of writes.
Controller/storage/serial tests pass **1,874,450 checks**; touch/simulator tests
pass **1,112 checks**, both with snapshot exports. Generated UI previews are
unchanged. Production-renderer tests pass at all five clocks; wheel, GPIO,
route-cache and AVR-format checks also pass.

The supporting source cache reuses identity/lease validation without caching
temperature values, field freshness or write permissions. Text/style composition
uses contiguous fills, and configuration ranges use an exact member/rank bound
instead of repeated binary-search validation. All nineteen builds and debug
isolation pass. **Static RAM is unchanged** from `8cae35c`; the largest build
still leaves **1,032 bytes**. Layout, colours, 8 MHz SPI, EEPROM and the operator's
authorized current allocations retain their existing semantics.

On hardware, repeat the [bench comparison](#ui-redraw-bench-comparison) with all
nine PSUs and capture `change-ms`, phase maxima and receive-drop deltas. Include
navigation during an apply/ACK wait and OFF, then ordinary telemetry with no
input. A priority window is cooperative and is not an eight-millisecond CAN
blackout: interrupts and receive processing continue between slices.

## UI latency and repeated route lookups (2026-11-10)

The reported nine-slot/two-responder bench run had a historical **44,524 us**
main-loop maximum, **13,115 ms** scene maximum and **48** software receive drops.
The next snapshot retained those maxima/drop counts; it did not demonstrate new
receive loss during that particular highlight move. Its latest 206/216 ms scene
could be a later telemetry comparison, not the slow navigation scene.

- Repeated identity scans are shared by slot queries at the same timestamp and
  discovery revision. In the steady host fixture, controller plus idle-console
  searches drop from **166 + 47 to 9 + 0** with nine configured/two responding,
  and from **459 + 144 to 9 + 0** with all nine responding. UI composition reuses
  that snapshot without further searches. A later timestamp can resolve each
  slot again; identity changes immediately invalidate the cache. These are
  deterministic search counts, not AVR cycle measurements.
- Lifecycle tests cover same-millisecond discovery, fresh hot telemetry through
  a cached route, duplicate identity, rebinding/loading/unbinding, route expiry
  before the next tick, address negotiation and millis rollover. Thermal OFF
  remains latched and no positive-current write is introduced. Existing
  identity/ACK, readiness, thermal and interrupted-storage tests remain enabled.
- Discovery compares each fresh identity pair once. Unsolicited serial
  control/availability sampling runs at a 100 ms cadence; commands, ACKs, alerts
  and protection keep their existing scheduling.
- The renderer compares cells in eight-cell batches within the 750 us budget,
  removing the fixed 32-cell limit. An unchanged 390-cell scene needs one call
  instead of thirteen in the synthetic comparison-cost model. Solid strips use
  the colour-fill writer and blank fills skip glyph expansion. Transfer sizes,
  SPI transaction boundaries, filled highlights and the 8 MHz clock are retained.
- Full group/charger highlight moves are now tested, in addition to text-only
  selection changes. At 8 MHz a group move still needs **768 transfers** and a
  charger move **1,040**: filling those backgrounds really changes those pixels.
  The test completes them in 110/149 calls (86/117 ms of synthetic SPI timing).
  A Tools selection takes 56 transfers/eight calls. These values exclude real
  AVR computation, interrupt load and the application work between calls;
  they do **not** establish the physical latency target.
- Production-renderer tests at all five clocks verify final pixels, solid and
  mixed strips, rapid full-highlight reversals, mid-transfer changes, rollover,
  costly-preparation progress and observable budget overruns. Separate
  `change-ms` timing survives later telemetry refreshes. `diag ui reset` and
  control/storage/serial/UI phase maxima are tested through Application/serial.
- All nineteen Mega profiles, debug isolation and real wheel/fan/cutoff/alert/
  PSU_ACTIVE adapter regressions pass. Added static RAM is **50 bytes** in
  display+serial builds; the largest retains **1,032 bytes**. There is no new
  framebuffer, EEPROM schema change or charging-policy change.
- Controller/storage/serial tests pass **1,874,218 checks**; touch/simulator
  integration passes **1,091 checks**, both with snapshot exports. The generated
  previews retain the existing layout, colours, digits and filled highlights.

The [bench comparison](#ui-redraw-bench-comparison) remains required. Phase
maxima will locate remaining long work; an SPI maximum includes interrupt work
that may run as the transaction ends, so it is not a direct wire-time reading.

## Nine-unit capacity and compact persistence (2026-11-10)

- Default capacity is nine PSUs with eight groups. A shared 16-bit member mask
  covers control, authorization, ACK/results, availability, thermal state, serial,
  wheel/touch navigation and debug simulation. PSU 9 is exercised independently
  and alongside slots 1–8; zeroing/clearing a lower slot cannot truncate bit 8.
- Schema 5 uses 211 bytes per journal record. Tests load independently encoded
  schemas 2–4, preserve the original eight-slot count/profile/absence policy and
  thermal stop, then interrupt every byte of the first nine-slot save. The old
  or new complete record survives; both touch calibration tails remain intact.
  Name packing covers every allowed character, including letter case and NUL.
  CRC-valid malformed capacities, memberships and authorization bits are rejected.
- Ninth-unit tests cover rejected group writes, address changes/stale plans,
  all-unit thermal limiting, sensor loss/retry, durable thermal shutdown with
  unsaved drafts, dropout acknowledgement versus explicit absence acceptance,
  returned-member inclusion, sole-producer PSU_ACTIVE and OFF for all nine.
- All-unit DATA/INFO scheduling and manual-poll fairness tests now run with nine
  devices. Touch hit targets select PSU 9, toggle its individual availability
  checkbox and review/confirm only its absence. Wheel navigation reaches the
  ninth member on page five. Simulated fault injection waits for output readiness
  qualification after the final ACK before changing the scenario.
- The full nineteen-profile matrix and debug-isolation checks pass. The wheel
  build grows by 106 bytes; all profiles retain at least 1 KiB static headroom.
  The savings retain the twelve-entry discovery table (three spare entries at
  nine slots), pack seven cooperative control flags per PSU into one byte and
  reduce the saved-record buffer by five bytes. No CAN receive buffer was cut.
- Host/controller/storage tests pass **1,882,032 checks** with snapshot exports;
  touch/simulator tests pass **1,081 checks**. Production-renderer, real
  ClickEncoder and fan/cutoff/alert/PSU_ACTIVE GPIO regressions also pass.
  [Generated previews](ui-preview.html) include the ninth member's fifth page,
  touch details and individual absence review.
- `tools/check_memory.py` checks RAM including `.noinit` and flash in every Mega
  ELF. It rejects missing builds and static headroom below 1024 bytes. This does
  not measure stack high-water usage or establish real nine-PSU bus performance.

See [upgrading from eight slots](GETTING_STARTED.md#adding-psu-9-after-upgrading-an-eight-slot-controller).

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
- The synthetic three-row button **text-colour change** needs **28 transfers at 8 MHz**, versus
  **1,248** for the previous renderer redrawing all 78 cells as one-row strips.
  Both backgrounds remain filled in this case; it does not model moving a
  group's filled selection background. This is a transfer-count result, not
  measured Mega latency. Simulated wire timing excludes AVR raster/ISR costs.
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

The build matrix and size table below include nine-unit support and priority redraws.

```sh
python3 tools/test_host.py
python3 tools/test_route_budget.py
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
python3 tools/check_memory.py
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
| `mega2560` | 6899 | 170768 | 1293 |
| `mega2560_serial` | 5108 | 104796 | 3084 |
| `mega2560_display` | 6868 | 161880 | 1324 |
| `mega2560_local` | 5919 | 126690 | 2273 |
| `mega2560_minimal` | 4144 | 50402 | 4048 |
| `mega2560_touch` | 7101 | 179702 | 1091 |
| `mega2560_touch_wheel` | 7132 | 181148 | 1060 |
| `mega2560_ui_debug` | 6921 | 173600 | 1271 |
| `mega2560_touch_debug` | 7123 | 182520 | 1069 |
| `mega2560_touch_local` | 6121 | 132892 | 2071 |
| `mega2560_touch_debug_local` | 6143 | 133422 | 2049 |
| `mega2560_touch_enable` | 7101 | 179914 | 1091 |
| `mega2560_touch_fan` | 7106 | 180612 | 1086 |
| `mega2560_serial_fan` | 5113 | 105186 | 3079 |
| `mega2560_serial_cutoff` | 5109 | 104884 | 3083 |
| `mega2560_touch_cutoff` | 7107 | 180776 | 1085 |
| `mega2560_serial_alerts` | 5110 | 105070 | 3082 |
| `mega2560_touch_alerts` | 7109 | 180672 | 1083 |
| `mega2560_wheel_alerts` | 6902 | 171262 | 1290 |

Compared with `0d24d39`, outline rendering saves **28 static RAM bytes** in display builds. The wheel/alert build leaves **1,290 bytes** free;
the largest profile uses **7,132 bytes**, leaving **1,060 bytes** for stack/runtime
use. All profiles pass the existing 1 KiB static-headroom requirement.

Static RAM excludes stack/interrupt nesting. No full colour framebuffer is used;
two text/colour/outline scenes occupy 1130 bytes. Worst-case stack and loaded display/CAN
timing still require a hardware check. EEPROM remains within 512 bytes: two
256-byte slots with **211-byte schema-5 records**. Calibration still uses the
40-byte tails at 216 and 472. Schemas 2–4 remain readable with their profiles
intact. Review the [migration/downgrade limits](ALERTS.md#persistence-migration-and-limits).

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
Keep unused T_CS HIGH at 3.3 V if its SPI lines remain connected. CAN bitrate,
scheduling and the 1 MHz display read cap are unchanged by the capacity increase;
the EEPROM schema changes as described above.

## UI redraw bench comparison

Use the same Mega, level shifting, 8 MHz draw clock, PSU count, normal polling and
serial settings for each run. Record the baseline's highlight latency with a
video if it predates `diag ui`. The patch's timing counters start at boot; full
page changes can dominate the maximum. On the patched build let the page finish,
then `diag ui reset` clears timing maxima without resetting CAN counters. The
new `change-ms` pair retains the latest/maximum completed nonperiodic change
through later telemetry refreshes; `scene-ms(last/max)` includes those refreshes.

1. With the usual commissioned setup and CAN traffic running, send:
   ```text
   watch off
   raw off
   diag ui
   diag bus
   diag ui reset
   ```
   Omit `reset` on older firmware that does not support it.
2. Move one highlight, let it finish and capture `diag ui` / `diag bus`. Then
   turn between Tools menu choices for 30 seconds, including quick reversals.
   Check that cyan outlines move cleanly without fragments or overwritten text. Also exercise group-page changes,
   PSU details, and editing `54.10 V` / fractional current without applying unless
   intended. Use touch release/press and wheel clicks after each navigation.
3. While repeating highlights, sample `diag ui` and `diag bus`. Record a second
   bus snapshot after the run. Compare **counter increases**, especially
   `rx-drops`, `hw-overflow-events`, `tx-errors` and command results. Record
   `loop-max-us` and `phase-us control/storage/serial/ui` to locate slow phases;
   loop/receive high-water maxima are historical and are not reset by `diag ui reset`.
   Trace drops alone are diagnostic-output congestion, not CAN receive loss.
4. Repeat the same sequence on the patched firmware. Aim for ordinary menu
   highlight completion **below 100 ms at 8 MHz**. Measure group/charger outlines and full-page transitions separately; page
   replacement does substantially more work than moving focus. A phone
   video can corroborate input-to-highlight time; scene/change timings start
   after composition and end at completed drawing. Check that telemetry stays fresh and
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
| Latest/max completed nonperiodic change, ms | video / old scene reading | `change-ms` |
| Maximum control/storage/serial/UI phase, us | if available | `phase-us` |
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
- Two- and nine-unit applies; global voltage before current; explicit offline
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
- All eight groups and nine identities/ratings, draft and operating values round-trip;
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
  211-byte records/calibration tails, normal-save/calibration contention, failed
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
coverage are retained through schema-5 migration.

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
  already-qualified group remains configurable. The nine-unit job and simulator
  complete; startup timers also work across `millis()` wrap.
- Serial and the PSU detail UI distinguish acceptance, verification and measured
  output. Progress remains intact through a throttled UART; the group card shows
  `VERIFY`, and the controller does not report running before the first write.

The new loaded bench check remains outstanding: flash `mega2560_serial`, apply
the intended test limits, inspect `diag control` / `diag psu 1` for qualification
followed by `current-state=active`, and independently check delivered voltage and
current. Transient ready pulses must not produce repeated zero/wake sequences.
Then request OFF and confirm zero hold. The fan policy was not changed.

## Nine-PSU commissioning bench check

1. On the old build, export `config`, request `autoresume off`, verify zero-current
   hold, `save`, and wait for `EEPROM OK`. Keep the enable switch OFF. Upgrade
   without `defaults`; compare the eight bindings, groups and profile with the export.
2. Set `count 9`, discover and bind the new identity, then add it to its intended
   source group. Check `diag bus`, `diag group NAME` and `diag psu 9`; all nine
   identities should be verified, unambiguous and broadcasting. Save and reboot
   while OFF; confirm slot 9, memberships and touch calibration survive.
3. Review `preview all`, explicitly enable and apply a suitable low-current
   profile to the intended loads. Check ACKs and effective/acknowledged limits
   for PSU 9. Meter the actual output; a software ACK is not physical proof.
4. Request `autoresume off`. Confirm zero-current hold for every reachable PSU,
   especially slot 9, and PSU_ACTIVE LOW. With appropriate isolation, remove
   the ninth source, check its alert, and verify Acknowledge does not accept it.
   Review/confirm only slot 9's absence; the remaining allocations must not grow.
5. Restore that source. Verify identity reconciliation at zero and the explicit
   inclusion review. Check group totals, device details and touch/wheel paging.
6. Scroll while running `diag ui` and `diag bus` under all-nine traffic. Receive
   loss/overflow, command timeout and read-failure counters should not grow.
   Confirm telemetry/identity freshness and observe actual display response.
   Software tests do not replace this physical bus/load check.
