#!/usr/bin/env python3
"""Exercise the AVR debug byte mapping on the host with address/UB sanitizers."""
import ast
import os
from pathlib import Path
import subprocess
import tempfile
root=Path(__file__).resolve().parents[1]
manifest=ast.parse((root/'tools/test_host.py').read_text())
sources=next(ast.literal_eval(n.value) for n in manifest.body if isinstance(n,ast.Assign)
             and any(isinstance(t,ast.Name) and t.id=='sources' for t in n.targets))
sources=[s for s in sources if not s.startswith('test/')]+['src/debug/Simulator.cpp','test/test_sim_storage.cpp']
with tempfile.TemporaryDirectory(prefix='r4850-sim-store-') as directory:
    binary=str(Path(directory)/'test')
    subprocess.run([os.environ.get('CXX','g++'),'-std=c++11','-Wall','-Wextra','-Werror','-g',
        '-fsanitize=address,undefined','-fno-omit-frame-pointer','-DPSU_UI_DEBUG=1','-DPSU_DEBUG_COMPACT_STORAGE=1',
        '-DPSU_MAX_UNITS=4','-DPSU_ENABLE_DISPLAY=0','-DPSU_ENABLE_ENCODER=0',
        '-Itest/support','-Isrc',*sources,'-o',binary],cwd=root,check=True)
    env=os.environ.copy(); env['ASAN_OPTIONS']=env.get('ASAN_OPTIONS','')+':detect_leaks=0'
    subprocess.run([binary],env=env,check=True)
