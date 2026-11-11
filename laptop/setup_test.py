"""Tests for the root one-command setup entry point."""
import contextlib
import importlib.util
import io
from pathlib import Path
import sys
import unittest
from unittest.mock import call, patch


ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("tinykey_setup", ROOT / "setup.py")
setup = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(setup)


class SetupTests(unittest.TestCase):
    def test_ble_setup_runs_install_enrollment_and_controller_in_order(self):
        results = [type("Result", (), {"returncode": code})() for code in (0, 0, 7)]
        with patch.object(setup.subprocess, "run", side_effect=results) as run:
            self.assertEqual(setup.main([]), 7)
        self.assertEqual(run.call_args_list, [
            call([sys.executable, "-m", "pip", "install", "pyserial", "bleak", "pynput"], cwd=ROOT),
            call([sys.executable, str(ROOT / "laptop" / "link.py"), "--setup"], cwd=ROOT),
            call([sys.executable, str(ROOT / "laptop" / "main.py")], cwd=ROOT),
        ])
        for args, kwargs in run.call_args_list[1:]:
            self.assertTrue(Path(args[0][1]).is_absolute())
            self.assertEqual(kwargs["cwd"], ROOT)

    def test_usb_setup_selects_usb_controller(self):
        with patch.object(setup.subprocess, "run", return_value=type("Result", (), {"returncode": 0})()) as run:
            self.assertEqual(setup.main(["--usb"]), 0)
        self.assertEqual(run.call_args_list[-1], call(
            [sys.executable, str(ROOT / "laptop" / "main.py"), "--usb"], cwd=ROOT
        ))

    def test_paths_work_when_setup_file_is_in_directory_with_spaces(self):
        fake_root = ROOT / "folder with spaces"
        results = [type("Result", (), {"returncode": 0})() for _ in range(3)]
        with patch.object(setup, "__file__", str(fake_root / "setup.py")), \
             patch.object(setup.subprocess, "run", side_effect=results) as run:
            self.assertEqual(setup.main([]), 0)
        self.assertEqual(run.call_args_list[1], call(
            [sys.executable, str(fake_root / "laptop" / "link.py"), "--setup"], cwd=fake_root
        ))
        self.assertEqual(run.call_args_list[2], call(
            [sys.executable, str(fake_root / "laptop" / "main.py")], cwd=fake_root
        ))

    def test_install_failure_stops_before_enrollment(self):
        with patch.object(setup.subprocess, "run", return_value=type("Result", (), {"returncode": 2})()) as run:
            with contextlib.redirect_stderr(io.StringIO()):
                self.assertEqual(setup.main([]), 2)
        run.assert_called_once()

    def test_enrollment_failure_stops_before_controller(self):
        results = [type("Result", (), {"returncode": code})() for code in (0, 3)]
        with patch.object(setup.subprocess, "run", side_effect=results) as run:
            with contextlib.redirect_stderr(io.StringIO()):
                self.assertEqual(setup.main([]), 3)
        self.assertEqual(run.call_count, 2)

    def test_help_has_no_subprocess_side_effects(self):
        with patch.object(setup.subprocess, "run") as run, contextlib.redirect_stdout(io.StringIO()) as output:
            with self.assertRaises(SystemExit) as raised:
                setup.main(["--help"])
        self.assertEqual(raised.exception.code, 0)
        self.assertIn("--usb", output.getvalue())
        run.assert_not_called()

    def test_keyboard_interrupt_returns_130_without_traceback(self):
        with patch.object(setup.subprocess, "run", side_effect=KeyboardInterrupt), \
             contextlib.redirect_stderr(io.StringIO()) as output:
            self.assertEqual(setup.main([]), 130)
        self.assertIn("setup cancelled", output.getvalue())


if __name__ == "__main__":
    unittest.main()
