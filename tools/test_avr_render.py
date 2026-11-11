#!/usr/bin/env python3
"""Run complete redraws on simavr with production AVR code and nominal SPI cost.

Optional Linux regression: needs libsimavr-dev, g++, and the existing PlatformIO
AVR packages/dependencies. --simavr-prefix also accepts an extracted package's
usr directory; no system installation or hardware upload is performed here.
"""
import argparse
import ast
import os
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--core-dir', type=Path, default=root.parent / '.platformio')
parser.add_argument('--simavr-prefix', type=Path, default=Path('/usr'))
args = parser.parse_args()
compiler = args.core_dir / 'packages/toolchain-atmelavr/bin/avr-g++'
arduino = args.core_dir / 'packages/framework-arduino-avr'
font = next((root / '.pio/libdeps').glob('*/Adafruit GFX Library/glcdfont.c'), None)
include = args.simavr_prefix / 'include/simavr'
library = next(args.simavr_prefix.glob('lib/**/libsimavr.so'), None)
if not compiler.is_file() or not font or not library or not (include / 'sim_avr.h').is_file():
    raise SystemExit('Build a Mega display profile first and provide libsimavr-dev (or --simavr-prefix PATH).')
manifest = ast.parse((root / 'tools/test_host.py').read_text())
sources = next(ast.literal_eval(n.value) for n in manifest.body if isinstance(n, ast.Assign)
               and any(isinstance(t, ast.Name) and t.id == 'sources' for t in n.targets))
sources = [str(root / s) for s in sources if not s.startswith('test/') and
           s not in ('src/ui/SerialConsole.cpp', 'src/ui/SerialProfiles.cpp', 'src/ui/SerialAlerts.cpp')]
sources += [str(root / s) for s in ('src/debug/Simulator.cpp', 'src/ui/Ili9341Display.cpp', 'test/avr_render/benchmark.cpp')]
sources += [str(arduino / 'cores/arduino' / s) for s in ('wiring.c', 'wiring_digital.c')]
with tempfile.TemporaryDirectory(prefix='r4850-avr-render-') as folder:
    folder = Path(folder)
    elf, runner = folder / 'benchmark.elf', folder / 'runner'
    subprocess.run([str(compiler), '-mmcu=atmega2560', '-DF_CPU=16000000UL', '-std=gnu++11',
                    '-Os', '-flto', '-fno-tree-switch-conversion', '-fno-exceptions', '-g',
                    '-ffunction-sections', '-fdata-sections', '-DPSU_UI_DEBUG=1',
                    '-DPSU_MAX_UNITS=9', '-DPSU_ENABLE_DISPLAY=1', '-DPSU_ENABLE_SERIAL=0', '-DPSU_ENABLE_ENCODER=0',
                    '-I' + str(root / 'test/avr_render'), '-I' + str(font.parent), '-I' + str(root / 'src'),
                    '-I' + str(arduino / 'cores/arduino'), '-I' + str(arduino / 'variants/mega'),
                    *sources, '-Wl,--gc-sections', '-o', str(elf)], check=True)
    subprocess.run([os.environ.get('CXX', 'g++'), '-std=c++11', '-O2', '-Wall', '-Wextra', '-Werror',
                    '-I' + str(include), str(root / 'test/avr_render/run.cpp'), '-L' + str(library.parent),
                    '-lsimavr', '-o', str(runner)], check=True)
    env = os.environ.copy()
    env['LD_LIBRARY_PATH'] = str(library.parent) + ':' + env.get('LD_LIBRARY_PATH', '')
    subprocess.run([str(runner), str(elf)], env=env, check=True, timeout=90)
