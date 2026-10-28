#!/usr/bin/env python3
"""Check reset-safe GPIO initialization and all AC-permissive pin conflicts."""
from pathlib import Path
import os
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
compiler = [os.environ.get('CXX', 'g++'), '-std=c++11', '-Wall', '-Wextra', '-Werror',
            '-Itest/support', '-Isrc']
with tempfile.TemporaryDirectory(prefix='r4850-cutoff-') as folder:
    binary = str(Path(folder) / 'test')
    subprocess.run(compiler + ['-DPSU_TEST_FAN_GPIO=1', '-DPSU_CHG_ALLOW_PIN=27',
        'src/io/ChargeAllowOutput.cpp', 'test/test_charge_allow.cpp', '-o', binary], cwd=root, check=True)
    subprocess.run([binary], check=True)
    for pin, extra, error in ((27, [], None), (-1, [], None), (70, [], 'serial/CAN/SPI'),
                             (0, [], 'serial/CAN/SPI'), (10, [], 'serial/CAN/SPI'),
                             (51, [], 'serial/CAN/SPI'), (3, [], 'wheel'),
                             (22, [], 'display'), (25, ['-DPSU_ENABLE_TOUCH=1'], 'touch'),
                             (26, ['-DPSU_ENABLE_ENABLE_IN=1'], 'ENABLE_IN'),
                             (6, ['-DPSU_FAN_PWM_PIN=6'], 'fan PWM')):
        result = subprocess.run(compiler + [f'-DPSU_CHG_ALLOW_PIN={pin}', *extra,
            '-x', 'c++', '-fsyntax-only', '-'], input='#include "config/ControlConfig.h"\n',
            text=True, capture_output=True, cwd=root)
        if (error and (not result.returncode or error not in result.stderr)) or (not error and result.returncode):
            raise SystemExit(f'Unexpected pin validation for D{pin}: {result.stderr}')
print('PASS: LOW-before-OUTPUT, edge-only GPIO writes and AC-permissive pin conflicts')
