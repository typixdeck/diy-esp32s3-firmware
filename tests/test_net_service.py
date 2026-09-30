#!/usr/bin/env python3
"""Compile/run isolated host fakes. No device, network or credential access."""
from pathlib import Path
import sys
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from host_build import sanitizer_flags
import subprocess
import tempfile

root = Path(__file__).resolve().parent
headers = [
    'esp_err.h', 'esp_event.h', 'esp_log.h', 'esp_netif.h', 'esp_sntp.h',
    'esp_timer.h', 'esp_wifi.h', 'nvs.h', 'freertos/FreeRTOS.h',
    'freertos/queue.h', 'freertos/semphr.h', 'freertos/task.h',
]
with tempfile.TemporaryDirectory(prefix='typix-net-test-') as work:
    work = Path(work)
    for header in headers:
        dest = work / header
        dest.parent.mkdir(parents=True, exist_ok=True)
        dest.write_text('#include "net_fakes.h"\n')
    binary = work / 'net-test'
    subprocess.run([
        'cc', '-std=gnu11', '-Wall', '-Wextra', '-Werror',
        *sanitizer_flags(), '-fno-omit-frame-pointer',
        '-I', str(work), '-I', str(root), str(root / 'test_net_service.c'),
        '-o', str(binary),
    ], check=True)
    subprocess.run([str(binary)], check=True)
