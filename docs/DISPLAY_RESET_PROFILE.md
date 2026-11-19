# Manual TFT recovery: verification and bench checks

2026-11-19. Production implementation based on `5b29369`, measured with
`mega2560_wheel_alerts`. The ten-second wheel gesture and `display reset` share
the same recovery path. Nothing in recovery enables charging or applies edits.

## Recovery behavior

A cooperative TFT-only reset keeps control and protection running. It uses no
CAN receive suppression, timeout suspension, zero-current write, standby write,
controller reconfiguration or AC-permit transition.

Calling the boot initializer in the foreground would be unsuitable: it has
about 700 ms of blocking delays and a bulk clear, and repeats shared SPI
initialization. At the fixture's roughly 150 received frames/s, 700 ms represents
about 105 frames against 15 usable firmware queue entries. Retaining interrupts
alone would not keep that queue serviced.

Recovery advances reset-pin waits and the boot register sequence from the main
loop, clears GRAM in bounded SPI transfers, then repaints the current view. CAN,
thermal protection, ENABLE_IN, serial and the EEPROM writer continue normally.
Transactions always end between strips. The existing 750 us cooperative budget
and clock-dependent transfer ceiling bound clear work; neither is a hard
real-time guarantee. `diag ui` includes initialization/clear work in render and
transaction maxima, and reports `recovering=1` until repaint finishes.

## Input and retained state

- Ordinary long-press actions now occur on release, after approximately 1.2 s.
  Reaching ten seconds instead requests one reset, without first opening Config,
  leaving an editor or dismissing a popup.
- Holding longer, rotating while held, queued button events and release bounce
  cannot apply a setting, confirm a review or acknowledge an alert. After
  recovery, button actions require a stable release and a fresh press.
- The independent physical hold timer still works when a popup interrupts
  ordinary input. A serial reset consumes any already-held wheel gesture too.
- Drafts and background operation/save ownership remain intact. Confirmation
  expiry, configuration revisions and safety guards still apply; an old review
  can legitimately require refreshing after recovery.
- Touch contact in progress is discarded. Calibration retains completed targets
  but requires fresh contact for its current target. Background saves continue.
- Adafruit's `setAddrWindow` has function-static bounds that survive a hardware
  reset. Recovery primes a different window, forcing both axes on the first
  clear strip. Renderer cell, partial-glyph and outline caches are invalidated;
  clearing all 76,800 pixels also removes arbitrary damage in blank gutters.
- Initialization commands match the pinned Adafruit ILI9341 1.6.3 boot sequence,
  with the configured landscape rotation restored explicitly. TFT RESET must be
  connected (D24 by default). This does not power-cycle the panel, reset the
  Mega, reload settings or establish the cause of the original corruption.

## Actual AVR execution with simulated peripherals

`tools/profile_display_reset.py` executes the compiled Mega ELF at 16 MHz in
simavr. It models an MCP2515 with two receive buffers and nine Huawei peers:
three groups, each unit authorized for 54 V / 10 A, INFO replies, bursts of
thirteen DATA replies, 300 ms broadcasts, setting ACKs and the normal current
synchronization sequence. Received frames are serialized at 1.3 ms per frame.
The real application, CAN ISR, UART, Timer0 and EEPROM code execute.

Two simulator limitations are handled explicitly:

- simavr 1.6 defaults to 100 us per SPI byte, independently of the divider. The
  fixture replaces SPDR callbacks with transfers timed from SPCR/SPSR.
- Its Timer1 does not advance in TimerOne's phase/frequency-correct mode. The
  fixture supplies the configured 1 ms overflow; the real TimerOne ISR and
  ClickEncoder still execute. It checks the configured timer mode/period and
  executed encoder-call count, rejecting a missing or doubled cadence.

This models instruction/interrupt execution and byte timing, not electrical
waveforms, display damage or PSU output dynamics. TX completion and peer
responses are simplified. Physical output continuity remains a bench check.

| Scenario | Required result |
|---|---|
| Active operation without reset | Baseline; nine authorized limits retained |
| Reset while all nine are active | No zero/standby writes or CHG_ALLOW/PSU_ACTIVE falls; no command failure |
| Current change 10 A to 11 A; first ACK delayed 600 ms | Recovery starts with ACK pending; all changes finish without a false timeout |
| Same change, first ACK withheld | Genuine timeout and command-failure alert remain visible |
| ENABLE_IN OFF during reset-pin wait | Normal zero-current stop and PSU_ACTIVE transition |
| Input-triggered 76 C overtemperature during reset | Normal stop; thermal OFF marker verified in EEPROM |
| Ordinary save during reset | Verified completion; stored targets and enable preference retained |
| Physical ten-second hold, kept down for twenty seconds, then release bounce | Exactly one reset; no ordinary hold action or new current target |
| Physical navigation to an editor, unsaved current change, then reset hold | Draft survives; release does not apply it |

