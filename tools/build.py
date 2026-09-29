#!/usr/bin/env python3
"""Build only; restores the tracked ES8389 overlay after component resolution."""
from pathlib import Path
import shutil
import subprocess
import tempfile
root = Path(__file__).resolve().parents[1]
codec = root/'managed_components/espressif__esp_codec_dev'
patches = ('device/es8389/es8389.c', 'device/es8389/es8389_reg.h', 'device/include/es8389_codec.h')
cache = root/'.cache'
cache.mkdir(exist_ok=True)
# Preserve source overlay and never run upstream flash scripts.
with tempfile.TemporaryDirectory(prefix='codec-overlay-', dir=cache) as work:
    work = Path(work)
    for name in patches:
        target = work/name
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(codec/name, target)
    partial = codec.exists() and not (codec/'CMakeLists.txt').exists()
    if partial:
        unexpected = [str(p.relative_to(codec)) for p in codec.rglob('*') if p.is_file() and str(p.relative_to(codec)) not in patches]
        if unexpected: raise SystemExit('Partial codec contains additional files; preserve/review before component resolution')
        shutil.move(str(codec), str(work/'partial-source'))
    try:
        subprocess.run(['idf.py', 'reconfigure'], cwd=root, check=True)
    finally:
        for name in patches:
            target = codec/name
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(work/name, target)
    subprocess.run(['idf.py', 'build'], cwd=root, check=True)
