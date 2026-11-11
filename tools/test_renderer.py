#!/usr/bin/env python3
"""Test the production ILI9341 rasterizer with a recording SPI/display surface."""
from pathlib import Path
import os
import subprocess
import tempfile
root = Path(__file__).resolve().parents[1]
font = root / '.pio/libdeps/mega2560/Adafruit GFX Library'
if not (font / 'glcdfont.c').exists():
    raise SystemExit('Build mega2560 first to install the pinned Adafruit font.')
with tempfile.TemporaryDirectory(prefix='psu-renderer-') as tmp:
    for hz in (500000, 1000000, 2000000, 4000000, 8000000):
        binary = str(Path(tmp) / str(hz))
        subprocess.run([os.environ.get('CXX', 'g++'), '-std=c++11', '-Wall', '-Wextra', '-Werror',
            '-g', '-fsanitize=address,undefined', '-fno-omit-frame-pointer',
            f'-DPSU_DISPLAY_SPI_HZ={hz}UL', '-DPSU_ENABLE_DISPLAY=1', '-DPSU_ENABLE_TOUCH=0',
            '-Itest/renderer_support', '-Itest/support', '-Isrc', '-I'+str(font),
            'src/ui/UiFrame.cpp', 'src/ui/Ili9341Display.cpp', 'test/test_renderer.cpp', '-o', binary], cwd=root, check=True)
        env = os.environ.copy()
        env['ASAN_OPTIONS'] = env.get('ASAN_OPTIONS', '') + ':detect_leaks=0'
        snapshots = sorted(Path(env['PSU_UI_SNAPSHOTS']).glob('*.txt')) if env.get('PSU_UI_SNAPSHOTS') else []
        subprocess.run([binary, *map(str, snapshots)], env=env, check=True)
