import importlib.util
from pathlib import Path
import sys
import unittest
from unittest.mock import patch, MagicMock

spec = importlib.util.spec_from_file_location("companion", Path(__file__).parents[1] / "host/companion.py")
companion = importlib.util.module_from_spec(spec)
spec.loader.exec_module(companion)


class SessionTests(unittest.TestCase):
    def make_session(self, allowed=True):
        self.calls = []
        session = companion.Session(
            allow_shutdown=allowed, nonce="a" * 16,
            shutdown_action=lambda: self.calls.append(1) or "ACCEPTED",
        )
        session.receive(f"TD1 WELCOME {'a' * 16} {'b' * 16}")
        session.heartbeat(True, (42, 42000, 1200000))
        return session

    def command(self, h="a", d="b", tail="1 SHUTDOWN"):
        return f"TD1 CMD {h * 16} {d * 16} {tail}"

    def test_default_read_only(self):
        session = self.make_session(False)
        self.assertTrue(session.receive(self.command()).endswith("DENIED\n"))
        self.assertEqual(self.calls, [])

    def test_shutdown_once(self):
        session = self.make_session()
        self.assertTrue(session.receive(self.command()).endswith("ACCEPTED\n"))
        self.assertIsNone(session.receive(self.command()))
        self.assertEqual(self.calls, [1])
        self.assertIn(" 0 42 ", session.heartbeat(True, (42, -1, -1)))

    def test_permission_not_available(self):
        session = self.make_session()
        session.heartbeat(False, (1, -1, -1))
        self.assertTrue(session.receive(self.command()).endswith("DENIED\n"))
        self.assertFalse(self.calls)

    def test_invalid_and_replay(self):
        session = self.make_session()
        for command in (self.command(h="c"), self.command(d="c"), self.command(tail="2 SHUTDOWN"),
                        self.command(tail="1 REBOOT"), self.command() + " extra",
                        "log " + self.command(), self.command() + "\n", "x" * 160):
            self.assertIsNone(session.receive(command))
        self.assertFalse(self.calls)

    def test_device_reconnect_does_not_replay(self):
        session = self.make_session()
        session.receive(self.command())
        session.receive(f"TD1 WELCOME {'a' * 16} {'c' * 16}")
        session.heartbeat(True, (1, -1, -1))
        self.assertIsNone(session.receive(self.command(d="c")))
        self.assertEqual(self.calls, [1])

    def test_nonce_bound_welcome(self):
        session = companion.Session(nonce="a" * 16)
        session.receive(f"TD1 WELCOME {'c' * 16} {'b' * 16}")
        self.assertIsNone(session.heartbeat(True, (1, -1, -1)))

    def test_shutdown_rechecks_authorization(self):
        with patch.object(companion, "shutdown_allowed", return_value=False), patch.object(companion.subprocess, "run") as run:
            self.assertEqual(companion.shutdown(), "DENIED")
            run.assert_not_called()

    def test_shutdown_constant_noninteractive_command(self):
        with patch.object(companion, "shutdown_allowed", return_value=True), patch.object(companion.subprocess, "run") as run:
            run.return_value.returncode = 0
            self.assertEqual(companion.shutdown(), "ACCEPTED")
            self.assertEqual(run.call_args.args[0], ["/usr/bin/systemctl", "--no-ask-password", "poweroff"])
            self.assertNotIn("shell", run.call_args.kwargs)


