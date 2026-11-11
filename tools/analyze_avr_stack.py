#!/usr/bin/env python3
"""Inspect linked AVR frames/call chains; report indirect calls and recursive paths.

This is an audit aid, not a proof: conditional paths and library/virtual calls
need review, and the bench watermark covers only paths actually exercised.
"""
import argparse
from pathlib import Path
import re
import subprocess
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('elf',type=Path)
p.add_argument('--core-dir',type=Path,default=Path(__file__).resolve().parents[2]/'.platformio')
a=p.parse_args()
text=subprocess.check_output([str(a.core_dir/'packages/toolchain-atmelavr/bin/avr-objdump'),'-d','-C',str(a.elf)],text=True)
parts=re.split(r'(?m)^([0-9a-f]+) <([^\n]+)>:\n',text)
functions={}
for i in range(1,len(parts),3):
    address,name,body=int(parts[i],16),parts[i+1],parts[i+2]
    instructions=[]
    for line in body.splitlines():
        m=re.match(r'\s*([0-9a-f]+):\s+(?:(?:[0-9a-f]{2})\s+)+\s*([a-z][a-z0-9]*)\s*(.*)',line)
        if m: instructions.append((int(m[1],16),m[2],m[3]))
    if not instructions or '::__c' in name: continue
    # AVR GCC saves registers, reads SP into Y, then subtracts its fixed frame.
    first='\n'.join(f'{op} {arg}' for _,op,arg in instructions[:40])
    end=first.find('out 0x3d, r28')
    if end<0:
        end=first.find('out 0x3e, r29')
    pro=first[:end] if end>=0 else ''
    if pro:
        saved=len(re.findall(r'(?m)^push ',pro))
        local=0
        m=re.search(r'sbiw r28, 0x([0-9a-f]+)',pro)
        if m: local=int(m[1],16)
        else:
            m=re.search(r'subi r28, 0x([0-9a-f]+)[^\n]*\n(sbci|sbc) r29, (r1|0x[0-9a-f]+)',pro)
            if m: local=int(m[1],16)+(int(m[3],16)*256 if m[2]=='sbci' else 0)
        # Small fixed frames can also use rcall .+0 (three bytes on ATmega2560).
        local+=3*len(re.findall(r'rcall \.\+0\b',pro))
    else:
        saved=local=0
        for _,op,arg in instructions:
            if op=='push': saved+=1
            elif op=='rcall' and arg.startswith('.+0'): local+=3
            elif name.startswith('__vector_') and op in ('in','eor'): continue
            else: break
    calls=[]; indirect=False
    for _,op,arg in instructions:
        if op in ('icall','eicall','ijmp','eijmp'): indirect=True
        if op not in ('call','rcall','jmp','rjmp'): continue
        m=re.search(r'(?:0x)?([0-9a-f]+) <([^>]+)>',arg)
        if not m: continue
        target=int(m[1],16)
        if target!=address and '+' not in m[2] and '::__c' not in m[2]: calls.append((target,0 if op in ('jmp','rjmp') else 3))
    functions[address]=(name,saved,local,calls,indirect)

def chain(at,active=()):
    if at in active: return (0,[],True)
    name,saved,local,calls,indirect=functions[at]
    best=(0,[],False)
    for target,ret in calls:
        if target not in functions: continue
        child=chain(target,active+(at,))
        if child[0]+ret>best[0]: best=(child[0]+ret,child[1],child[2])
    return saved+local+best[0],[name]+best[1],best[2] or indirect
print('Fixed frame bytes = saved registers + local frame. Calls include 3-byte Mega return addresses.')
for _,f in sorted(functions.items(),key=lambda v:v[1][1]+v[1][2],reverse=True)[:18]:
    print(f'{f[1]+f[2]:4} ({f[1]:2}+{f[2]:3}) {f[0]}')
print('\nLongest direct-call paths from selected entry points (indirect paths require separate review):')
roots=('main','psu::Application::begin(','psu::Application::tick(','psu::SerialConsole::storageCommand(',
       'psu::SerialConsole::execute(','psu::UiModel::action(','psu::PsuController::serviceProtection(',
       'psu::PsuController::loadProfile(','psu::CalibrationWizard::solve(',
       'psu::LocalUi::tick(','psu::Mcp2515Transport::onReceive(',
       'psu::(anonymous namespace)::serviceEncoder(', 'MCP2515Class::onInterrupt(', '__vector_')
for at,f in functions.items():
    if f[0]=='main' or any(f[0].startswith(r) for r in roots[1:]):
        size,path,unknown=chain(at)
        print(f'{size:4} bytes {"[indirect/cycle uncertainty]" if unknown else ""}: '+ ' -> '.join(path))
print('\nNot included: dynamic argument pushes, unlinked/indirect callees, heap allocations or interrupt overlap.')
print('Review those separately; compare with diag memory after boot, UI/serial/profile/protection/calibration stress.')
