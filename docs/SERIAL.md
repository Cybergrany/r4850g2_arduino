# Serial lifecycle on the Mega 2560

The application calls `Serial.begin(115200)` without waiting for a host and
services CAN/controller work before serial and optional display work each loop. The Mega uses a
hardware UART behind a separate USB bridge. Its AVR `HardwareSerial::operator bool`
always returns true; it does not expose terminal presence or DTR. UART transmission
continues without a terminal. Firmware buffers output and writes only when
`availableForWrite()` reports room, with at most 48 transmitted bytes per loop.
It never waits for a UART buffer or USB reader. Raw logging has its own bounded
queue; saturation drops trace entries with a warning, not controller ACK handling.

| Situation | Expected behaviour |
| --- | --- |
| Boot on independent power, no USB host | Defaults/EEPROM load and CAN polling proceed; startup text is not replayed automatically |
| Connect/open monitor without MCU reset | Existing settings, polling, and queued writes continue; press Ctrl-X for a fresh console |
| Close/reopen monitor without MCU reset | Same controller state; raw/watch may still be enabled until Ctrl-X or `hello` |
| USB disconnect with independent board power | Controller keeps running; console cannot detect this event |
| USB unplug when USB is the only power | Board powers off; reconnect is a new boot |
| DTR reset or power cycle | RAM/telemetry/unfinished jobs reset; EEPROM/defaults load, saved `autoresume` policy waits for verified, ready members |

Arduino documents the Mega's [USB DTR to reset connection](https://store.arduino.cc/products/arduino-mega-2560-rev3).
A terminal opening the port can therefore cause a real MCU reset, not just a
console reconnect. `platformio.ini` sets `monitor_dtr = 0` and `monitor_rts = 0`
to avoid intentional assertion by the monitor. These are
[monitor settings](https://docs.platformio.org/en/stable/core/userguide/device/cmd_monitor.html),
not upload settings. Some OS/USB drivers pulse lines during port open regardless;
firmware cannot prevent an external reset. Reappearing startup banners indicate
a reboot. Unsaved RAM configuration is lost on an actual reboot. `save` stores
controller configuration; it does not by itself enable applying it to PSUs on boot.

## Reconnect procedure

1. Open the project monitor at 115200 baud with terminal local echo disabled.
2. Press **Ctrl-X** before sending Enter or a command. This sends byte `0x18`,
   clears partial/invalid input, stops raw/watch/reports and description streaming, cancels an unsubmitted partial-apply confirmation,
   restores firmware echo, and prints a prompt. It does not cancel PSU jobs or
   change settings. Do not submit an old partial command by pressing Enter first.
3. Type `hello` for a greeting or `config` / `status all` to inspect state.

`hello` also resets console preferences, but requires a clean command boundary;
it cannot recover an unknown partial prefix by itself. Ctrl-C (`0x03`) has the
same firmware effect as Ctrl-X, but PlatformIO normally consumes Ctrl-C to quit
the monitor. Use Ctrl-X there. Ctrl-U (`0x15`) clears only the input line.
Line-buffering filters such as `send_on_enter` delay control keys as well as text;
use the normal direct keystroke mode for immediate recovery. For scripts, send
`\x18hello\n` after opening the port, and wait for the greeting before proceeding.
A bootloader/reset may consume early bytes; retry this console-only handshake
if needed rather than blindly replaying PSU write commands.

The UART cannot detect a rapid disconnect/reconnect. Until Ctrl-X is received,
a partial command remains pending for up to 30 seconds of input inactivity.
At that deadline it is invalidated, including if new bytes are already queued
when the next loop checks the timeout. Remaining bytes are discarded through
the next newline, so neither the old command nor its tail is executed. The
expiry warning is emitted once. Ctrl-X or Ctrl-U can clear discard state early.
The timeout and per-loop input budget are in `src/config/ConsoleConfig.h`.

Input continues draining while output is backed up. One complete command may
wait for response space; further input is discarded through its newline and
reported as `ERR input backlog`. An incomplete discarded prefix cannot become
a new command when its suffix arrives later. Ctrl-X can cancel a command that
has not yet reached the controller. For scripts, submit one command and wait
for its response/prompt before sending another; do not blindly paste a long
batch. After `save`, wait for `EEPROM OK`, not just the initial prompt.

Reports yield between rows and pause during editing. Raw frames and setting ACKs
print on their own lines and redraw the prompt/input, using basic CR/LF and
backspace rather than terminal-specific escape sequences. This redraw requires
firmware echo with local echo disabled. Description fragments remain streamed;
intervening input/output can split them across lines. Console resets cannot
withdraw commands already submitted to the PSU controller.

`save` prints `EEPROM save started`, then a later verification result. CAN and
input continue during programming. Edits made after the snapshot remain in RAM
and are explicitly reported as unsaved. Overlapping saves, loads, defaults and
explicit applies return Busy during programming. A console reset does not cancel
the save. `poll` and `describe` also report queue acceptance separately from
subsequent replies or transport errors. If a description overflows the trace
queue, retry with `raw off` after the warning.

## Garbled output on reconnect

Opening a monitor during output may capture only the end of a line or report.
The console now separates background messages from echoed input and restores
the prompt. Ctrl-X then `hello` establishes a fresh response even if the boot
banner was missed. This does not clear text already buffered by the host.

If fresh greeting/status responses still contain unreadable characters, this is
not explained by prompt interleaving. Check 115200, 8-N-1, no flow control, no
second program accessing the port, and capture the actual bytes/output for
further diagnosis. Firmware cannot reset or flush the separate USB bridge from
`Serial`; `Serial.flush()` only waits for UART transmission and is not a USB
reconnect or receive-buffer reset. No physical garbling diagnosis is claimed
from the host tests.

## Hardware checks still required

Host tests cover parser/controller behaviour with output unobserved, resumed
input, abandoned commands, timeout rollover, background output, and active PSU
jobs. They do not emulate the USB bridge, OS port events, power loss, or DTR.

- Boot on independent power with USB absent, then attach/open the monitor.
  Recover with Ctrl-X; check `config`, `status all`, and continued polling.
- Enable `watch on` / `raw on`, close/reopen the monitor, and use Ctrl-X again.
  Check for a startup banner to distinguish a reset from a surviving session.
- Type `config` without Enter, reconnect both before and after 30 seconds,
  then Ctrl-X and `hello`. Neither test should execute the abandoned input.
- Close/reopen during a deliberately chosen online apply; inspect status after
  reconnect. A surviving session should complete ACK processing. A real reset
  interrupts the job and follows saved boot policy, so inspect actual PSU state.
- Unplug/replug USB once with independent power and once with USB-only power.
  Confirm the expected runtime-versus-new-boot behaviour in the table above.

Group commissioning, discovery, apply confirmation, and persistence are covered
in [the serial group guide](GETTING_STARTED.md).