class TransportTests(unittest.TestCase):
    def test_reconnect_requires_physical_path(self):
        self.assertTrue(companion.stable_device("/dev/serial/by-path/platform-fd500000.pcie-pci-0000:01:00.0-usb-0:1.2:1.2"))
        for path in ("/dev/ttyACM0", "/dev/serial/by-id/board", "/dev/serial/by-path/../ttyACM0",
                     "/dev/serial/by-path/a b", "/dev/serial/by-path/a%h", "/dev/serial/by-path/",
                     "/dev/serial/by-path/..", "/dev/serial/by-path/."):
            self.assertFalse(companion.stable_device(path))
            with self.assertRaises(ValueError):
                companion.run_device(path, False, True)

    def test_binary_and_oversized_lines_are_discarded(self):
        reader = companion.LineReader()
        self.assertEqual(reader.feed(b"TD1 WEL"), [])
        self.assertEqual(reader.feed(b"COME a b\r\n"), ["TD1 WELCOME a b"])
        self.assertEqual(reader.feed(b"noise\x00TD1 WELCOME fake fake\n"), [])
        self.assertEqual(reader.feed(b"x" * 159 + b"\nTD1 HELLO good\n"), ["TD1 HELLO good"])
        self.assertLessEqual(len(reader.buffer), 158)

    def test_heartbeat_uses_cached_status_without_route_or_auth_wait(self):
        local = MagicMock()
        local.snapshot.return_value = (("192.0.2.1", 100, 200, 50), False)
        welcome = f"TD1 WELCOME {'a' * 16} {'b' * 16}\n".encode()
        with patch.object(companion.secrets, "token_hex", return_value="a" * 16), \
                patch.object(companion.time, "monotonic", side_effect=[0.0, 1.0, 2.0]), \
                patch.object(companion.select, "select", return_value=([5], [], [])), \
                patch.object(companion.os, "read", side_effect=[welcome, b"ignored log\n", b""]), \
                patch.object(companion, "telemetry", return_value=(123, 42000, 600000)), \
                patch.object(companion, "system_values") as route, \
                patch.object(companion, "shutdown_allowed") as auth, \
                patch.object(companion, "send") as send:
            with self.assertRaisesRegex(OSError, "disconnected"):
                companion.run_connection(5, False, local)
            lines = [call.args[1] for call in send.call_args_list]
            self.assertEqual(len([line for line in lines if line.startswith("TD1 HB ")]), 2)
            self.assertTrue(any(line.endswith(" 192.0.2.1 100 200 50\n") for line in lines))
            route.assert_not_called()
            auth.assert_not_called()

    def test_reconnect_uses_same_path_and_closes_each_session(self):
        path = "/dev/serial/by-path/test-0:1.2:1.2"
        with patch.object(companion, "LocalStatus") as local, \
                patch.object(companion, "open_device", side_effect=[FileNotFoundError(), 7, 8]) as opened, \
                patch.object(companion, "run_connection", side_effect=[OSError("disconnect"), KeyboardInterrupt()]) as run, \
                patch.object(companion.os, "close") as close, \
                patch.object(companion.time, "sleep") as sleep:
            with self.assertRaises(KeyboardInterrupt):
                companion.run_device(path, False, True)
            self.assertEqual([call.args for call in opened.call_args_list], [(path,)] * 3)
            self.assertEqual([call.args for call in close.call_args_list], [(7,), (8,)])
            self.assertEqual(run.call_count, 2)
            self.assertEqual(sleep.call_count, 2)
            local.return_value.__exit__.assert_called_once()

    def test_default_connection_exits_on_disconnect(self):
        with patch.object(companion, "LocalStatus"), \
                patch.object(companion, "open_device", return_value=7) as opened, \
                patch.object(companion, "run_connection", side_effect=OSError("disconnect")), \
                patch.object(companion.os, "close") as close, \
                patch.object(companion.time, "sleep") as sleep:
            with self.assertRaises(OSError):
                companion.run_device("/dev/ttyACM0", False, False)
            opened.assert_called_once()
            close.assert_called_once_with(7)
            sleep.assert_not_called()

    def test_sampler_does_not_advertise_shutdown_by_default(self):
        sampler = companion.LocalStatus(False)
        with patch.object(companion, "system_values", return_value=("192.0.2.1", 1, 2, 3)), \
                patch.object(companion, "shutdown_allowed") as auth:
            sampler.refresh()
        self.assertEqual(sampler.snapshot(), (("192.0.2.1", 1, 2, 3), False))
        auth.assert_not_called()


class InstallerTests(unittest.TestCase):
    def test_maintenance_prevents_starting_cdc_owner(self):
        host = Path(__file__).parents[1] / "host"
        with patch.dict(sys.modules, {"share": MagicMock(), "companion": companion}):
            spec = importlib.util.spec_from_file_location("companion_install", host / "install.py")
            installer = importlib.util.module_from_spec(spec)
            spec.loader.exec_module(installer)
        path = MagicMock()
        path.lstat.return_value.st_uid = 0
        path.lstat.return_value.st_size = 100
        path.is_file.return_value = True
        path.is_symlink.return_value = False
        for status in ("running", "failed", "succeeded"):
            path.read_text.return_value = '{"records":[{"status":"' + status + '"}]}'
            self.assertEqual(installer.maintenance_running(path), status == "running")
        for body in ('{"records":{}}', '{"records":[null]}', 'invalid json'):
            path.read_text.return_value = body
            self.assertTrue(installer.maintenance_running(path))
        path.lstat.return_value.st_uid = 501
        self.assertTrue(installer.maintenance_running(path))
        path.lstat.side_effect = FileNotFoundError()
        self.assertFalse(installer.maintenance_running(path))
        path.lstat.side_effect = PermissionError()
        self.assertTrue(installer.maintenance_running(path))


if __name__ == "__main__":
    unittest.main()
