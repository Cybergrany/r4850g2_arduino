#!/usr/bin/env python3
"""Optional: run the built Mega wheel/profile ELF with CAN INT held LOW in simavr.

Requires libsimavr-dev (Debian/Ubuntu), or --simavr-prefix pointing at extracted
packages. Uses real AVR instructions/interrupt masking, a minimal idle MCP/TFT
model, and no physical hardware. Simulated SPI speed is not a bench measurement.
"""
import argparse
import os
from pathlib import Path
import re
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--elf", type=Path, default=root / ".pio/build/mega2560_wheel_profile/firmware.elf")
parser.add_argument("--simavr-prefix", type=Path, default=Path("/usr"))
parser.add_argument("--expect-starvation", action="store_true", help="Confirm failure in a pre-fix ELF")
args = parser.parse_args()
prefix = args.simavr_prefix.resolve()
library = next(iter(sorted((prefix / "lib").glob("**/libsimavr.so"))), None)
if library is None:
    parser.error("Install libsimavr-dev or supply --simavr-prefix")
with tempfile.TemporaryDirectory(prefix="r4850-avr-irq-") as folder:
    binary = Path(folder) / "fixture"
    subprocess.run([os.environ.get("CC", "cc"), "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror",
                    "-I" + str(prefix / "include/simavr"), str(root / "test/avr_interrupt_guard.c"),
                    "-L" + str(library.parent), "-Wl,-rpath," + str(library.parent), "-lsimavr",
                    "-o", str(binary)], check=True)
    run = subprocess.run([str(binary), str(args.elf.resolve())], capture_output=True, text=True,
                         check=True, timeout=60)
    result = re.search(r"SIM_RESULT cycles=(\d+) millis=(\d+) interrupts=(\d+) uart-bytes=(\d+) tft-bytes=(\d+) boot=(\d+)", run.stdout)
    if result is None:
        raise AssertionError(run.stdout[-2000:] + run.stderr[-2000:])
    cycles, ms, interrupts, uart, tft, boot = map(int, result.groups())
    if args.expect_starvation:
        assert not boot and interrupts > 1000 and ms < cycles / 16000 - 1000, result.group()
        print("PASS: reproduced pre-fix interrupt starvation; " + result.group())
    else:
        assert boot and interrupts == 1 and uart > 500 and tft > 153600, result.group()
        assert abs(ms - cycles / 16000) < 50, result.group()
        for expected in ("rx-irq-yields=1 rx-polling=1 irq-low=1 flags-at-yield=0x0",
                         "clock-invalid-samples=0", "configured slots=", "Addresses are temporary"):
            assert expected in run.stdout, expected + " missing:\n" + run.stdout[-3000:]
        print("PASS: actual Mega ELF retains clock/UART/TFT progress with stuck INT; " + result.group())
