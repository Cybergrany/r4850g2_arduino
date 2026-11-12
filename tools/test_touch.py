#!/usr/bin/env python3
"""Exercise touch, simulated transport, real UI and isolated calibration storage."""
from pathlib import Path
import os
import subprocess
import tempfile
root = Path(__file__).resolve().parents[1]
sources = [
    "src/diagnostics/RuntimeTiming.cpp",
    "src/ui/UiAlerts.cpp", "src/ui/UiAlertView.cpp", "src/ui/UiRecovery.cpp", "src/ui/UiRecoveryView.cpp",
    'src/psu/Alerts.cpp', 'src/psu/PsuThermal.cpp', 'src/psu/PsuAlerts.cpp', 'src/io/AlertOutputs.cpp',
    'src/psu/PsuAvailability.cpp', 'src/io/ChargeAllowOutput.cpp', 'src/io/PsuActiveOutput.cpp',
    'src/ui/UiAvailability.cpp',
    'src/config/ChargerConfig.cpp', 'src/protocol/HuaweiProtocol.cpp',
    'src/psu/PsuControl.cpp', 'src/psu/Psu.cpp', 'src/psu/Discovery.cpp', 'src/psu/PsuController.cpp', 'src/psu/PsuProfiles.cpp',
    'src/psu/Presentation.cpp', 'src/storage/MemoryManager.cpp',
    'src/ui/UiModel.cpp', 'src/ui/UiSettings.cpp', 'src/ui/UiFrame.cpp', 'src/ui/UiView.cpp',
    'src/ui/UiLayout.cpp', 'src/ui/UiSettingsView.cpp', 'src/ui/Touch.cpp', 'src/ui/LocalUi.cpp',
    'src/debug/Simulator.cpp', 'src/ui/SerialConsole.cpp', 'src/ui/SerialProfiles.cpp', 'src/ui/SerialAlerts.cpp', 'src/io/FanOutput.cpp', 'src/app/Application.cpp', 'test/test_touch.cpp',
]
with tempfile.TemporaryDirectory(prefix='r4850-touch-') as folder:
    binary = str(Path(folder) / 'test')
    subprocess.run([os.environ.get('CXX', 'g++'), '-std=c++11', '-Wall', '-Wextra',
        '-Werror', '-g', '-fsanitize=address,undefined', '-fno-omit-frame-pointer',
        '-DPSU_ACTIVE_PIN=30', '-DPSU_WARNING_PIN=28', '-DPSU_ERROR_PIN=29', '-DPSU_ENABLE_TOUCH=1', '-DPSU_UI_DEBUG=1', '-DPSU_ENABLE_ENCODER=1', '-DPSU_ENABLE_ENABLE_IN=1', '-DPSU_FAN_PWM_PIN=6', '-DPSU_CHG_ALLOW_PIN=27',
        '-Itest/support', '-Isrc', *sources, '-o', binary], cwd=root, check=True)
    env = os.environ.copy()
    env['ASAN_OPTIONS'] = env.get('ASAN_OPTIONS', '') + ':detect_leaks=0'
    subprocess.run([binary], cwd=root, env=env, check=True)
