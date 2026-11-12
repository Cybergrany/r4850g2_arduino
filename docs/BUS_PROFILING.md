# CAN startup and fan captures

These builds investigate startup, address negotiation, receive loss and fan mode
changes. Profiling itself does not change the controller's charging policy. Use the
same source revision for all captures and keep the original terminal logs.

## Receive/timing patch comparison

The MCP2515 adapter now owns reception: one interrupt reads a snapshot of at most
two hardware buffers, using one 14-byte SPI burst per buffer. It no longer calls
the library's drain-until-empty interrupt handler. The software ring remains 15
usable frames. Main-loop receive passes also run between control, storage,
serial and display work. Routine bulk reads wait while that ring is backlogged
or the existing address-negotiation quiet interval is active. Protective setting
commands retain their priority, identity validation and existing deadlines.

The burst uses MCP2515 `READ RX BUFFER`; raising CS clears only the selected
buffer's receive flag. See [Microchip DS20001801K, section 12.4](https://ww1.microchip.com/downloads/aemDocuments/documents/APID/ProductDocuments/DataSheets/MCP2515-Family-Data-Sheet-DS20001801K.pdf).
Every SPI transaction finishes before returning to other work. No new framebuffer,
larger software queue, deliberate CAN loss or startup delay was added.

For a nine-PSU comparison, use the same normal-startup setup and run at least five
minutes, then exercise menu navigation and the previously failing scoped apply.
Record `diag bus`, `diag ui`, `diag memory` before and after. Keep `watch off` and
`raw off`; use the profile build for a second run when individual traffic events
are needed. Retain both the successful and unsuccessful captures.

`diag bus` adds `rx-isr-max-us`, `clock-invalid-samples` and `max-backstep-us`.
The first is the measured maximum inside our bounded receive handler (excluding
the external-interrupt wrapper). The clock counter counts invalid **short timing
measurements**, not unique hardware events. Ordinary 32-bit `micros()` rollover
is valid; a backwards sample ends a cooperative render slice and is counted
instead of turning into a roughly 4.29-billion-microsecond maximum. These counters
are sticky until reboot, including across `diag ui reset` and serial reconnect.
They do not change the millisecond clock or safety/control deadlines.

Expected evidence is no increasing receive loss, no clock-invalid samples, a
bounded ISR well below Timer0's 1,024 us overflow interval, responsive navigation,
and no lost INFO/setting acknowledgements. Existing receive-loss counters may
saturate, so compare a fresh boot and the profile's wider counter when needed.
Nonzero counters, long ISR measurements, or resumed stalls still need diagnosis;
the patch does not suppress their alerts. Software tests establish bounded work
and state handling, not physical bus timing, fan response or PSU regulation.

## Readiness restart limit check

The controller now permits two automatic zero/wake/restores after a qualified
output loses readiness, then latches `readiness restart limit reached` on the
third loss. This bounds the observed repeating sequence; it does not diagnose or
fix the electrical reason a PSU is losing readiness. Normal zero-hold status
changes and the initial bounded readiness qualification are separate paths.

During the controlled bench run, capture `diag control` and `alerts` if the known
ready/not-ready cycling returns. `readiness-losses` must stop at 3 with the
default budget of 2; automatic voltage/current/wake restoration for that PSU
must stop. The existing bounded standby/cutoff fallback and cooling remain in
force. Healthy units retain their own state unless the shared AC cutoff trips.

1. Acknowledge the incident in the UI (or `alerts ack <id>`). Its ERROR condition
   and restart count must remain; no retry or setting write is authorized by ACK.
2. Request `autoresume off`, inspect `diag control` and correct the bench cause.
   Record `diag bus`, `diag ui` and `diag memory` before retrying.
3. Open the incident's **Details → Retry ALL…** review. Cancel is initially
   selected. Cancelling leaves the fault and counters unchanged.
4. After inspecting the cause, explicitly confirm Retry ALL or use serial
   `reconcile`. It affects every managed PSU, resets the session counters and
   reconciles at zero. OFF remains OFF; ON can restore the authorized profile
   after the normal identity, thermal and availability checks.

Do not use reboot as the routine recovery test: the counts are deliberately
session state. OFF/ON, address reacquisition and loading the same identities must
not clear them. Keep the full capture if the budget is exceeded without a fault,
or the controller still hangs after the receive/timing patch.

## Choose the build

