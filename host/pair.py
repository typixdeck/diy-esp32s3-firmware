#!/usr/bin/env python3
"""Provision this companion's public certificate and token over an explicit USB port."""
import argparse
import re
import base64
import os
from pathlib import Path
import select
import time
import companion
import share


def exchange(fd, command, expected):
    companion.send(fd,command+"\n")
    deadline=time.monotonic()+6
    line=bytearray();discard=False
    while time.monotonic()<deadline:
        if not select.select([fd],[],[],0.2)[0]:continue
        data=os.read(fd,512)
        if not data:raise OSError("USB disconnected")
        for byte in data:
            if byte in (10,13):
                if not discard:
                    text=bytes(line)
                    if text==expected:return
                    if text==b"TDPAIR ERROR":raise RuntimeError("Pairing rejected")
                    if expected==b"BOOT_040" and text.startswith(b"TD_DIAG ") and re.search(rb" fw=0\.4\.[01] ", b" "+text+b" "):return
                line.clear();discard=False
            elif not discard:
                if 32<=byte<=126 and len(line)<384:line.append(byte)
                else:line.clear();discard=True
    raise TimeoutError("No expected device response")


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument("--device",required=True)
    p.add_argument("--config",type=Path,default=Path.home()/".config/typixdeck/companion")
    a=p.parse_args()
    ip=(a.config/"address").read_text().strip()
    token=(a.config/"token").read_text().strip()
    import ipaddress,re
    ip=str(ipaddress.IPv4Address(ip))
    if not re.fullmatch("[0-9a-f]{64}",token):p.error("invalid token")
    pem=(a.config/"cert.pem").read_bytes()
    if len(pem)>1535:p.error("certificate too large")
    encoded=base64.b64encode(pem).decode("ascii")
    if len(encoded)>2048:p.error("encoded certificate too large")
    fd=companion.open_device(a.device)
    try:
        exchange(fd,"BOOT_STATUS",b"BOOT_040")
        exchange(fd,f"TDPAIR BEGIN {ip} {token}",b"TDPAIR READY")
        for offset in range(0,len(encoded),96):
            exchange(fd,f"TDPAIR CERT {offset} {encoded[offset:offset+96]}",b"TDPAIR CHUNK")
        exchange(fd,"TDPAIR COMMIT",b"TDPAIR OK")
    finally:os.close(fd)
    print("Pairing saved. Enable ESP Wi-Fi and sync its clock to use HTTPS.")

if __name__=="__main__":main()
