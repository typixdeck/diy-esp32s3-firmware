import importlib.util
from pathlib import Path
import unittest
from unittest.mock import patch

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


if __name__ == "__main__":
    unittest.main()
