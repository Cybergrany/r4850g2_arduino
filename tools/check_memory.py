#!/usr/bin/env python3
"""Check static RAM headroom and flash limits in every configured Mega ELF.

Run the full PlatformIO build matrix first. This is a static budget check;
it does not measure stack high-water usage or prove hardware timing.
"""
import argparse
import configparser
import os
from pathlib import Path
import subprocess

root = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--core-dir', type=Path, default=Path(os.environ.get('PLATFORMIO_CORE_DIR', root.parent / '.platformio')))
args = parser.parse_args()
size = args.core_dir / 'packages/toolchain-atmelavr/bin/avr-size'
config = configparser.ConfigParser(interpolation=None)
config.read(root / 'platformio.ini')
failed = []
for section in config.sections():
    if not section.startswith('env:'):
        continue
    name = section.split(':', 1)[1]
    elf = root / '.pio/build' / name / 'firmware.elf'
    if not elf.is_file():
        failed.append(name + ': missing ELF; build this profile first')
        continue
    sections = {}
    for line in subprocess.check_output([str(size), '-A', str(elf)], text=True).splitlines():
        parts = line.split()
        if len(parts) == 3 and parts[0].startswith('.'):
            sections[parts[0]] = int(parts[1])
    if '.text' not in sections or '.data' not in sections:
        raise SystemExit(name + ': cannot read ELF section sizes')
    ram = sum(sections.get(s, 0) for s in ('.data', '.bss', '.noinit'))
    flash = sections['.text'] + sections['.data']
    headroom = 8192 - ram
    print(f'{name}: RAM={ram}/8192, free={headroom}, flash={flash}/253952')
    if headroom < 1024 or flash > 253952:
        failed.append(name + ': exceeds Mega budget or leaves less than 1 KiB static RAM headroom')
if failed:
    raise SystemExit('\n'.join(failed))
print('PASS: all profiles retain at least 1 KiB static RAM headroom and fit flash')
