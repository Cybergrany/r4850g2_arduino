#!/usr/bin/env python3
"""Profile manual TFT recovery with the real Mega wheel ELF.

Requires libsimavr-dev, or --simavr-prefix pointing at extracted packages.
This is deterministic software fault injection, not physical PSU validation.
"""
import argparse
import os
from pathlib import Path
import subprocess

root = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--elf', type=Path, default=root / '.pio/build/mega2560_wheel_alerts/firmware.elf')
parser.add_argument('--simavr-prefix', type=Path, default=Path('/usr'))
parser.add_argument('--output', type=Path, required=True)
parser.add_argument('--cases', type=int, nargs='+', default=list(range(9)), choices=range(9))
args = parser.parse_args()
out = args.output.resolve()
out.mkdir(parents=True, exist_ok=True)
prefix = args.simavr_prefix.resolve()
library = next(iter(sorted((prefix / 'lib').glob('**/libsimavr.so'))), None)
if library is None:
    parser.error('Install libsimavr-dev or supply --simavr-prefix')
cxx = os.environ.get('CXX', 'g++')
flags = [cxx, '-std=c++11', '-O2', '-Wall', '-Wextra', '-Werror']
subprocess.run([*flags, '-Isrc', '-Itest/support', 'test/display_reset_eeprom.cpp',
                'src/config/ChargerConfig.cpp', 'src/storage/MemoryManager.cpp',
                '-o', str(out / 'seed')], cwd=root, check=True)
subprocess.run([*flags, '-I' + str(prefix / 'include/simavr'), 'test/avr_display_reset.cpp',
                '-L' + str(library.parent), '-Wl,-rpath,' + str(library.parent), '-lsimavr',
                '-o', str(out / 'sim')], cwd=root, check=True)
eeprom = out / 'eeprom.bin'
subprocess.run([str(out / 'seed'), str(eeprom)], check=True)
names = ['baseline', 'reset', 'enable-off', 'thermal-stop', 'pending-ack', 'missing-ack', 'save', 'wheel-hold', 'wheel-editor']
for case in args.cases:
    log = out / (names[case] + '.log')
    with log.open('w') as stream:
        result = subprocess.run([str(out / 'sim'), str(args.elf.resolve()), str(eeprom), str(case)],
                                stdout=stream, stderr=subprocess.STDOUT, timeout=180)
    text = log.read_text()
    assert result.returncode == 0, f'{names[case]} failed ({result.returncode}); see {log}'
    assert 'SIM PASS' in text and 'rx-drops=0' in text and 'clock-invalid-samples=0' in text, log
    assert 'ERR input backlog' not in text, log
    if case not in (3, 5):
        assert 'ERROR' not in text and 'last=ACK timeout' not in text, log
    stored = subprocess.check_output([str(out / 'seed'), str(eeprom) + f'.after-{case}', 'verify'], text=True)
    assert f'stop={1 if case == 3 else 0}' in stored, stored
    if case == 3:
        assert 'autoresume=0' in stored, stored
    if case == 6:
        save_start = text.index('EEPROM save started; wait for EEPROM OK before power-off')
        assert 'EEPROM OK; saved drafts and authorized profile are separate' in text[save_start:], \
            'Save must finish and verify, not only be queued'
    print(f'PASS {names[case]}: {stored.strip()}', flush=True)
    print('\n'.join(line for line in text.splitlines() if line.startswith('SIM ')), flush=True)
