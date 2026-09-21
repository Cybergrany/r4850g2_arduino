# Reference comparison

Compared directly against these cloned revisions:

- Fork baseline: [`143f197a059b41c80dd57dcc865ebd946883b3e9`](https://github.com/Cybergrany/r4850g2_arduino/tree/143f197a059b41c80dd57dcc865ebd946883b3e9)
- Craig Peacock reference: [`6d8ff660f8d5ddd551958cc9593864bf21154ed7`](https://github.com/craigpeacock/Huawei_R4850G2_CAN/blob/6d8ff660f8d5ddd551958cc9593864bf21154ed7/r4850.c)
- Address-field cross-check: [BotoX `huawei.cpp` at `c0ec00d`](https://github.com/BotoX/huawei-r48xx-esp32/blob/c0ec00d52801b07be695284bff9c330266c932a9/src/huawei.cpp)
- Addressing and final-description-frame clarification: [patagonaa's protocol documentation](https://github.com/patagonaa/huawei-r48xx#can-protocol), including the author's own hardware investigations.

The executable C code was used as the parity baseline: its current scaling is
newer than its README examples and the spreadsheet. The original Arduino sketch
used an older fixed current multiplier.

| Reference capability | Fork baseline | Modular firmware |
| --- | --- | --- |
| 125 kbit/s extended CAN | Present | Preserved, board config separate |
| Periodic telemetry request | RTR frame; paused during menu | Correct eight-byte data frame; UI-independent polling |
| Input V, A, W, Hz and temperature | Present | Per PSU, validity and last-seen tracking |
| Output V, A, W and temperature | Present | Per PSU; also retains filtered `0x82` current separately |
| Efficiency | Decoded as fraction | Fraction internally, percent in serial status |
| Maximum available current `0x76` | Raw / 20 | Raw / 1024 times per-PSU rated current |
| Online voltage/current writes | Present, fixed current multiplier | Present, voltage x1024; current / rated current x1024 |
| Offline voltage/current writes | Present | Separate setpoints and explicit offline/persist operations |
| `-s` writes online and offline | Local Persist wrote offline only | `persist` queues both pairs; `offline` writes only defaults |
| Setting ACK including rejection and value | Logged status from interrupt | Decoded values on serial; matched command outcomes in PSU state |
| OVP acknowledgement decoding | Label only | Incoming OVP ACK decoded as volts; no new OVP setter |
| Ah estimate from `1001117E` | Ignored | Per-PSU session estimate; same /20 and 377 ms assumption |
| ASCII description fragments | Ignored | Streamed via `describe`, including final `..D27E` fragment |
| Unknown-frame diagnostics | Interrupt-context error logging | Optional raw trace and unknown/unconfigured-frame counter |

All functional CAN operations implemented by this pinned reference are available
in the new firmware. Linux SocketCAN interface selection and command-line flags
are represented by the Arduino transport adapter and serial commands, not a new
Linux executable. The reference's intentionally ignored startup/status frames
are visible through raw tracing; speculative alarm or enable-state meanings
have not been added.

## Deliberate limits

- Ah is the reference's estimate, not a calibrated accumulator: lost unsolicited
  frames undercount, and it resets on controller reboot. The reference itself
  still uses the old /20 factor for this frame despite changing setpoint scaling.
- Rated current is explicit config (50 A by default). Automatic identification
  and rated-current discovery are not implemented by the pinned reference and
  have not been assumed. Validate current-limit calibration on the actual units.
- The reference has no PSU address discovery/assignment, stable physical-ID
  binding, output-enable setter, fan controls, charging-state machine, or active
  bank balancing. Those remain future features, not missing parity patches.
- The safe offline-voltage validation range is 48.0..58.5 V, versus
  41.5..58.5 V online. A PSU can still reject a model-specific endpoint; its ACK
  is authoritative. Current is bounded by `ChargerConfig.h` and 120% of the
  configured rating.
- The retained CAN driver uses synchronous transmission. The firmware has been
  compiled and its logical behaviours tested with simulated I/O; no claim of
  physical multi-PSU, CAN timing, encoder, display, or power-cut validation is made.

The software includes the reference's GPLv3 license and attribution for the
derived protocol behaviour. Hardware design assets and the historical sketch
retain their existing provenance.
