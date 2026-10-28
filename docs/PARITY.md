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
raw; nonzero is not automatically interpreted as a fault. Bit 29 is decoded
separately as input-power failure, with fresh/present/absent/unknown state;
other bits remain available for inspection. The zero-hold readiness exception
accepts only fresh status with bits 9/10 (shutdown/fan-MAX) or zero; other set
bits block that exception. These operating/settings bits come from Appendix A
of the [related Huawei protocol reference](https://github.com/patagonaa/huawei-r48xx/blob/main/doc/Huawei%20R100030G1%20(R4850G2)%20charging%20module%20CAN%20communication%20protocol%20-%20%E5%85%85%E7%94%B5%E6%A8%A1%E5%9D%97CAN%E9%80%9A%E8%AE%AF%E5%8D%8F%E8%AE%AE%E6%8C%87%E5%AF%BC%E4%B9%A6%20-%20translated%2Bocr.pdf),
and match the G2 bench trace's `0x400`/`0x600`. This is not a full G2 alarm decoder
or a guarantee across firmware variants. The AC interpretation matches
[patagonaa's AC-present decoder](https://github.com/patagonaa/esphome-huawei-r4850/blob/main/components/huawei_r4850/huawei_r4850.cpp).
The AVR mask uses an explicit 32-bit value, rather than a 16-bit `int` shift.

Huawei's [R4850G2 manual](https://www.beyondlogic.org/pdf/Huawei_R4850G2_Rectifier_User_Manual_V1.4.pdf)
describes parallel operation and current sharing. Source groups in this firmware
select which unit limits to command; they are not a closed-loop power-sharing
controller, BMS, charge-stage algorithm, or termination
policy. Actual group loading must be measured under the intended wiring/load.

The Mega can now command the installation's common AC cutoff through CHG_ALLOW;
this adds no contactor feedback or DC isolation. The manual's 3–8 s startup delay
is covered by a separate 15 s readiness allowance; stopped-current checks retain
their shorter deadline. See [startup policy and hardware acceptance](STARTUP.md).

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
  setter or input-current setter is implemented.
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

## Standby and fan controls (2026-10-27)

The [documented register set format](https://github.com/patagonaa/huawei-r48xx#80-register-set-request)
puts standby `0x0132` and fan mode `0x0134` in byte 3 (0/1), with zeroes
in bytes 2 and 4–7. Full echoed payloads are matched for ACKs. The same source
marks `0x0114` and `0x0187` unsupported on G2; neither is used here.

The [ESPHome component's persistence table](https://github.com/patagonaa/esphome-huawei-r4850#persistence)
reports standby as volatile/CAN-connected and fan Auto/Max as persistent in the
PSU, including when disconnected. This motivates transition-only fan writes and
explicit commissioning of offline V/A defaults. The guarded zero/fan/voltage/wake
sequence is controller policy based on those operations, not a documented atomic
G2 safe-start transaction. Verify register acceptance and retained settings on
the actual hardware. [Implemented policy and limitations](CONTROL.md).

Normal OFF now uses volatile online current `0x0103 = 0` and keeps the unit
awake. Startup clears inherited standby (`0x0132 = OFF`) only after zero and
voltage preparation. Hardware standby ON remains a bounded fault fallback when
zero/wake cannot be confirmed. This changes controller policy, not register
encoding; zero acceptance and wake retention still require real G2 testing.
