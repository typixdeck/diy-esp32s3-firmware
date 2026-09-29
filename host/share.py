#!/usr/bin/env python3
"""Paired, read-only TypixDeck HTTPS companion. Run as the desktop user."""
from __future__ import annotations
import argparse
import hmac
import ipaddress
import json
import os
from pathlib import Path
import re
import secrets
import ssl
import stat
import subprocess
import time
from http.server import BaseHTTPRequestHandler, HTTPServer
from socketserver import ThreadingMixIn
import threading
from contextlib import contextmanager
import io
import companion

PORT = 38471
MAX_FILE = 256 * 1024
NAME = re.compile(r"[A-Za-z0-9][A-Za-z0-9._-]{0,39}\Z")


def system_info(root: Path) -> tuple[str, int, int, int]:
    address, memory, disk, load = "-", -1, -1, -1
    try:
        result = subprocess.run(["ip", "-j", "route", "get", "192.0.2.1"],
                                capture_output=True, text=True, timeout=2, check=False)
        route = json.loads(result.stdout)[0]
        address = str(ipaddress.IPv4Address(route.get("prefsrc", "")))
    except (OSError, ValueError, KeyError, IndexError, subprocess.TimeoutExpired):
        pass
    try:
        for line in Path("/proc/meminfo").read_text().splitlines():
            if line.startswith("MemAvailable:"):
                memory = int(line.split()[1]) // 1024
        fs = os.statvfs(root)
        disk = min(2147483647, fs.f_bavail * fs.f_frsize // 1048576)
        load = min(1000000, round(os.getloadavg()[0] * 100))
    except (OSError, ValueError):
        pass
    return address, memory, disk, load


def read_shared(root: Path, name: str) -> bytes:
    if not NAME.fullmatch(name):
        raise ValueError("invalid name")
    directory = os.open(root, os.O_RDONLY | os.O_DIRECTORY | os.O_NOFOLLOW)
    try:
        fd = os.open(name, os.O_RDONLY | os.O_NOFOLLOW | os.O_NONBLOCK, dir_fd=directory)
        try:
            info = os.fstat(fd)
            if not stat.S_ISREG(info.st_mode) or info.st_size > MAX_FILE:
                raise ValueError("not a bounded regular file")
            data = bytearray()
            while len(data) <= MAX_FILE:
                chunk = os.read(fd, min(8192, MAX_FILE + 1 - len(data)))
                if not chunk:
                    break
                data.extend(chunk)
            if len(data) > MAX_FILE:
                raise ValueError("file grew past limit")
            return bytes(data)
        finally:
            os.close(fd)
    finally:
        os.close(directory)


def list_shared(root: Path) -> bytes:
    entries = []
    for item in sorted(root.iterdir()):
        if len(entries) == 24:
            break
        if not NAME.fullmatch(item.name):
            continue
        try:
            info = item.lstat()
            if stat.S_ISREG(info.st_mode) and info.st_size <= MAX_FILE:
                entries.append(f"{item.name} {info.st_size}\n")
        except OSError:
            pass
    return "".join(entries).encode("ascii")


_capture_lock = threading.Lock()

@contextmanager
def capture_output(env):
    """Wake only the selected display for an explicit snapshot, then restore DPMS."""
    output = None
    restore_off = False
    try:
        result = subprocess.run(["/usr/bin/wlopm"], capture_output=True, text=True,
                                env=env, timeout=2, check=True)
        outputs = []
        for line in result.stdout.splitlines():
            parts = line.split()
            if len(parts) == 2 and re.fullmatch(r"[A-Za-z0-9][A-Za-z0-9_.-]{0,63}", parts[0]) and parts[1] in ("on", "off"):
                outputs.append(parts)
        if outputs:
            output, state = next((x for x in outputs if x[0].startswith("DPI-")), outputs[0])
            if state == "off":
                subprocess.run(["/usr/bin/wlopm", "--on", output], env=env,
                               stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=2, check=True)
                restore_off = True
    except FileNotFoundError:
        pass
    try:
        yield output
    finally:
        if restore_off:
            subprocess.run(["/usr/bin/wlopm", "--off", output], env=env,
                           stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=2, check=False)


def screenshot() -> bytes:
    # No framebuffer device/root access, no invented image on capture failure.
    from PIL import Image, ImageOps
    env = os.environ.copy()
    if not env.get("WAYLAND_DISPLAY"):
        raise OSError("Wayland desktop unavailable")
    with _capture_lock, capture_output(env) as output:
        command = ["/usr/bin/grim", "-s", "0.5"]
        if output:
            command += ["-o", output]
        command += ["-"]
        result = subprocess.run(command, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL,
                                env=env, timeout=5, check=True)
    image = Image.open(io.BytesIO(result.stdout)).convert("RGB")
    image = ImageOps.pad(image, (320, 240), color="black")
    output = bytearray(320 * 240 * 2)
    for i, (r, g, b) in enumerate(image.getdata()):
        value = (r >> 3) << 11 | (g >> 2) << 5 | b >> 3
        output[i * 2] = value & 255
        output[i * 2 + 1] = value >> 8
    return bytes(output)


class Handler(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"
    def log_message(self, *_):
        pass  # No tokens, addresses, filenames or activity journal.

    def reply(self, code, body=b"", kind="application/octet-stream"):
        self.send_response(code)
        self.send_header("Content-Type", kind)
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        self.wfile.write(body)

    def do_GET(self):
        expected = "Bearer " + self.server.token
        if not hmac.compare_digest(self.headers.get("Authorization", ""), expected):
            self.reply(401)
            return
        try:
            if self.path == "/v1/status":
                values = (*companion.telemetry(), *system_info(self.server.root))
                data = ("TD2 STATUS " + " ".join(map(str, values)) + "\n").encode("ascii")
            elif self.path == "/v1/files":
                data = list_shared(self.server.root)
            elif self.path == "/v1/screen":
                data = self.server.capture()
                if len(data) != 320 * 240 * 2:
                    raise ValueError("invalid screenshot")
            elif self.path.startswith("/v1/files/"):
                data = read_shared(self.server.root, self.path[len("/v1/files/"):])
            else:
                self.reply(404)
                return
            self.reply(200, data)
        except (OSError, ValueError, subprocess.SubprocessError, ImportError):
            self.reply(503)


class ShareServer(ThreadingMixIn, HTTPServer):
    daemon_threads = True
    def __init__(self, address, root, token, context=None, capture=screenshot):
        self.root, self.token, self.context, self.capture = Path(root), token, context, capture
        self.slots = threading.BoundedSemaphore(3)
        super().__init__(address, Handler)
    def process_request(self, request, address):
        if not self.slots.acquire(blocking=False):
            self.shutdown_request(request)
            return
        try:
            super().process_request(request, address)
        except BaseException:
            self.slots.release()
            raise
    def process_request_thread(self, request, address):
        # Handshakes and an idle keepalive connection cannot block the accept loop.
        try:
            request.settimeout(12)
            if self.context:
                request = self.context.wrap_socket(request, server_side=True)
            super().process_request_thread(request, address)
        except (OSError, ssl.SSLError):
            self.shutdown_request(request)
        finally:
            self.slots.release()
    def handle_error(self, *_):
        pass  # A client disconnect is not a reason to expose request context.


def initialize(directory: Path, address: str):
    address = str(ipaddress.IPv4Address(address))
    directory.mkdir(mode=0o700, parents=True, exist_ok=True)
    os.chmod(directory, 0o700)
    for name in ("key.pem", "cert.pem", "token"):
        if (directory / name).exists():
            raise ValueError("pairing already exists; refusing to overwrite")
    old_umask = os.umask(0o077)
    try:
        subprocess.run(["openssl", "req", "-x509", "-newkey", "ec", "-pkeyopt", "ec_paramgen_curve:prime256v1",
                        "-nodes", "-keyout", str(directory / "key.pem"), "-out", str(directory / "cert.pem"),
                        "-days", "1825", "-subj", "/CN=typixdeck-companion", "-addext", f"subjectAltName=IP:{address}",
                        "-addext", "basicConstraints=critical,CA:TRUE"],
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, check=True)
        (directory / "token").write_text(secrets.token_hex(32))
        (directory / "address").write_text(address)
    finally:
        os.umask(old_umask)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--config", type=Path, default=Path.home() / ".config/typixdeck/companion")
    parser.add_argument("--share", type=Path, default=Path.home() / "TypixDeck/shared")
    parser.add_argument("--init", metavar="IPv4", help="create pairing locally without starting a server")
    args = parser.parse_args()
    if os.geteuid() == 0:
        parser.error("run as desktop user")
    if args.init:
        initialize(args.config, args.init)
        return
    token = (args.config / "token").read_text().strip()
    if not re.fullmatch(r"[0-9a-f]{64}", token):
        parser.error("invalid pairing")
    address = str(ipaddress.IPv4Address((args.config / "address").read_text().strip()))
    args.share.mkdir(parents=True, exist_ok=True, mode=0o700)
    context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
    context.minimum_version = ssl.TLSVersion.TLSv1_2
    context.load_cert_chain(args.config / "cert.pem", args.config / "key.pem")
    with ShareServer((address, PORT), args.share, token, context) as server:
        server.serve_forever()

if __name__ == "__main__":
    main()