| Environment | Behaviour |
| --- | --- |
| `mega2560_wheel_profile` | Current wheel/display installation, with bounded profiling. ENABLE_IN D26, CHG_ALLOW D27, WARNING D28, ERROR D29, PSU_ACTIVE D30. |
| `mega2560_serial_profile` | Production control and profiling, CHG_ALLOW D27; no display, wheel or enable switch. Useful as a second comparison, not a substitute for the loaded UI run. |
| `mega2560_can_observer` | Separate application: receives CAN, sends **no application CAN frames**, reads/writes **no EEPROM**, ignores ENABLE_IN, and releases D27 only after a serial review. |

The first two set `PSU_BUS_PROFILE=1`. Ordinary builds compile the instrumentation
out. The observer additionally sets `PSU_CAN_OBSERVER=1` and has a restricted
source list: no controller, persistence, UI, or CAN setting scheduler is linked.
Use the supplied PlatformIO observer environment rather than enabling that flag
in an arbitrary Arduino build.

All three use the existing MCP2515 wiring: CS D10, INT D2, Mega SPI D50/D51/D52,
125 kbit/s, **8 MHz MCP crystal**. The Mega must remain powered upstream of the
AC contactor. D27 is an active-HIGH command, not contactor-position feedback;
hardware must hold it LOW during reset. Observer setup writes LOW before OUTPUT
and holds D28/D29/D30 LOW. It parks TFT CS D22 and touch CS D25 HIGH so the
installed peripherals cannot drive shared MISO. The screen is not initialized.

The observer uses normal MCP2515 receive mode, including automatic CAN-level
acknowledgement. This lets a single PSU transmit without a second CAN node.
It is **application-silent**, not electrically listen-only. It never calls the
application transmit API; the adapter's `send()` also returns false in this build.
It does not poll, answer negotiation, set fans, enforce zero current, or assert
the ordinary protection outputs. PSU offline/default/persistent settings still
apply. **Disconnect batteries and other DC loads for these observer captures.**
Do not use it as a charging controller. Its lack of EEPROM access preserves the
previous production configuration, which takes effect again when production
firmware is restored, including any saved enable preference.

## Capture setup

For the requested fan/startup comparison, leave DC loads disconnected in all
runs. Before the production runs, turn the physical enable switch OFF and use
`autoresume off`, then `save`; wait for successful persistence. Do not change
bindings, groups or participation during the captures. This deliberately saves
an OFF boot preference; restore your desired preference after testing.

Build/upload the wheel profile and open the monitor with logging:

```sh
pio run -e mega2560_wheel_profile -t upload --upload-port COM15
pio device monitor -e mega2560_wheel_profile --port COM15 -f log2file
```

Use 115200 baud, 8-N-1, no flow control, local echo off. Existing monitor settings
keep DTR/RTS inactive. Capture from reset with the terminal already open where
possible. A USB bridge may still reset on reconnect; distinguish that from a
continuous session. `hello` or Ctrl-X resets console input, not profile counters
or alert acknowledgement. Logging starts automatically in a profiling build.

Send commands **one at a time**, waiting for their output; the production console
intentionally rejects pasted backlogs rather than executing a later surprise.

```text
watch off
raw off
busprofile on
busprofile mark 100
```

Avoid `discover`, `poll`, `describe`, manual fan commands, setting applies and
reconciliation during the initial passive observation: those change bus traffic
or controller state. Built-in background control remains active in this build.
Normal successful refresh ACKs remain quiet, but the explicitly enabled BUS
stream includes setting attempts and received ACKs for correlation.

At the end of each production run, send these individually:

```text
diag profile
diag startup
diag control
diag bus
diag ui
diag memory
```

`diag profile` takes a summary immediately; it does not reset counters or issue
a PSU request. Other diagnostics show the ordinary control state and timing.
If another unit's detailed readings are needed, use `telemetry all` after the
initial soak. Let each multi-line response finish before sending another command.

## Hardware runs needed

1. **Production, normal startup:** all PSU AC feeds available, then power/reset
   the Mega normally with CHG_ALLOW controlling their common AC. Capture at least
   **five minutes** without intervention. Record when fans become quiet, stay
   loud, or become loud again. A missing configuration deliberately keeps D27
   LOW: commission/save first, rather than treating that as a startup fault.
2. **Production, delayed PSU AC:** same profile and configuration, but hold PSU
   AC off while the Mega boots; make AC available about **15 seconds later**
   using the existing independent AC inhibit/feed controls. Do not bypass D27
   or use a firmware retry as the delay. Capture another five minutes. If the
   wiring cannot independently delay AC, skip this run and say so.
