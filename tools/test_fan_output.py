#!/usr/bin/env python3
"""Exercise physical PWM adapter boundaries, polarity and Mega pin conflicts."""
from pathlib import Path
import os
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
compiler = [os.environ.get('CXX', 'g++'), '-std=c++11', '-Wall', '-Wextra', '-Werror',
            '-Itest/support', '-Isrc']
with tempfile.TemporaryDirectory(prefix='r4850-fan-') as folder:
    binary = str(Path(folder) / 'test')
    for inverted in (0, 1):
        subprocess.run(compiler + ['-DPSU_TEST_FAN_GPIO=1', '-DPSU_FAN_PWM_PIN=6',
            f'-DPSU_FAN_PWM_INVERTED={inverted}', 'src/io/FanOutput.cpp',
            'test/test_fan_output.cpp', '-o', binary], cwd=root, check=True)
        subprocess.run([binary], check=True)
    for pin, extra, error in ((10, [], 'conflicts with CAN'),
                             (11, [], 'Timer1'),
                             (26, [], 'needs a Mega PWM pin'),
                             (6, ['-DPSU_ENABLE_ENABLE_IN=1', '-DPSU_ENABLE_IN_PIN=6'], 'conflicts with ENABLE_IN'),
                             (6, [], None), (44, [], None),
                             (11, ['-DPSU_ENABLE_ENCODER=0'], None)):
        result = subprocess.run(compiler + [f'-DPSU_FAN_PWM_PIN={pin}', *extra,
            '-x', 'c++', '-fsyntax-only', '-'], input='#include "config/ControlConfig.h"\n',
            text=True, capture_output=True, cwd=root)
        if (error and (result.returncode == 0 or error not in result.stderr)) or (not error and result.returncode):
            raise SystemExit(f'Unexpected pin validation for D{pin}: {result.stderr}')
print('PASS: PWM startup, duty, polarity, unchanged writes, cadence/wrap and pin/timer conflicts')
