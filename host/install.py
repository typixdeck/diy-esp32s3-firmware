#!/usr/bin/env python3
"""Install the rootless companion on Raspberry Pi OS; never flashes the ESP."""
import argparse
import ipaddress
import json
import os
from pathlib import Path
import shutil
import subprocess
import share
import companion


def maintenance_running(path=Path("/var/lib/typix-copilot/status.json")):
    """Do not start a new CDC owner during a recorded write transaction."""
    try:
        info=path.lstat()
        if info.st_uid!=0 or not path.is_file() or path.is_symlink() or info.st_size>1048576:
            return True
        records=json.loads(path.read_text()).get("records",[])
        if not isinstance(records,list) or (records and not isinstance(records[0],dict)):
            return True
        return bool(records and records[0].get("status")=="running")
    except FileNotFoundError:
        return False
    except (OSError,ValueError,TypeError,AttributeError):
        return True


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument("--address",required=True,help="current Pi LAN IPv4; reserve it in DHCP")
    p.add_argument("--device",help="optional verified /dev/serial/by-path/... CDC path")
    p.add_argument("--start",action="store_true",help="enable and start installed user units")
    a=p.parse_args()
    if os.geteuid()==0:p.error("run as desktop user")
    address=str(ipaddress.IPv4Address(a.address))
    if a.device and not companion.stable_device(a.device):
        p.error("supply an explicit stable by-path device")
    if not Path("/usr/bin/python3").exists() or not shutil.which("openssl"):
        p.error("requires system Python 3 and openssl")
    home=Path.home()
    config=home/".config/typixdeck/companion"
    if not config.exists() or not (config/"token").exists():share.initialize(config,address)
    elif (config/"address").read_text().strip()!=address:p.error("address differs from existing certificate; preserve or explicitly re-pair")
    target=home/".local/share/typixdeck/companion"
    target.mkdir(parents=True,exist_ok=True)
    for name in ("companion.py","share.py","pair.py"):
        shutil.copy2(Path(__file__).with_name(name),target/name)
    (home/"TypixDeck/shared").mkdir(parents=True,exist_ok=True,mode=0o700)
    units=home/".config/systemd/user";units.mkdir(parents=True,exist_ok=True)
    # %h paths avoid shell expansion and tolerate spaces in the user's home directory.
    body='''[Unit]
Description=TypixDeck paired HTTPS sharing
After=graphical-session.target

[Service]
ExecStart=/usr/bin/python3 "%h/.local/share/typixdeck/companion/share.py"
Restart=on-failure
RestartSec=10
NoNewPrivileges=yes
UMask=0077

[Install]
WantedBy=default.target
'''
    display=os.environ.get("WAYLAND_DISPLAY","")
    if display and '/' not in display and all(c.isalnum() or c in '-_.' for c in display):
        body=body.replace("Restart=",f"Environment=WAYLAND_DISPLAY={display}\nEnvironment=XDG_RUNTIME_DIR=%t\nRestart=",1)
    (units/"typix-companion-share.service").write_text(body)
    names=["typix-companion-share.service"]
    if a.device:
        (units/"typix-companion-cdc.service").write_text(f'''[Unit]
Description=TypixDeck explicit CDC telemetry

[Service]
ExecStart=/usr/bin/python3 "%h/.local/share/typixdeck/companion/companion.py" --device "{a.device}" --reconnect
Restart=on-failure
RestartSec=2
NoNewPrivileges=yes

[Install]
WantedBy=default.target
''')
        names.append("typix-companion-cdc.service")
    subprocess.run(["systemctl","--user","daemon-reload"],check=True)
    if a.start:
        if a.device and maintenance_running():
            names.remove("typix-companion-cdc.service")
            print("CDC start deferred while Copilot maintenance is active or its status is unreadable; re-run after it finishes.")
        subprocess.run(["systemctl","--user","enable","--now",*names],check=True)
    print("Installed user services; shared folder: ~/TypixDeck/shared")
    if not display:print("Desktop environment absent: import WAYLAND_DISPLAY before requesting screenshots.")
    print("Pairing requires stopping CDC; Copilot with CDC coordination pauses/resumes the exact user service.")

if __name__=="__main__":main()
