#!/usr/bin/env python3
"""Exercise the real wheel adapter and bundled ClickEncoder with sampled GPIO."""
from pathlib import Path
import argparse
import os
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--encoder-source', type=Path,
                    default=root / 'src/vendor/ClickEncoder/ClickEncoder.cpp',
                    help='Override with the original pinned source to reproduce the short-click failure')
args = parser.parse_args()
with tempfile.TemporaryDirectory(prefix='psu-wheel-guard-') as folder:
    d = Path(folder)
    (d / 'avr').mkdir()
    (d / 'avr/io.h').write_text('#pragma once\n')
    (d / 'avr/interrupt.h').write_text('#pragma once\ninline void cli() {}\ninline void sei() {}\n')
    (d / 'avr/pgmspace.h').write_text('#pragma once\n#define pgm_read_byte(p) (*(p))\n')
    (d / 'Arduino.h').write_text('#pragma once\n#include "' + str(root / 'test/support/Arduino.h') +
        '"\nconstexpr uint8_t INPUT=0, INPUT_PULLUP=2;\nint digitalRead(uint8_t);\n')
    (d / 'TimerOne.h').write_text("""#pragma once
#include <stdint.h>
struct Timer {
 uint32_t period=0;
 void (*callback)()=nullptr;
 void initialize(unsigned long us) {period=us;}
 void attachInterrupt(void (*isr)()) {callback=isr;}
};
extern Timer Timer1;
""")
    binary = str(d / 'test')
    # Upstream has member-order and ignored scalar return-qualifier warnings. Keep all
    # other warnings fatal; GPIO and ISR timing are supplied by the harness.
    subprocess.run([os.environ.get('CXX', 'g++'), '-std=c++11', '-Wall', '-Wextra',
        '-Werror', '-Wno-reorder', '-Wno-ignored-qualifiers', '-g', '-fsanitize=address,undefined', '-fno-omit-frame-pointer',
        '-DPSU_TEST_FAN_GPIO=1', '-I'+folder, '-Itest/support', '-Isrc',
        'src/ui/WheelInput.cpp', str(args.encoder_source.resolve()),
        'test/test_wheel_input.cpp', '-o', binary], cwd=root, check=True)
    env = os.environ.copy()
    env['ASAN_OPTIONS'] = env.get('ASAN_OPTIONS', '') + ':detect_leaks=0'
    subprocess.run([binary], env=env, check=True)
