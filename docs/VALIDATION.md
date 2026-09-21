# Validation

Build target: Arduino Mega 2560. Fork base `143f197`; reference comparison
`6d8ff660`. No upload or live hardware exercise was performed.

Commands run from the repository with PlatformIO Core 6.1.18:

```sh
python3 tools/test_host.py
PLATFORMIO_CORE_DIR=/home/dave/psu-can-workspace/.platformio pio run \
  -e mega2560 -e mega2560_serial -e mega2560_display \
  -e mega2560_local -e mega2560_minimal -j 4
git diff --check
```

The absolute `PLATFORMIO_CORE_DIR` only isolates this workspace's downloaded
packages. On another machine, omit it or choose your own writable cache.

All five builds passed with Atmel AVR platform 5.3.0, Arduino AVR framework
5.4.0, AVR GCC 7.3.0, CAN 0.3.1, SSD1306Ascii 1.3.5,
ClickEncoder d6d5738fdf, and TimerOne 1.2.0. The only third-party compile warnings
observed were ClickEncoder's existing constructor member-order warnings.

Build sizes against the Mega's 8192 bytes of RAM and 253952 bytes of application flash:

| Profile | Static RAM (bytes) | Flash (bytes) |
| --- | ---: | ---: |
| `mega2560` | 2175 | 32480 |
| `mega2560_serial` | 1882 | 24814 |
| `mega2560_display` | 2150 | 30254 |
| `mega2560_local` | 1675 | 22876 |
| `mega2560_minimal` | 1352 | 12688 |

Build reports measure static RAM, not worst-case stack or interrupt nesting. The two EEPROM journal
slots reserve 512 bytes and occupy 116 bytes each with eight configured-capacity
slots, regardless of the current active count.

## Host checks

The host suite compiles the actual C++ configuration, protocol, PSU controller,
memory manager, and serial console. Only the I/O adapters are simulated. It runs
with `-Wall -Wextra -Werror` and AddressSanitizer/UndefinedBehaviorSanitizer.
LeakSanitizer is disabled because it cannot operate under the execution sandbox's
ptrace mechanism; address and undefined-behaviour checks remain enabled.

Covered behaviours:

- Exact CAN IDs and payloads, including addresses 1, 2, and 127; data-frame
  requests; correct voltage and rated-current scaling.
- Interleaved telemetry for different PSUs; independent current scaling;
  signed temperature; separate fast/filtered current; invalid frames ignored.
- Finite numeric validation; voltage/current limits; unique addresses;
  atomic validation of RAM edits across heterogeneous PSU configurations.
- Single and range apply; skipped disabled slots; one outstanding setting;
  wrong-address/wrong-value ACKs; rejection; timeout; transmit failure;
  `millis()` rollover; failed CAN initialization with config still editable.
- Online + offline command ordering and distinct stored setpoints.
- Every EEPROM update interruption point for both a first save and an
  overwrite of the older slot after two valid saves: 118 boundaries for each.
- Corrupted newest-record fallback; incompatible schema; readback failure;
  invalid configuration rejection; capacity below 512 bytes; address bounds.
- Serial commands with LF/CRLF, backspace, partial lines, inclusive ranges,
  malformed/NaN/infinite/overflowing numbers, overlong lines and surplus tokens.
- Controller save/load/defaults versus PSU writes; apply-in-progress edits
  rejected; polling continues while waiting for a serial newline.
- Description continuation and final fragments; decoded current and OVP ACK values.

## Bench checks before deployment

1. Confirm Mega SPI and I2C wiring and the MCP2515 crystal setting. Build the
   serial-only profile first if the display/encoder are absent. Verify `CAN ready`.
2. With one PSU, inspect `status 1` and `raw on`. Confirm telemetry and current
   scaling against a meter/load. Use a modest intended setpoint and check both
   voltage and current ACKs, then measured output.
3. Attach the additional PSUs and inspect their addresses. Map the configured
   slots, vary one slot's setpoint, and verify only that physical unit responds.
   Repeat after different startup orders before relying on independent settings.
4. Exercise `apply 1-2` and inspect every slot's status. Check that a disconnected
   or rejecting unit does not report success or prevent the next unit's attempt.
5. Save/load controller config, reboot, and confirm that `bootapply 0` restores
   settings without issuing setpoint writes. Then exercise the explicit boot-apply
   option if needed for the installation.
6. Test `offline`/`persist` separately and check actual PSU behaviour after loss
   of CAN traffic and a PSU power cycle. Confirm each unit's accepted limits.
7. Exercise the display/menu and incomplete serial lines during polling, then
   inspect receive-drop counters under the intended bus load. Check raw tracing
   and EEPROM saves separately because they can increase processing latency.
8. If power-loss recovery is required in service, test physical EEPROM saves
   under controlled power interruption with the board's brownout configuration.
   The simulated journal tests do not model analogue brownout corruption.
