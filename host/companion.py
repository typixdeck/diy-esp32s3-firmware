#!/usr/bin/env python3
"""Local, opt-in TypixDeck telemetry/shutdown bridge. No third-party modules."""
from __future__ import annotations

import argparse
import fcntl
import os
from pathlib import Path
import re
import secrets
import select
import stat
import subprocess
import threading
import termios
import time
import tty

NONCE = re.compile(r"[0-9a-f]{16}\Z")
RECONNECT_DELAY = 2.0


def stable_device(path: str) -> bool:
    """Only a previously verified physical by-path may be reopened."""
    return bool(re.fullmatch(r"/dev/serial/by-path/[A-Za-z0-9][A-Za-z0-9_.:+-]{0,254}", path))


def shutdown_allowed() -> bool:
    """Only advertise existing noninteractive login1 authorization."""
    try:
        result = subprocess.run(
            ["/usr/bin/busctl", "--system", "--timeout=2", "call",
             "org.freedesktop.login1", "/org/freedesktop/login1",
             "org.freedesktop.login1.Manager", "CanPowerOff"],
            capture_output=True, text=True, timeout=3, check=False,
        )
        return result.returncode == 0 and result.stdout.strip() == 's "yes"'
    except (OSError, subprocess.TimeoutExpired):
        return False


def shutdown() -> str:
    # Never sudo, prompt, hard-power, or infer filesystem safety from this result.
    if not shutdown_allowed():
        return "DENIED"
    try:
        result = subprocess.run(
            ["/usr/bin/systemctl", "--no-ask-password", "poweroff"],
            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
            timeout=5, check=False,
        )
        return "ACCEPTED" if result.returncode == 0 else "FAILED"
    except (OSError, subprocess.TimeoutExpired):
        return "FAILED"


def read_int(path: str, minimum: int, maximum: int) -> int:
    try:
        value = int(Path(path).read_text(encoding="ascii").strip())
        return value if minimum <= value <= maximum else -1
    except (OSError, ValueError, UnicodeError):
        return -1


def telemetry() -> tuple[int, int, int]:
    try:
        uptime = min(4294967295, max(0, int(float(Path("/proc/uptime").read_text().split()[0]))))
    except (OSError, ValueError, IndexError):
        uptime = 0
    temperature = read_int("/sys/class/thermal/thermal_zone0/temp", 0, 200000)
    frequency = read_int("/sys/devices/system/cpu/cpu0/cpufreq/scaling_cur_freq", 0, 10000000)
    return uptime, temperature, frequency


def system_values() -> tuple[str, int, int, int]:
    from share import system_info
    return system_info(Path.home() / "TypixDeck/shared")


class LocalStatus:
    """Keep bounded route/login1 subprocesses out of the one-second CDC loop."""
    def __init__(self, allow_shutdown: bool):
        self.allow_shutdown = allow_shutdown
        self.values = ("-", -1, -1, -1)
        self.authorized = False
        self.lock = threading.Lock()
        self.stop = threading.Event()
        self.thread = threading.Thread(target=self.run, name="typix-local-status", daemon=True)

    def refresh(self):
        values = system_values()
        authorized = self.allow_shutdown and shutdown_allowed()
        with self.lock:
            self.values, self.authorized = values, authorized

    def run(self):
        while not self.stop.is_set():
            try:
                self.refresh()
            except (OSError, ValueError, subprocess.SubprocessError):
                with self.lock:
                    self.values, self.authorized = ("-", -1, -1, -1), False
            if self.stop.wait(2):
                break

    def snapshot(self):
        with self.lock:
            return self.values, self.authorized

    def __enter__(self):
        self.thread.start()
        return self

    def __exit__(self, *_):
        self.stop.set()
        self.thread.join(timeout=5)


class LineReader:
    """Discard binary/log noise without retaining the raw CDC stream."""
    def __init__(self):
        self.buffer = bytearray()
        self.discard = False

    def feed(self, data: bytes):
        lines = []
        for byte in data:
            if byte in (10, 13):
                if not self.discard and self.buffer:
                    lines.append(self.buffer.decode("ascii"))
                self.buffer.clear()
                self.discard = False
            elif not self.discard:
                if 32 <= byte <= 126 and len(self.buffer) < 158:
                    self.buffer.append(byte)
                else:
                    self.buffer.clear()
                    self.discard = True
        return lines


