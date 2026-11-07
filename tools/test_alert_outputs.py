#!/usr/bin/env python3
"""Test independent alert/active GPIOs, acknowledgement, debug isolation and pin validation."""
from pathlib import Path
import os
import subprocess
import tempfile
root=Path(__file__).resolve().parents[1]
cc=[os.environ.get('CXX','g++'),'-std=c++11','-Wall','-Wextra','-Werror','-Itest/support','-Isrc']
with tempfile.TemporaryDirectory(prefix='psu-alert-gpio-') as folder:
    binary=str(Path(folder)/'test')
    for debug,active in ((0,30),(1,30),(0,-1)):
        subprocess.run(cc+['-DPSU_TEST_FAN_GPIO=1','-DPSU_WARNING_PIN=28','-DPSU_ERROR_PIN=29',
            f'-DPSU_UI_DEBUG={debug}',f'-DPSU_ACTIVE_PIN={active}',
            'src/io/AlertOutputs.cpp','src/io/PsuActiveOutput.cpp','src/psu/Alerts.cpp',
            'test/test_alert_outputs.cpp','-o',binary],cwd=root,check=True)
        subprocess.run([binary],check=True)
    for pins,good in [([],True),(['-DPSU_WARNING_PIN=28','-DPSU_ERROR_PIN=29'],True),
        (['-DPSU_WARNING_PIN=28','-DPSU_ERROR_PIN=28'],False),
        *[([f'-DPSU_WARNING_PIN={pin}'],False) for pin in (-2,0,1,2,3,10,22,50,69+1)],
        (['-DPSU_WARNING_PIN=25','-DPSU_ENABLE_TOUCH=1'],False),
        (['-DPSU_ERROR_PIN=26','-DPSU_ENABLE_ENABLE_IN=1'],False),
        (['-DPSU_ERROR_PIN=6','-DPSU_FAN_PWM_PIN=6'],False),
        (['-DPSU_ERROR_PIN=27','-DPSU_CHG_ALLOW_PIN=27'],False),
        (['-DPSU_ACTIVE_PIN=30','-DPSU_WARNING_PIN=28','-DPSU_ERROR_PIN=29'],True),
        (['-DPSU_ACTIVE_PIN=69'],True),
        (['-DPSU_ACTIVE_PIN=4','-DPSU_ENABLE_ENCODER=0'],True),
        (['-DPSU_ACTIVE_PIN=22','-DPSU_ENABLE_DISPLAY=0','-DPSU_ENABLE_ENCODER=0'],True),
        *[([f'-DPSU_ACTIVE_PIN={pin}'],False) for pin in (-2,0,1,2,3,4,5,10,22,23,24,50,51,52,53,70,256)],
        (['-DPSU_ACTIVE_PIN=25','-DPSU_ENABLE_TOUCH=1'],False),
        (['-DPSU_ACTIVE_PIN=26','-DPSU_ENABLE_ENABLE_IN=1'],False),
        (['-DPSU_ACTIVE_PIN=6','-DPSU_FAN_PWM_PIN=6'],False),
        (['-DPSU_ACTIVE_PIN=27','-DPSU_CHG_ALLOW_PIN=27'],False),
        (['-DPSU_ACTIVE_PIN=28','-DPSU_WARNING_PIN=28'],False),
        (['-DPSU_ACTIVE_PIN=29','-DPSU_ERROR_PIN=29'],False)]:
        r=subprocess.run(cc+pins+['-x','c++','-fsyntax-only','-'],input='#include "config/AlertConfig.h"\n',text=True,capture_output=True,cwd=root)
        if bool(r.returncode)==good: raise SystemExit(str(pins)+': unexpected result\n'+r.stderr)
print('PASS: LOW before OUTPUT, independent warning/error/active levels, acknowledgement, debug/disabled no GPIO, pin collisions')
