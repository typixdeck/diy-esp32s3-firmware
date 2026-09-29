#!/usr/bin/env python3
"""Offline behavioral checks. Never opens a serial port or writes a device."""
from pathlib import Path
import subprocess
import sys
import tempfile
root = Path(__file__).resolve().parents[1]
def run(args): subprocess.run(args, cwd=root, check=True)
run([sys.executable, 'tests/test_net_service.py'])
run([sys.executable, 'tests/test_pi_share.py'])
run([sys.executable, 'tests/test_gt911.py'])
run([sys.executable, 'tests/test_sensors.py'])
run([sys.executable, 'tests/test_boot_services.py'])
run([sys.executable, '-m', 'unittest', 'discover', '-s', 'tests', '-p', 'test_companion.py'])
run([sys.executable, '-m', 'unittest', 'discover', '-s', 'tests', '-p', 'test_share.py'])
with tempfile.TemporaryDirectory(prefix='typix-check-') as work:
    for name, sources, flags in [
        ('instrument', ['main/instrument_engine.c', 'tests/test_instrument_engine.c'], []),
        ('pi-link', ['main/pi_link.c', 'tests/test_pi_link.c'], ['-DPI_LINK_HOST_TEST']),
    ]:
        out = str(Path(work)/name)
        run(['cc', '-std=c11', '-D_POSIX_C_SOURCE=200809L', '-Wall', '-Wextra', '-Werror',
             '-fsanitize=address,undefined', '-Imain', *flags, *sources, '-lm', '-o', out])
        run([out])
run([sys.executable, 'tools/preview_ui.py'])
