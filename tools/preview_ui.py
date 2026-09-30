#!/usr/bin/env python3
"""Run production C framebuffer UI on host with explicit synthetic fixtures."""
from pathlib import Path
import argparse
import hashlib
import json
import re
import shlex
import subprocess
import tempfile
import struct
import zlib
from host_build import sanitizer_flags, sanitizer_name

root = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--output', type=Path, default=root / 'build/preview-ui')
args = parser.parse_args()
args.output.mkdir(parents=True, exist_ok=True)
headers = ['esp_err.h', 'esp_lcd_panel_ops.h', 'driver/i2c_master.h',
           'freertos/FreeRTOS.h', 'freertos/task.h', 'freertos/semphr.h',
           'freertos/queue.h', 'esp_heap_caps.h', 'esp_log.h', 'esp_timer.h',
           'esp_app_desc.h', 'nvs.h', 'nvs_flash.h', 'esp_codec_dev.h', 'esp_partition.h']
with tempfile.TemporaryDirectory(prefix='typix-ui-preview-') as work:
    work = Path(work)
    for header in headers:
        dest = work / header
        dest.parent.mkdir(parents=True, exist_ok=True)
        dest.write_text('#include "preview_ui.h"\n')
    freetype = shlex.split(subprocess.check_output(['pkg-config', '--cflags', '--libs', 'freetype2'], text=True))
    binary = work / 'preview-ui'
    subprocess.run(['cc', '-std=gnu11', '-Wall', '-Wextra', '-Wno-unused-function',
                    '-Wno-unused-variable', '-Wno-missing-field-initializers', '-Wno-sign-compare', '-Wno-misleading-indentation',
                    *sanitizer_flags(), '-fno-omit-frame-pointer',
                    '-I', str(work), '-I', str(root/'tools'),
                    str(root/'tools/preview_ui.c'), str(root/'main/ttf_font.c'), str(root/'main/builtin_apps.c'),
                    *freetype, '-lm', '-o', str(binary)], check=True)
    subprocess.run([str(binary), str(root/'fonts/puhui_subset.ttf'), str(args.output.resolve())], check=True)
for path in args.output.glob('*.ppm'):
    with path.open('rb') as stream:
        assert stream.readline() == b'P6\n'
        width, height = map(int, stream.readline().split())
        assert stream.readline() == b'255\n'
        pixels = stream.read()
    def chunk(kind, body):
        return struct.pack('!I', len(body)) + kind + body + struct.pack('!I', zlib.crc32(kind + body) & 0xffffffff)
    scanlines = b''.join(b'\0' + pixels[y*width*3:(y+1)*width*3] for y in range(height))
    png = b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('!IIBBBBB', width, height, 8, 2, 0, 0, 0))
    png += chunk(b'IDAT', zlib.compress(scanlines)) + chunk(b'IEND', b'')
    path.with_suffix('.png').write_bytes(png)
    path.unlink()
(args.output / 'manifest.json').write_text(json.dumps({
    'kind': 'host-framebuffer-preview', 'hardware_verified': False,
    'host_sanitizers': sanitizer_name(),
    'firmware_version': re.search(r'set\(PROJECT_VER "([^"]+)"\)',
                                  (root/'CMakeLists.txt').read_text()).group(1),
    'fixtures': 'Synthetic sensor, network, clock, audio and Raspberry Pi state. No hardware or network access.',
    'renderers': {name: hashlib.sha256((root/name).read_bytes()).hexdigest()
                  for name in ('main/ui.c', 'main/ttf_font.c', 'main/builtin_apps.c', 'main/builtin_apps.h')},
    'images': sorted(p.name for p in args.output.glob('*.png')),
}, indent=2) + '\n')
print(args.output.resolve())