class Session:
    def __init__(self, *, allow_shutdown=False, nonce=None, shutdown_action=shutdown):
        self.host = nonce or secrets.token_hex(8)
        if not NONCE.fullmatch(self.host):
            raise ValueError("invalid session nonce")
        self.device = None
        self.sequence = 0
        self.allow_shutdown = allow_shutdown
        self.capability = False
        self.command_used = False
        self.shutdown_action = shutdown_action

    def hello(self) -> str:
        return f"TD1 HELLO {self.host}\n"

    def heartbeat(self, authorized: bool, values: tuple[int, int, int]) -> str | None:
        self.capability = bool(self.allow_shutdown and authorized and not self.command_used)
        if not self.device:
            return None
        self.sequence += 1
        if self.sequence > 4294967295:
            raise RuntimeError("session sequence exhausted")
        uptime, temperature, frequency = values
        return f"TD1 HB {self.host} {self.device} {self.sequence} {int(self.capability)} {uptime} {temperature} {frequency}\n"

    def receive(self, line: str) -> str | None:
        if len(line) >= 159 or any(ord(c) < 32 or ord(c) > 126 for c in line):
            return None
        parts = line.split(" ")
        if len(parts) == 4 and parts[:2] == ["TD1", "WELCOME"]:
            if parts[2] != self.host or not NONCE.fullmatch(parts[3]):
                return None
            if parts[3] != self.device:
                self.device = parts[3]
                self.sequence = 0
            return None
        if (len(parts) != 6 or parts[:2] != ["TD1", "CMD"] or
                not self.device or parts[2:4] != [self.host, self.device] or
                parts[4:] != ["1", "SHUTDOWN"] or self.command_used):
            return None
        self.command_used = True  # Commit before invoking any side effect, even on failure.
        result = self.shutdown_action() if self.capability and self.allow_shutdown else "DENIED"
        if result not in {"ACCEPTED", "DENIED", "FAILED"}:
            result = "FAILED"
        self.capability = False
        return f"TD1 ACK {self.host} {self.device} 1 {result}\n"


def open_device(path: str) -> int:
    # Caller supplies the physically verified port. No VID/PID scan or ttyACM fallback.
    fd = os.open(path, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
    try:
        if not stat.S_ISCHR(os.fstat(fd).st_mode):
            raise ValueError("device is not a character device")
        fcntl.flock(fd, fcntl.LOCK_EX | fcntl.LOCK_NB)
        attributes = termios.tcgetattr(fd)
        attributes[2] &= ~termios.HUPCL
        attributes[2] |= termios.CLOCAL | termios.CREAD
        termios.tcsetattr(fd, termios.TCSANOW, attributes)
        tty.setraw(fd, termios.TCSANOW)
        return fd
    except BaseException:
        os.close(fd)
        raise


def send(fd: int, line: str) -> None:
    remaining = line.encode("ascii")
    deadline = time.monotonic() + 2
    while remaining:
        wait = deadline - time.monotonic()
        if wait <= 0 or not select.select([], [fd], [], wait)[1]:
            raise TimeoutError("CDC write timeout")
        try:
            count = os.write(fd, remaining)
        except BlockingIOError:
            continue
        if count <= 0:
            raise OSError("CDC write failed")
        remaining = remaining[count:]


def run_connection(fd: int, allow_shutdown: bool, local: LocalStatus) -> None:
    session = Session(allow_shutdown=allow_shutdown)
    reader = LineReader()
    hello_at = heartbeat_at = 0.0
    while True:
        now = time.monotonic()
        if now >= hello_at:
            send(fd, session.hello())
            hello_at = now + 2
        if now >= heartbeat_at:
            (ip, mem, disk, load), authorized = local.snapshot()
            line = session.heartbeat(authorized, telemetry())
            if line:
                send(fd, line)
                send(fd, f"TD1 SYS {session.host} {session.device} {session.sequence} {ip} {mem} {disk} {load}\n")
            heartbeat_at = now + 1
        if not select.select([fd], [], [], 0.1)[0]:
            continue
        try:
            data = os.read(fd, 1024)
        except BlockingIOError:
            continue
        if not data:
            raise OSError("CDC disconnected")
        for line in reader.feed(data):
            response = session.receive(line)
            if response:
                send(fd, response)


def run_device(path: str, allow_shutdown: bool, reconnect: bool) -> None:
    if reconnect and not stable_device(path):
        raise ValueError("reconnect requires an explicit verified by-path device")
    with LocalStatus(allow_shutdown) as local:
        while True:
            fd = None
            try:
                fd = open_device(path)
                # Every reopened connection gets a fresh Session, never an old command.
                run_connection(fd, allow_shutdown, local)
            except (OSError, TimeoutError):
                if not reconnect:
                    raise
            finally:
                if fd is not None:
                    os.close(fd)
            if not reconnect:
                return
            time.sleep(RECONNECT_DELAY)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--device", required=True, help="explicit, verified /dev/serial/by-path/... path")
    parser.add_argument("--allow-shutdown", action="store_true", help="allow only already-authorized, noninteractive Linux poweroff")
    parser.add_argument("--reconnect", action="store_true", help="reopen only the same verified physical by-path after reconnect")
    args = parser.parse_args()
    if os.geteuid() == 0:
        parser.error("run as a regular user; never keep this bridge as root")
    if args.reconnect and not stable_device(args.device):
        parser.error("--reconnect requires an explicit verified /dev/serial/by-path/... path")
    run_device(args.device, args.allow_shutdown, args.reconnect)


if __name__ == "__main__":
    main()
