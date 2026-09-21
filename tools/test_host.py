#!/usr/bin/env python3
"""Compile real protocol/controller/storage/console against tiny host I/O adapters."""
from pathlib import Path
import os
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
sources = [
    "src/config/ChargerConfig.cpp", "src/protocol/HuaweiProtocol.cpp",
    "src/psu/Psu.cpp", "src/psu/PsuController.cpp",
    "src/storage/MemoryManager.cpp", "src/ui/SerialConsole.cpp", "test/test_main.cpp",
]
with tempfile.TemporaryDirectory(prefix="r4850-tests-") as directory:
    binary = str(Path(directory) / "tests")
    subprocess.run([
        os.environ.get("CXX", "g++"), "-std=c++11", "-Wall", "-Wextra", "-Werror", "-g",
        "-fsanitize=address,undefined", "-fno-omit-frame-pointer",
        "-DPSU_ENABLE_DISPLAY=0", "-DPSU_ENABLE_ENCODER=0",
        "-Itest/support", "-Isrc", *sources, "-o", binary,
    ], cwd=root, check=True)
    environment = os.environ.copy()
    # LeakSanitizer cannot run inside ptrace-based sandboxes. Address/UB checks stay on.
    environment["ASAN_OPTIONS"] = environment.get("ASAN_OPTIONS", "") + ":detect_leaks=0"
    subprocess.run([binary], cwd=root, env=environment, check=True)
