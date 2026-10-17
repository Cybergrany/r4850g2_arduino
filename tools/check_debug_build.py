#!/usr/bin/env python3
"""Reject debug firmware that links a physical CAN driver or EEPROM access."""
import argparse
import os
from pathlib import Path
import re
import subprocess
root = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--core-dir', type=Path, default=Path(os.environ.get('PLATFORMIO_CORE_DIR', root.parent / '.platformio')))
args = parser.parse_args()
nm = args.core_dir / 'packages/toolchain-atmelavr/bin/avr-nm'
for environment in ('mega2560_ui_debug', 'mega2560_touch_debug', 'mega2560_touch_debug_local'):
    elf = root / '.pio/build' / environment / 'firmware.elf'
    symbols = subprocess.check_output([str(nm), '-C', str(elf)], text=True)
    forbidden = re.findall(r'^.*(?:MCP2515|Mcp2515Transport|mcp2515::|ArduinoEeprom|eeprom_(?:read|write|update)|\bCANClass\b).*$', symbols, re.M)
    if forbidden:
        raise SystemExit(environment + ': hardware linked:\n' + '\n'.join(forbidden))
    if 'SimulatedCan' not in symbols or 'RamStorage' not in symbols:
        raise SystemExit(environment + ': simulation adapters missing')
    print(environment + ': PASS (simulator/RAM present; physical CAN/EEPROM absent)')
