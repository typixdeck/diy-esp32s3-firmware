from pathlib import Path
import sys
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from host_build import sanitizer_flags
import subprocess,tempfile
root=Path(__file__).resolve().parent
with tempfile.TemporaryDirectory(prefix='typix-share-test-') as temp:
    temp=Path(temp)
    (temp/'esp_err.h').write_text('typedef int esp_err_t;\n')
    binary=temp/'share-test'
    subprocess.run(['cc','-std=gnu11','-DPI_SHARE_HOST_TEST','-Wall','-Wextra','-Werror',*sanitizer_flags(),
                    '-I',str(temp),'-I',str(root),str(root/'test_pi_share.c'),'-lm','-o',str(binary)],check=True)
    subprocess.run([str(binary)],check=True)
