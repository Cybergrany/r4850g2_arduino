# Protocol evidence and reference parity

Compared against these revisions/sources:

- Fork baseline: [`143f197a059b41c80dd57dcc865ebd946883b3e9`](https://github.com/Cybergrany/r4850g2_arduino/tree/143f197a059b41c80dd57dcc865ebd946883b3e9).
- Craig Peacock's executable C reference: [`6d8ff660f8d5ddd551958cc9593864bf21154ed7`](https://github.com/craigpeacock/Huawei_R4850G2_CAN/blob/6d8ff660f8d5ddd551958cc9593864bf21154ed7/r4850.c).
- Address fields: [BotoX `huawei.cpp` at `c0ec00d`](https://github.com/BotoX/huawei-r48xx-esp32/blob/c0ec00d52801b07be695284bff9c330266c932a9/src/huawei.cpp).
- Discovery/identity observations: [patagonaa's R48xx protocol documentation](https://github.com/patagonaa/huawei-r48xx#can-protocol), reviewed 2026-09-22.

Craig's executable implements newer rated-current scaling than its README
examples/spreadsheet; this firmware retains that scaling rather than the old
Arduino sketch's fixed current multiplier.

| Craig reference operation | This firmware |
| --- | --- |
| 125 kbit/s, extended CAN | Preserved; configurable MCP2515 adapter |
| Periodic telemetry query | Eight-byte data requests, independent of UI |
| Input/output V, A, W, frequency, temperatures | Per identity/slot, with validity and freshness; all available through `telemetry` |
| Efficiency | Fraction internally, percent in serial telemetry |
| Maximum available current `0x76` | Raw / 1024 × commissioned rated current |
| Online voltage/current | `apply all`, `apply voltage`, `apply GROUP`; global voltage and per-member group shares |
| Offline voltage/current | Global draft voltage and group draft totals; explicit `offline all` |
| Online plus offline writes (`-s`) | `apply all`, wait/check result, then `offline all`, wait/check result |
| Accepted/rejected setting ACK and value | Matching job outcome; decoded volts/amps and raw value in serial |
| Incoming OVP ACK | Decoded as volts; no new OVP setter |
| Ah estimate from `1001117E` | Per bound slot/identity session in RAM, same /20 and 377 ms assumption; `reset-ah` |
| ASCII description | `describe ADDRESS`, including continuation/final fragments and sanitized controls |
| Ignored/unknown traffic inspection | `raw on`, bus counters, and raw alarm/status bits |

The pinned reference's functional CAN operations remain available. Linux
SocketCAN options are represented by the Arduino transport and serial interface.
The previous combined `persist` shortcut is replaced by explicit online/offline
commands. No complex outstanding patch is needed for parity with this revision.

## Discovery policy and evidence boundaries

Patagonaa documents apparent automatic software-address negotiation, INFO `0x50`
register `0x0002` as a six-byte serial identity, and unsolicited `0x11` status
whose byte 3 distinguishes ready from not ready. Those are reverse-engineering
observations, not a universal vendor identity/safety contract. The firmware uses
read-only discovery, corroborates identity, and refuses ambiguous routing. It
does not generate address-assignment traffic. E-Label model/barcode inspection
and explicit current rating remain part of commissioning. Unsupported identity
or readiness behaviour blocks control pending investigation. [Protocol source](https://github.com/patagonaa/huawei-r48xx#can-protocol).

The same source describes different current normalization on some R48xx models.
This implementation preserves Craig's established R4850G2 scaling; it does not
automatically treat every R48xx as electrically equivalent. Validate commands,
reported current, and actual current on each supported model. `0x0183` is retained
raw; nonzero is not automatically interpreted as a fault. [Observed values](https://github.com/patagonaa/huawei-r48xx#output-current-limit).

Huawei's [R4850G2 manual](https://www.beyondlogic.org/pdf/Huawei_R4850G2_Rectifier_User_Manual_V1.4.pdf)
describes parallel operation and current sharing. Source groups in this firmware
select which unit limits to command; they are not a closed-loop power-sharing
controller, BMS, contactor controller, charge-stage algorithm, or termination
policy. Actual group loading must be measured under the intended wiring/load.

Online settings are volatile; PSU defaults govern startup and communication-loss
fallback. Establish compatible defaults before adding a replacement to a live
pack. Read-only discovery cannot prevent an already powered unit from outputting
its own saved settings. [Craig's hardware review](https://www.beyondlogic.org/review-huawei-r4850g2-power-supply-53-5vdc-3kw/).

## Deliberate limits

- Ah remains a rough reference-compatible session estimate. Lost frames
  undercount. Bound-slot totals survive reconnects/address changes in Mega RAM;
  reboot, `reset-ah`, rebinding to a different identity, slot removal or deployment
  change resets them. The discovery-local counter remains available to API users
  but can reset with its observation. Neither is a calibrated battery fuel gauge.
- Per-unit configured current is bounded by 60 A and 120% of the commissioned
  rating. Group totals exceeding any member's allowed share are rejected, never
  silently capped. Hardware can reject tighter model-specific bounds.
- Voltage validation is 41.5–58.5 V online and 48–58.5 V offline. ACKs report PSU
  acceptance, not actual load delivery or protection status.
- No automatic model/rating identification, full alarm decoder, PSU address
  setter, fan control, standby setter, or input-current setter is implemented.
  These are outside the pinned Craig reference's implemented controls.
- The identity check before each write reduces address-reuse risk; there is no
  protocol transaction that atomically proves identity and sets a value. Fast
  resets without observable readiness loss and faulty/duplicate identities need
  physical validation/supervision. EEPROM and software cannot electrically
  isolate an unknown or failed PSU.
- Compilation and host simulation do not validate physical multi-PSU address
  negotiation, timing, calibration, sharing, or electrical power-loss behaviour.
  See [bench validation](VALIDATION.md).

Derived protocol behaviour retains Craig Peacock's GPL-3.0-or-later attribution
in source and the repository's [LICENSE](../LICENSE). Historical sketch/hardware
assets retain their existing provenance.
