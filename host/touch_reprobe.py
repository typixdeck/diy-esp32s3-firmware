#!/usr/bin/env python3
"""Retry DT-declared, unbound GT911 devices after the shared I2C path returns.
No address scan, GPIO/reset/MUX/power writes, or input event capture.
"""
import ctypes
import fcntl
import os
from pathlib import Path
import re

class Message(ctypes.Structure):
    _fields_=[('addr',ctypes.c_uint16),('flags',ctypes.c_uint16),('length',ctypes.c_uint16),('buf',ctypes.c_void_p)]
class Transfer(ctypes.Structure):
    _fields_=[('msgs',ctypes.POINTER(Message)),('count',ctypes.c_uint32)]


def candidates():
    for path in Path('/sys/bus/i2c/devices').iterdir():
        match=re.fullmatch(r'(\d+)-(0014|005d)',path.name)
        if not match:continue
        try:
            compatible=(path/'of_node/compatible').read_bytes().split(b'\0')
            if b'goodix,gt911' not in compatible or (path/'name').read_text().strip()!='gt911':continue
        except OSError:continue
        yield path,int(match[1]),int(match[2],16)


def product_id(bus,address):
    fd=os.open(f'/dev/i2c-{bus}',os.O_RDWR|os.O_CLOEXEC)
    try:
        offset=(ctypes.c_uint8*2)(0x81,0x40);data=(ctypes.c_uint8*4)()
        msgs=(Message*2)(Message(address,0,2,ctypes.addressof(offset)),Message(address,1,4,ctypes.addressof(data)))
        request=Transfer(msgs,2)
        fcntl.ioctl(fd,0x0707,bytearray(request))
        return bytes(data)
    finally:os.close(fd)


def main():
    if os.geteuid()!=0:raise SystemExit('requires root for a scoped kernel driver retry')
    nodes=list(candidates())
    # Once any declared GT911 has a driver, never reset/unbind or try the other address.
    if any((path/'driver').exists() for path,_,_ in nodes):return
    for path,bus,address in nodes:
        try:
            if product_id(bus,address)!=b'911\0':continue
            if (path/'driver').exists():return
            Path('/sys/bus/i2c/drivers_probe').write_text(path.name)
            if (path/'driver').exists():
                print('GT911 driver registered after delayed availability')
                return
        except OSError:
            continue

if __name__=='__main__':main()
