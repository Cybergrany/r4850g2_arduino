#!/usr/bin/env python3
"""Exercise production profiling/observer code and the actual MCP2515 receive ISR."""
from pathlib import Path
import os
import subprocess
import tempfile
root=Path(__file__).resolve().parents[1]
cc=[os.environ.get('CXX','g++'),'-std=c++11','-Wall','-Wextra','-Werror','-g',
    '-fsanitize=address,undefined','-fno-omit-frame-pointer',
    '-Itest/bus_support','-Itest/support','-Isrc',
    '-DPSU_BUS_PROFILE=1','-DPSU_ENABLE_DISPLAY=0','-DPSU_ENABLE_ENCODER=0','-DPSU_CHG_ALLOW_PIN=27']
environment=os.environ.copy()
environment['ASAN_OPTIONS']=environment.get('ASAN_OPTIONS','')+':detect_leaks=0'
with tempfile.TemporaryDirectory(prefix='psu-bus-') as folder:
    binary=str(Path(folder)/'test')
    subprocess.run(cc+['-DPSU_CAN_OBSERVER=1','src/diagnostics/BusProfile.cpp','src/diagnostics/BusProfileText.cpp',
        'src/observer/BusObserver.cpp','src/protocol/HuaweiProtocol.cpp','test/test_bus_observer.cpp','-o',binary],cwd=root,check=True)
    subprocess.run([binary],env=environment,check=True)
    for observer in (0,1):
        subprocess.run(cc+[f'-DPSU_CAN_OBSERVER={observer}','-DPSU_TEST_FAN_GPIO=1',
            'src/can/Mcp2515Transport.cpp','src/protocol/HuaweiProtocol.cpp','test/test_bus_receive.cpp','-o',binary],cwd=root,check=True)
        subprocess.run([binary],env=environment,check=True)
    subprocess.run(cc+['-DPSU_CAN_OBSERVER=1','-DPSU_TEST_FAN_GPIO=1','-DPSU_ACTIVE_PIN=30','-DPSU_WARNING_PIN=28','-DPSU_ERROR_PIN=29',
        'src/main.cpp','src/observer/BusObserver.cpp','src/can/Mcp2515Transport.cpp','src/io/ChargeAllowOutput.cpp',
        'src/diagnostics/BusProfile.cpp','src/diagnostics/BusProfileText.cpp','src/protocol/HuaweiProtocol.cpp',
        'test/test_observer_main.cpp','-o',binary],cwd=root,check=True)
    subprocess.run([binary],env=environment,check=True)
