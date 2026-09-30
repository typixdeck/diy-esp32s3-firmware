#!/usr/bin/env python3
"""Run production startup code against allocation/I/O/reset failure fakes."""
from pathlib import Path
import sys
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from host_build import sanitizer_flags
import subprocess
import tempfile

root = Path(__file__).resolve().parent
cases = {
    'instrument': ('test_instrument_controls.c', ['esp_err.h','esp_codec_dev.h','freertos/FreeRTOS.h','freertos/task.h','freertos/semphr.h']),
    'audio': ('test_audio_start.c', ['driver/i2s_std.h', 'driver/i2c_master.h',
        'esp_codec_dev.h', 'esp_codec_dev_defaults.h', 'esp_log.h', 'esp_check.h']),
    'boot': ('test_boot_diag.c', ['esp_attr.h', 'esp_system.h', 'esp_app_desc.h',
        'esp_heap_caps.h', 'esp_timer.h', 'freertos/FreeRTOS.h']),
}
for name, (source, headers) in cases.items():
    with tempfile.TemporaryDirectory(prefix=f'typix-{name}-') as tmp:
        work = Path(tmp)
        for header in headers:
            path = work / header
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(f'#include "{name}_fakes.h"\n')
        binary = work / 'check'
        subprocess.run(['cc', '-std=gnu11', '-Wall', '-Wextra', '-Werror',
            '-Wno-unused-parameter', *sanitizer_flags(),
            '-fno-omit-frame-pointer', '-I', str(work), '-I', str(root),
            str(root/source), '-lm', '-o', str(binary)], check=True)
        subprocess.run([str(binary)], check=True)
