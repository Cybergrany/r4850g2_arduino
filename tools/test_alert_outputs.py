#!/usr/bin/env python3
"""Test independent alert GPIOs, acknowledgement, debug isolation and pin validation."""
from pathlib import Path
import os
import subprocess
import tempfile
root=Path(__file__).resolve().parents[1]
cc=[os.environ.get('CXX','g++'),'-std=c++11','-Wall','-Wextra','-Werror','-Itest/support','-Isrc']
with tempfile.TemporaryDirectory(prefix='psu-alert-gpio-') as folder:
    binary=str(Path(folder)/'test')
    for debug in (0,1):
        subprocess.run(cc+['-DPSU_TEST_FAN_GPIO=1','-DPSU_WARNING_PIN=28','-DPSU_ERROR_PIN=29',
            f'-DPSU_UI_DEBUG={debug}','src/io/AlertOutputs.cpp','src/psu/Alerts.cpp',
            'test/test_alert_outputs.cpp','-o',binary],cwd=root,check=True)
        subprocess.run([binary],check=True)
    for pins,good in [([],True),(['-DPSU_WARNING_PIN=28','-DPSU_ERROR_PIN=29'],True),
        (['-DPSU_WARNING_PIN=28','-DPSU_ERROR_PIN=28'],False),
        *[([f'-DPSU_WARNING_PIN={pin}'],False) for pin in (-2,0,1,2,3,10,22,50,69+1)],
        (['-DPSU_WARNING_PIN=25','-DPSU_ENABLE_TOUCH=1'],False),
        (['-DPSU_ERROR_PIN=26','-DPSU_ENABLE_ENABLE_IN=1'],False),
        (['-DPSU_ERROR_PIN=6','-DPSU_FAN_PWM_PIN=6'],False),
        (['-DPSU_ERROR_PIN=27','-DPSU_CHG_ALLOW_PIN=27'],False)]:
        r=subprocess.run(cc+pins+['-x','c++','-fsyntax-only','-'],input='#include "config/AlertConfig.h"\n',text=True,capture_output=True,cwd=root)
        if bool(r.returncode)==good: raise SystemExit(str(pins)+': unexpected result\n'+r.stderr)
print('PASS: LOW before OUTPUT, independent levels, acknowledgement, debug no GPIO, pin collisions')
