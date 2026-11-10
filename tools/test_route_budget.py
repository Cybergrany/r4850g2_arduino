#!/usr/bin/env python3
"""Count production identity-table searches using a temporary instrumented copy.

No test hooks, counters or timing assumptions enter the firmware. The functional
host suite separately checks route invalidation, failures and protective actions.
"""
import ast
import os
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
# Reuse the host suite's source manifest without executing its build/run script.
manifest = ast.parse((root / 'tools/test_host.py').read_text())
sources = next(ast.literal_eval(n.value) for n in manifest.body if isinstance(n, ast.Assign)
               and any(isinstance(t, ast.Name) and t.id == 'sources' for t in n.targets))
with tempfile.TemporaryDirectory(prefix='r4850-route-budget-') as folder:
    folder = Path(folder)
    source = (root / 'src/psu/Discovery.cpp').read_text()
    entry = 'int8_t Discovery::identityIndex(const Identity& id, uint32_t now) const {'
    assert source.count(entry) == 1, 'Update the instrumentation entry point'
    source = 'namespace psu { unsigned routeSearches = 0; }\n' + source.replace(entry, entry + '\n  ++routeSearches;')
    instrumented = folder / 'Discovery.cpp'
    instrumented.write_text(source)
    sources = [str(instrumented) if s == 'src/psu/Discovery.cpp' else
               'test/test_route_budget.cpp' if s == 'test/test_main.cpp' else s for s in sources]
    binary = folder / 'test'
    subprocess.run([os.environ.get('CXX', 'g++'), '-std=c++11', '-O2', '-Wall', '-Wextra', '-Werror',
                    '-DPSU_ENABLE_DISPLAY=0', '-DPSU_ENABLE_ENCODER=0',
                    '-Itest/support', '-Isrc', '-Isrc/psu', *sources, '-o', str(binary)], cwd=root, check=True)
    subprocess.run([str(binary)], cwd=root, check=True)
