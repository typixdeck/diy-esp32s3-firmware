#!/usr/bin/env python3
"""Package existing build locally. Does not open a port or flash hardware."""
from pathlib import Path
import argparse
import hashlib
import json
import re
import shutil
import subprocess
import sys
root = Path(__file__).resolve().parents[1]
p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--output', type=Path, default=root/'build/package')
args = p.parse_args()
out = args.output.resolve()
out.mkdir(parents=True, exist_ok=True)
build = root/'build'
desc = json.loads((build/'project_description.json').read_text())
config = json.loads((build/'flasher_args.json').read_text())
version = desc['project_version']
expected = re.search(r'set\(PROJECT_VER \"([0-9]+\.[0-9]+\.[0-9]+)\"\)', (root/'CMakeLists.txt').read_text())
if expected is None or version != expected.group(1):
    raise SystemExit('Build version does not match CMakeLists.txt; rebuild before packaging')
app = build/desc['app_bin']
if app.stat().st_size > 0x200000: raise SystemExit('App exceeds factory partition')
font = root/'fonts/puhui_subset.ttf'
if font.stat().st_size > 0x400000: raise SystemExit('Font exceeds font partition')
settings = config['flash_settings']
full = out/f'typixdeck-diy-{version}-full.bin'
subprocess.run([sys.executable, '-m', 'esptool', '--chip', 'esp32s3', 'merge_bin',
    '--flash_mode', settings['flash_mode'], '--flash_size', settings['flash_size'],
    '--flash_freq', settings['flash_freq'], '-o', str(full),
    '0x0', str(build/'bootloader/bootloader.bin'),
    '0x8000', str(build/'partition_table/partition-table.bin'),
    '0x10000', str(app), '0x210000', str(font)], check=True)
application = out/f'typixdeck-diy-{version}-app.bin'
shutil.copy2(app, application)
manifest = {'version': version, 'chip': 'esp32s3', 'board': 'TypixDeck 0720',
    'hardware_verified': False, 'idf': '5.5.1', 'flash_settings': settings,
    'source_commit': subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=root, text=True).strip(),
    'images': {}, 'notice': 'Prerelease; hardware validation pending. Full image overwrites NVS including preferences and Wi-Fi credentials. App-only requires matching partition/font layout. Built from source; no device data included.'}
for file in (application, full):
    manifest['images'][file.name] = {'bytes': file.stat().st_size, 'sha256': hashlib.sha256(file.read_bytes()).hexdigest(), 'offset': '0x0' if '-full.' in file.name else '0x10000'}
(out/'manifest.json').write_text(json.dumps(manifest, indent=2, ensure_ascii=False)+'\n')
(out/'SHA256SUMS').write_text(''.join(f"{hashlib.sha256(file.read_bytes()).hexdigest()}  {file.name}\n" for file in (application, full, out/'manifest.json')))
print(out)