3. **Observer:** flash the environment below with PSU AC inhibited and DC loads
   disconnected. Start logging, reset the Mega, then release AC explicitly as
   shown. Observe five minutes. If practical, keep one PSU/group's AC off at
   first and switch it on **60 seconds later**, marking that step. This exposes
   late negotiation with no requests from our controller. An additional observer
   run with all units starting together is useful if the late-start run differs.

```sh
pio run -e mega2560_can_observer -t upload --upload-port COM15
pio device monitor -e mega2560_can_observer --port COM15 -f log2file
```

Observer commands, each entered separately:

```text
status
mark 100
ac on
```

Read the displayed warning that PSU defaults may energize DC. To release D27:

```text
confirm yes
```

The review expires after 15 seconds. `confirm no` cancels it. `ac off` always
requests LOW, including with serial-output backpressure; it also cancels an old
review. `hello`/Ctrl-X do not change an already released AC command. An observer
with failed CAN initialization cannot release D27. `status` reports the commanded
AC level, not physical feedback. `profile off` / `profile on` control streaming
without resetting counters. The observer does not provide production `diag`,
configuration or fan commands.

Use these markers in either build (production prefix `busprofile mark`, observer
prefix `mark`). If a marker cannot enter a full event queue, the command echo
still records your input and the event-loss counter increases.

| Marker | Physical observation/action |
| --- | --- |
| 100 | Start of the observed run |
| 200 | Fans audibly high; note which units/groups |
| 201 | Fans audibly quiet |
| 300 | Immediately before manually removing a unit/group's AC |
| 301 | Immediately after restoring that AC |
| 400 | End of soak, before final diagnostics |

End the observer run with `mark 400`, `status`, then `ac off`. Return the original
logs, build environment, which AC feeds were present/delayed, and the physical
fan observations. One normal and one abnormal production capture are especially
useful. Do not filter out alerts or the final diagnostics.

## What the capture means

Three bounded record types are interleaved with the ordinary console:

- `BUS S,...`: about once a second; transport counters, protocol counts, error
  registers and maxima. They expose receive loss separately from log loss.
- `BUS U,...`: production only, one configured slot per second in rotation
  (nine seconds for nine slots). Current fan request/ACK, last PSU status and
  age, freshness-qualified temperatures and V/A, authorized/limited current,
  last acknowledged wire limit and authorization flags. Snapshots complement
  event records when bursts exceed the event queue.
- `BUS E t=...`: negotiation start/quiet, identity or status change, readiness,
  route reset, control phase, fan-policy change, setting attempt/ACK/failure,
  permit/enable change, fault or operator marker. Production records the first
  negotiation identity per episode; the observer records every processed
  negotiation message, including `30-01` versus `30-02`.

Decode on a computer, keeping the raw log as the primary evidence:

```sh
python tools/analyze_bus_profile.py device-monitor.log
python tools/analyze_bus_profile.py --events normal.log abnormal.log observer.log
```

The analyzer distinguishes summary timestamp rollover from reboot and reports
malformed/truncated rows. Event rows can appear after a newer summary because
they are queued. A printed marker is controller processing time, not the exact
physical switching time.

The fan command `0x0134` encodes AUTO/MAX in **byte 3**; a legacy `raw=0` tail
does not identify that mode. BUS events retain the correct byte. Status register
`0x0183` bit 10 is shown as the related Huawei protocol's fixed-full-speed mode
bit. Treat it as firmware-dependent status evidence, **not measured RPM**; do
not interpret arbitrary nonzero status words as faults. A fan that stays fast
with fresh AUTO status may be following internal PSU logic or a retained PSU
setting; the observer itself does not change that setting.

The current controller invalidates routes during observed negotiation and gates
setting writes, but its background/manual read scheduling is not universally
silent. The profiler deliberately preserves that behaviour and counts attempted
TX during the known negotiation interval. It also leaves the existing startup
AC release and enable policy unchanged. `negotiation-end` means the existing
three-second **observed-message quiet interval** elapsed; it is not independent
proof that every PSU finished booting. A dropped negotiation frame can prevent
the controller from knowing negotiation is active. Compare ISR negotiation
counts, processed counts and RX losses before drawing conclusions.

