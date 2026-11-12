#!/usr/bin/env python3
"""Check linked observer isolation and absence of the optional recorder in ordinary builds."""
import argparse
from pathlib import Path
import re
import subprocess
root=Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--core-dir',type=Path,default=root.parent/'.platformio')
a=p.parse_args()
nm=a.core_dir/'packages/toolchain-atmelavr/bin/avr-nm'
def symbols(env):
    return subprocess.check_output([str(nm),'-C',str(root/'.pio/build'/env/'firmware.elf')],text=True)
observer=symbols('mega2560_can_observer')
for pattern in (r'PsuController::',r'MemoryManager::',r'ArduinoEeprom',r'eeprom_(read|write|update)',
                r'LocalUi::',r'SerialConsole::',r'mcp2515::Transmitter::'):
    assert not re.search(pattern,observer), 'Observer unexpectedly links '+pattern
assert 'BusObserver::' in observer and 'Mcp2515Transport::' in observer and '(anonymous namespace)::cutoff' in observer
# The CAN library vtable can retain endPacket; its symbol is not proof of a call.
# The actual adapter send() returns false in this build, exercised by host tests.
for env in ('mega2560_serial','mega2560_wheel_alerts'):
    assert 'BusProfile::' not in symbols(env), env+': disabled instrumentation linked'
print('PASS: observer has no controller/EEPROM/UI/transmitter; ordinary builds omit profiling')