All nine scenarios passed, with zero software receive drops, hardware overflows
or invalid clock samples. CAN and TFT chip selects never overlapped. The fixture
also checks the actual initialization-command hash, restored MADCTL, and every
clear-pixel address. The ordinary active reset received 906 frames during
recovery; no receive blackout is introduced.

Observed during recovery, across these scenarios:

- Longest foreground CAN receive gap: **13.695 ms**.
- Longest TFT chip-select interval: **367 us**.
- Longest interval between application ticks: **53.766 ms**; the no-reset
  comparison reached 57.635 ms in its observation window.
- Full reset/clear/repaint: **4.796–5.792 s**; ordinary active recovery took
  **5.698 s**. The 700 ms of initialization waits are only part of visible
  recovery, and are themselves cooperative.
- Switch OFF during recovery: first zero write after **247 ms**, all nine zero
  by **2.597 s**. Overtemperature: **242 ms** and **2.564 s**, respectively.

These are measured simulator maxima, not upper bounds for all hardware, traffic
phases or menus. `diag ui` scene/change timings describe repaint; they are not
the total hardware-reset duration.

## Memory and regression checks

| Mega wheel build | Flash | Static RAM | Static free |
|---|---:|---:|---:|
| Baseline | 217,290 B | 7,344 B | 848 B |
| Manual recovery | 219,590 B | 7,367 B | 825 B |

The patch adds **23 B static RAM** and **2,300 B flash**. The observed runtime
stack-to-static separation was at least **313 B** in these runs, including
interrupt execution. It adds no configuration copies, framebuffer or heap
allocations. The existing static analyzer still
finds a much deeper configuration-load path: 745 B direct-call estimate from
main, before unresolved calls and interrupt overlap. Display recovery does not
enter that path. The advisory 1 KiB reserve remains a warning, not a guarantee;
observed simulation stack space does not prove worst-case safety for every path.

Passed regression checks:

- Controller/storage/UI host suite: 3,418,246 checks with ASan/UBSan.
- Touch/simulator integration: 1,418 checks, including drafts, verified save
  ownership, acknowledgement independence and partial calibration contact.
- Real ClickEncoder/TimerOne/GPIO host test: 248,171 checks, including short and
  long presses, exactly-once reset, release bounce, interrupted holds, rotation
  while pressed, serial reset during a hold and timer rollover.
- Production raster tests at 500 kHz, 1, 2, 4 and 8 MHz: complete pixel comparison
  after arbitrary corruption, partial drawing, changing scenes, repeated and
  duplicate reset requests, and millis rollover.
- AVR formatting, all 21 shipped Mega profiles, physical RAM/flash limits,
  UI-debug and observer isolation, and linked AVR stack analysis.

Reproduce:

```sh
PLATFORMIO_CORE_DIR=/home/dave/psu-can-workspace/.platformio pio run -e mega2560_wheel_alerts
python3 tools/profile_display_reset.py --simavr-prefix /tmp/psu-simavr-1119/root/usr --output /tmp/psu-display-reset-check
python3 tools/test_host.py
python3 tools/test_renderer.py
python3 tools/test_touch.py
python3 tools/test_wheel_alert_guard.py
python3 tools/check_avr_formats.py
python3 tools/analyze_avr_stack.py .pio/build/mega2560_wheel_alerts/firmware.elf
```

The example simulator prefix contains locally extracted `libsimavr-dev` and
`libsimavr2` packages, without a system installation. With a system installation,
omit `--simavr-prefix`. Build every environment before running `check_memory.py`,
`check_debug_build.py` and `check_observer_build.py`.

## Bench sequence

1. With ordinary CAN traffic running, capture `diag bus`, `diag ui`, `diag memory`
   and `diag control`. Use an already validated test current for an active test.
2. Enter a voltage/current editor and adjust its draft without confirming.
   Hold the wheel for ten seconds, until recovery starts; keep it held through
   repaint, then release. The editor should retain its draft. There must be no
   apply/save result or acknowledgement caused by that release. A new short
   press still follows the normal apply/review path.
3. Repeat on the Groups screen and on an alert. Neither a shorter hold action
   nor acknowledgement should occur before/after the reset. Also try
   `display reset`; a duplicate while recovering reports unavailable/busy.
4. Capture the four diagnostics again. Compare receive-loss and timeout counters,
   CHG_ALLOW/PSU_ACTIVE and an external current measurement throughout recovery.
   `diag ui` uses `UI=1` for boot availability and `recovering=1` while busy.
   `diag ui reset` only clears timing statistics; it does not reset the panel.
5. Repeat with ENABLE_IN switched OFF during recovery: the ordinary stop must
   still occur. Confirm the display returns to the configured orientation and
   clean layout, including blank areas and selection outlines.

Software tests support this bench test; they do not establish uninterrupted
physical output or diagnose why the original display corruption occurred.
