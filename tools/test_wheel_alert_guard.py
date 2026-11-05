#!/usr/bin/env python3
"""Compile the real wheel adapter against button events matching ClickEncoder."""
from pathlib import Path
import os
import subprocess
import tempfile
root=Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix='psu-wheel-guard-') as folder:
    d=Path(folder)
    (d/'ClickEncoder.h').write_text('''#pragma once
#include <Arduino.h>
int digitalRead(uint8_t);
class ClickEncoder {
public:
 enum Button {Open,Pressed,Held,Released,Clicked};
 static Button button;
 static int16_t delta;
 ClickEncoder(uint8_t,uint8_t,uint8_t,uint8_t) {}
 void service() {} void setAccelerationEnabled(bool) {} void setDoubleClickEnabled(bool) {}
 int16_t getValue() {int16_t value=delta; delta=0; return value;}
 Button getButton() {Button b=button; button=Open; return b;}
};
''')
    (d/'TimerOne.h').write_text('''#pragma once
struct Timer {void initialize(int) {} void attachInterrupt(void(*)()) {}};
extern Timer Timer1;
''')
    (d/'test.cpp').write_text('''#include "ui/WheelInput.h"
#include <ClickEncoder.h>
#include <TimerOne.h>
#include <cassert>
Timer Timer1;
ClickEncoder::Button ClickEncoder::button=ClickEncoder::Open;
int16_t ClickEncoder::delta=0;
uint32_t timeNow=0; bool down=false;
uint32_t millis() {return timeNow;}
int digitalRead(uint8_t) {return down ? LOW : HIGH;}
int main() {
 psu::WheelInput wheel; wheel.begin(); down=true; wheel.cancelGesture();
 ClickEncoder::button=ClickEncoder::Held; ClickEncoder::delta=1;
 auto stale=wheel.read(); assert(!stale.held && !stale.delta);
 down=false; timeNow=10; ClickEncoder::button=ClickEncoder::Clicked; assert(!wheel.read().clicked);
 timeNow=70; ClickEncoder::delta=1; assert(wheel.read().delta==1);
 down=true; timeNow=80; assert(!wheel.read().clicked);
 down=false; ClickEncoder::button=ClickEncoder::Clicked; assert(wheel.read().clicked);
 // Short release/bounce cannot rearm the old press; rollover is safe.
 down=true; wheel.cancelGesture(); timeNow=0xfffffff0; wheel.read();
 down=false; wheel.read(); timeNow=5; down=true; wheel.read();
 ClickEncoder::button=ClickEncoder::Held; assert(!wheel.read().held);
 down=false; timeNow=20; wheel.read(); timeNow=80; wheel.read(); down=true; timeNow=90; wheel.read();
 ClickEncoder::button=ClickEncoder::Held; assert(wheel.read().held);
}
''')
    binary=str(d/'test')
    subprocess.run([os.environ.get('CXX','g++'),'-std=c++11','-Wall','-Wextra','-Werror',
        '-DPSU_TEST_FAN_GPIO=1','-I'+folder,'-Itest/support','-Isrc','src/ui/WheelInput.cpp',str(d/'test.cpp'),'-o',binary],cwd=root,check=True)
    subprocess.run([binary],check=True)
print('PASS: old click/hold consumed, physical release and new press required, debounce and rollover')