Protocol context: [software address negotiation](https://github.com/patagonaa/huawei-r48xx#software-address-negotiation)
and [register setting](https://github.com/patagonaa/huawei-r48xx#80-register-set-request).
Related Huawei status documentation and differences between PSU variants are
linked in [PARITY.md](PARITY.md). The captures will inform a subsequent startup
policy patch; software tests alone do not establish PSU physical response.

## Bounds, units and raw format (BUS v1)

There is no heap allocation, raw-frame archive or additional UI framebuffer.
Production retains four 12-byte events; the observer retains 24 plus twelve
small address/identity/status observations. Event overflow drops the new event
and increments `event_drop`, independently of CAN receive loss. Streaming OFF
continues counting and can fill the event queue; turning it back ON may first
print older events. Neither reconnect nor `hello` resets these counters.

One bounded row is enqueued per eligible console turn. Commands, operation
results and alert notifications precede profile output; profile turns alternate
with ordinary background console work. Typing and multi-line views suspend
automatic rows. UART writes remain bounded by writable capacity; no logging is
done in the CAN ISR. Formatting, counting and three SPI register reads per
summary still add measurable overhead. Compare `diag bus`, `diag ui` and
`diag memory`; this is not a zero-perturbation wire analyzer.

Summary fields, in order after `BUS S,`:

```text
ms,rx,rx_drop,invalid,hw_overflow,rx_high_water,service_gap_us,queue_age_ms,
neg_isr,tx_info,tx_data,tx_describe,tx_setting,tx_ok,tx_fail,tx_in_neg,
neg_rx,info_rx,data_rx,broadcast_rx,ack_rx,other_rx,event_drop,eflg,tec,rec
```

- `rx`, `rx_drop`, `invalid`: 32-bit counters. `rx` counts adapter-drained
  packets before validation. `invalid` counts non-8-byte/standard/RTR frames;
  `rx_drop` counts valid frames lost from the software ring (15 usable slots).
- Protocol/TX counters: 16-bit modulo counters. Counts alone cannot recover
  multiple wraps across a long log gap. `tx_ok` means transport completion,
  not PSU acceptance; `tx_in_neg` counts attempts while firmware knew negotiation
  was active, not every attempt during actual PSU booting.
- `hw_overflow`: saturating 16-bit count of observed MCP overflow events, not an
  exact number of lost frames. `event_drop` is saturating diagnostic loss.
- `service_gap_us`: maximum valid short interval between calls to receive, including
  empty calls. `queue_age_ms`: maximum enqueue-to-dequeue age, using 16-bit
  timestamps; it saturates at 65535 after a measured service gap beyond that
  range. These are not oscilloscope timing or true wire utilization. Wrap-aware
  timing assumes receive is called within half a `micros()` wrap (~35.8 min).
  Invalid short-clock samples are counted by `diag bus`, not used as a maximum.
- `eflg`, `tec`, `rec`: instantaneous MCP2515 error registers, decimal. Short
  faults may be missed; RX overflow latches are separately counted and cleared
  by the existing driver. An uninitialized transport reports zero registers;
  check the boot/ordinary CAN initialization result before treating that as healthy.

Unit fields, in order after `BUS U,`:

```text
ms,slot,address,identity,issue,phase,fan_requested,fan_ack,fan_reason,status,
status_age_ms,response_age_ms,broadcast_age_ms,input_c,output_c,output_v,output_a,
input_v,authorized_ca,limited_ca,ack_raw,authorized_cv,flags
```

`-` means unavailable. Address zero means no usable identity route. Fan enums:
0 UNKNOWN, 1 AUTO, 2 MAX. Issue/phase/reason enums are defined in
`ControllerTypes.h` / `PsuControl.h`; the event lines also render their names.
Temperatures, V and A are engineering units. `authorized_ca`, `limited_ca` and
`authorized_cv` are hundredths; `ack_raw` is the wire's fraction-of-rated-current
encoding, with 1024 = the commissioned current rating. Limited current reflects
thermal limiting of authorization, **not permission to run while OFF**. `flags`
bits: 0 enable ON, 1 D27 permit allowed, 2 negotiation known active, 3 zero hold
confirmed, 4 awake confirmed, 5 authorized voltage synchronized, 6 current
synchronized. Old acknowledged values are not physical measurements.

For setting events, `detail & 255` is the register; `detail >> 8` is TX purpose
(0 background, 1 apply job, 2 manual read, 3 control) or RX reply code
(1 accepted, 0x21 rejected). Phase detail packs old/new phase; route detail packs
issue/address and its value is the new epoch. Fan detail packs mode/reason;
value packs signed output centidegrees in the high half and output centiamps
in the low half, with -32768 / 65535 denoting unknown. Permit value is the
production `PermitState` enum, but observer permit events use 0/1 for LOW/HIGH;
boot event detail distinguishes production (0) from observer (1).
