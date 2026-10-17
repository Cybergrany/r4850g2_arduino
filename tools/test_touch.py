#!/usr/bin/env python3
"""Exercise touch, simulated transport, real UI and isolated calibration storage."""
from pathlib import Path
import os
import subprocess
import tempfile
root = Path(__file__).resolve().parents[1]
sources = [
    'src/config/ChargerConfig.cpp', 'src/protocol/HuaweiProtocol.cpp',
    'src/psu/Psu.cpp', 'src/psu/Discovery.cpp', 'src/psu/PsuController.cpp',
    'src/psu/Presentation.cpp', 'src/storage/MemoryManager.cpp',
    'src/ui/UiModel.cpp', 'src/ui/UiFrame.cpp', 'src/ui/UiView.cpp',
    'src/ui/UiLayout.cpp', 'src/ui/Touch.cpp', 'src/ui/LocalUi.cpp',
    'src/debug/Simulator.cpp', 'src/ui/SerialConsole.cpp', 'src/app/Application.cpp', 'test/test_touch.cpp',
]
with tempfile.TemporaryDirectory(prefix='r4850-touch-') as folder:
    binary = str(Path(folder) / 'test')
    subprocess.run([os.environ.get('CXX', 'g++'), '-std=c++11', '-Wall', '-Wextra',
        '-Werror', '-g', '-fsanitize=address,undefined', '-fno-omit-frame-pointer',
        '-DPSU_ENABLE_TOUCH=1', '-DPSU_UI_DEBUG=1', '-DPSU_ENABLE_ENCODER=1',
        '-Itest/support', '-Isrc', *sources, '-o', binary], cwd=root, check=True)
    env = os.environ.copy()
    env['ASAN_OPTIONS'] = env.get('ASAN_OPTIONS', '') + ':detect_leaks=0'
    subprocess.run([binary], cwd=root, env=env, check=True)
