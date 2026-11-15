"""Focused software checks for mode authorization, approval replay, and disconnect locking."""
import contextlib
import io
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import types
import unittest
from unittest.mock import Mock, patch


fake_link = types.ModuleType("link")
fake_link.BleLink = fake_link.UsbLink = object
sys.modules.setdefault("link", fake_link)

import main
import pointer
import proximity
import remote
import security

ROOT = Path(__file__).resolve().parents[1]


def function_source(path, signature):
    source = path.read_text(encoding="utf-8")
    start = source.index(signature)
    opening = source.index("{", start)
    depth = 0
    for index in range(opening, len(source)):
        depth += source[index] == "{"
        depth -= source[index] == "}"
        if depth == 0:
            return source[opening:index + 1]
    raise AssertionError(f"unterminated function: {signature}")


class IntegrationTests(unittest.TestCase):
    def setUp(self):
        main._connected = False
        security.disconnect()
        security.setup(lambda _: True)
        remote.keyboard = Mock()
        remote.KEYS.update({
            "CMD:NEXT": "right", "CMD:PREV": "left", "CMD:PLAY": "media_play_pause",
            "CMD:SKIP": "media_next", "CMD:VOLUP": "media_volume_up",
            "CMD:VOLDN": "media_volume_down",
        })
        remote.set_connected(False)
        remote.setup(lambda _: True, main.remote_active)
        pointer.is_active = main.remote_active
        pointer.speed[:] = [0.0, 0.0]
        proximity.reset()

    def test_mode_sync_and_reconnect_gate_stale_commands(self):
        main._connected = True
        remote.set_connected(True)
        self.assertTrue(remote.on_line("MODE:MEDIA"))
        self.assertTrue(remote.on_line("CMD:PLAY"))
        remote.keyboard.tap.assert_called_once_with("media_play_pause")

        remote.keyboard.reset_mock()
        main._connected = False
        remote.set_connected(False)
        self.assertTrue(remote.on_line("CMD:PLAY"))
        remote.keyboard.tap.assert_not_called()
        main._connected = True
        remote.set_connected(True)
        self.assertTrue(remote.on_line("CMD:PLAY"))
        remote.keyboard.tap.assert_not_called()
        self.assertTrue(remote.on_line("MODE:MEDIA"))
        remote.on_line("CMD:PLAY")
        remote.keyboard.tap.assert_called_once_with("media_play_pause")

    def test_connect_requests_mode_and_clears_previous_authorization(self):
        main.link = types.SimpleNamespace(send_line=Mock(return_value=True))
        main._connected = True
        remote.set_connected(True)
        remote.set_mode("MEDIA")
        security.pending = b"old challenge"
        output = io.StringIO()
        with contextlib.redirect_stdout(output):
            main.on_connect("COM3")
        main.link.send_line.assert_called_once_with("SYNC")
        self.assertFalse(remote.mode_authorized)
        self.assertFalse(security.is_pending())

    def test_approval_is_one_use_and_blocks_remote_commands(self):
        main._connected = True
        remote.set_connected(True)
        remote.set_mode("MEDIA")
        challenge = security.request_approval()
        self.assertIsNotNone(challenge)
        self.assertTrue(security.is_pending())
        remote.on_line("CMD:NEXT")
        remote.keyboard.tap.assert_not_called()
        response = "RESP:" + security.expected_hex(challenge)
        self.assertEqual(security.check_resp(response), "approved")
        self.assertEqual(security.check_resp(response), "invalid")
        self.assertFalse(security.is_pending())

    def test_disconnect_forgets_pending_approval(self):
        challenge = security.request_approval()
        self.assertIsNotNone(challenge)
        security.disconnect()
        self.assertFalse(security.is_pending())
        self.assertEqual(security.check_resp("RESP:" + security.expected_hex(challenge)), "invalid")

    def test_only_connected_disconnect_locks_once_and_reports_failure(self):
        with patch.object(main, "lock_workstation", return_value=(False, "mock failure")) as lock:
            output = io.StringIO()
            with contextlib.redirect_stdout(output):
                main.on_disconnect()
                main._connected = True
                main.on_disconnect()
                main.on_disconnect()
            lock.assert_called_once_with()
            self.assertIn("Laptop lock failed: mock failure", output.getvalue())

    def test_time_requests_are_answered_in_any_mode(self):
        sent = []
        remote.setup(sent.append, main.remote_active)
        self.assertTrue(remote.on_line("TIME?"))
        self.assertEqual(len(sent), 1)
        self.assertRegex(sent[0], r"^TIME:\d\d:\d\d:\d\d$")

    def test_pointer_follows_remote_gate(self):
        self.assertTrue(pointer.on_line("TILT:100,0"))
        self.assertEqual(pointer.speed, [0.0, 0.0])  # not connected / not Media: ignored
        main._connected = True
        remote.set_connected(True)
        remote.set_mode("MEDIA")
        self.assertTrue(pointer.on_line("TILT:100,0"))
        self.assertNotEqual(pointer.speed, [0.0, 0.0])
        pointer.speed[:] = [0.0, 0.0]
        security.request_approval()
        self.assertTrue(pointer.on_line("TILT:100,0"))
        self.assertEqual(pointer.speed, [0.0, 0.0])  # approval pending: ignored

    def test_connect_and_disconnect_clear_old_signal_readings(self):
        proximity.readings[:] = [-95, -95, -95]
        main.link = types.SimpleNamespace(send_line=Mock(return_value=True))
        with contextlib.redirect_stdout(io.StringIO()):
            main.on_connect("COM3")
        self.assertEqual(proximity.readings, [])
        proximity.readings[:] = [-95]
        with patch.object(main, "lock_workstation", return_value=(True, "")), \
             contextlib.redirect_stdout(io.StringIO()):
            main.on_disconnect()
        self.assertEqual(proximity.readings, [])

    def test_all_handlers_are_routed(self):
        main._connected = True
        remote.set_connected(True)
        handled = []
        with patch.object(security, "on_line", side_effect=lambda l: handled.append("security") or l.startswith("RESP:")), \
             patch.object(proximity, "on_line", side_effect=lambda l: handled.append("proximity") or l.startswith("RSSI:")), \
             patch.object(pointer, "on_line", side_effect=lambda l: handled.append("pointer") or l.startswith("TILT:")):
            main.on_line("MODE:MEDIA")
            self.assertTrue(remote.media_mode)
            handled.clear()
            main.on_line("RSSI:-50")
            self.assertEqual(handled, ["security", "proximity"])
            handled.clear()
            main.on_line("TILT:0,0")
            self.assertEqual(handled, ["security", "proximity", "pointer"])

    def test_watch_mode_debounce_and_remote_gate_are_wired(self):
        remote_source = (ROOT / "watch" / "remote.ino").read_text(encoding="utf-8")
        mode = function_source(ROOT / "watch" / "watch.ino", "void appModeButtonLoop(")
        active = function_source(ROOT / "watch" / "remote.ino", "bool remoteActive(")
        self.assertIn("now - appModeChangedAt >= APP_MODE_DEBOUNCE_MS", mode)
        self.assertIn("!appModeHoldUsed && !appModeBlocked && !securityIsApprovalActive()", mode)
        self.assertIn("appModeIsMedia() && appHasLaptopLink() && !securityIsApprovalActive() && !pointerHeld()", active)
        self.assertIn("if (!remoteActive() && (raw || b.down))", remote_source)
        self.assertIn("b.blocked || !remoteActive()", remote_source)

    def test_watch_approval_timeout_runs_without_accelerometer(self):
        loop = function_source(ROOT / "watch" / "security.ino", "void securityLoop(")
        self.assertLess(loop.index("APPROVAL_TIMEOUT_MS"), loop.index("if (!secAccelAddr) return;"))
        self.assertIn('sendLine("RESP:REJECT")', loop)

    def test_watch_rejects_short_challenges_before_indexing(self):
        parser = function_source(ROOT / "watch" / "security.ino", "size_t secParseChallenge(")
        self.assertLess(parser.index("lineLen < 39"), parser.index("hex[2 * i]"))
        handler = function_source(ROOT / "watch" / "security.ino", "bool securityOnLine(")
        self.assertLess(handler.index("secParseChallenge(line, msg)"), handler.index("waitingForApproval = true"))

    def test_approval_owns_display_and_status_redraw_waits(self):
        ui = ROOT / "watch" / "ui.ino"
        draw = function_source(ui, "void uiDrawScreen(")
        self.assertIn("if (uiScreen == UI_MEDIA) {\n    remoteDrawLabels();", draw)
        mode = function_source(ui, "void uiSetKeyMode(")
        self.assertIn("if (uiScreen == UI_MEDIA || uiScreen == UI_KEY) uiShowHome();", mode)
        loop = function_source(ui, "void uiLoop(")
        self.assertIn("(uiScreen == UI_MEDIA || uiScreen == UI_KEY)) uiDrawLink();", loop)
        self.assertIn("bleConnected || appUsbActive()", loop)

    def test_fresh_watch_challenge_cancels_media_feedback_and_partial_tap(self):
        handler = function_source(ROOT / "watch" / "security.ino", "bool securityOnLine(")
        cancel = function_source(ROOT / "watch" / "remote.ino", "void remoteCancelHeld(")
        loop = function_source(ROOT / "watch" / "remote.ino", "void remoteLoop(")

        valid_challenge = handler.index("if (secParseChallenge(line, msg) == 0)")
        cancel_call = handler.index("remoteCancelHeld();")
        reset_tap = handler.index("secFirstTapMs = 0;")
        prompt = handler.index("uiShowApproval();")
        self.assertLess(valid_challenge, cancel_call)
        self.assertLess(cancel_call, reset_tap)
        self.assertLess(reset_tap, prompt)
        self.assertIn("remoteModeJustChanged = true;", cancel)
        self.assertIn("if (b.down || b.raw) b.handled = true;", cancel)
        self.assertIn("if (remoteModeJustChanged && raw) b.blocked = true;", loop)

    def test_usb_frame_activity_timeout_and_ble_fallback(self):
        if not shutil.which("g++"):
            self.skipTest("g++ is unavailable for the USB activity harness")
        watch = ROOT / "watch" / "watch.ino"
        usb_active = function_source(watch, "bool appUsbActive(")
        laptop_link = function_source(watch, "bool appHasLaptopLink(")
        link_loop = function_source(watch, "void appLinkLoop(")
        sync = function_source(watch, "void handleLine(")
        self.assertIn("if (!fromBle && !appUsbActive()) return;", sync)

        harness = r'''#include <cassert>
#include <cstdint>
struct Frame { struct { uint16_t FNUM; } bit; };
struct Device { Frame FNUM; };
struct Usb { Device DEVICE; };
Usb usb; Usb *USB = &usb;
bool usbConfigured = true;
bool staleDtr = true;
const unsigned long APP_USB_FRAME_TIMEOUT_MS = 250;
class UsbDevice { public: bool configured() { return usbConfigured; } } USBDevice;
unsigned long nowMs = 0;
unsigned long millis() { return nowMs; }
bool bleConnected = false, blePresent = false;
bool appLaptopAuthorized = false, appLaptopViaBle = false;
bool appLinkActive = false, appStatusDirty = false;
int appMode = 1;
const int MODE_KEY = 0;
bool canceledApproval = false, canceledHeld = false, uiKey = false;
void securityCancelApproval() { canceledApproval = true; }
void remoteCancelHeld() { canceledHeld = true; }
void uiSetKeyMode(bool key) { uiKey = key; }
''' + "bool appUsbActive() " + usb_active + '\n' + "bool appHasLaptopLink() " + laptop_link + '\n' + "void appLinkLoop() " + link_loop + r'''
int main() {
  usb.DEVICE.FNUM.bit.FNUM = 2046;
  assert(appUsbActive());
  nowMs = 1; usb.DEVICE.FNUM.bit.FNUM = 2047; assert(appUsbActive());
  nowMs = 2; usb.DEVICE.FNUM.bit.FNUM = 0; assert(appUsbActive()); // frame counter rollover
  nowMs = 252; assert(appUsbActive());
  nowMs = 253; assert(!appUsbActive()); // stale DTR/configuration cannot keep USB alive
  usbConfigured = false; nowMs = 254; usb.DEVICE.FNUM.bit.FNUM = 1; assert(!appUsbActive());

  usbConfigured = true; nowMs = 255; assert(appUsbActive());
  nowMs = 256; usb.DEVICE.FNUM.bit.FNUM = 2; assert(appUsbActive()); // replug recovery
  appLaptopAuthorized = true; appLaptopViaBle = true; bleConnected = true;
  nowMs = 507; appLinkLoop();
  assert(!appUsbActive() && appLaptopAuthorized && appHasLaptopLink()); // BLE remains authorized

  appLaptopViaBle = false; appMode = 1; nowMs = 508; appLinkLoop();
  assert(!appLaptopAuthorized && appMode == MODE_KEY && canceledApproval && canceledHeld && uiKey);
}
'''
        with tempfile.TemporaryDirectory(prefix="tinykey-usb-frame-") as temp:
            source = Path(temp) / "usb_frame_test.cpp"
            executable = Path(temp) / "usb_frame_test.exe"
            source.write_text(harness, encoding="utf-8")
            try:
                subprocess.run([shutil.which("g++"), "-std=c++11", str(source), "-o", str(executable)],
                               check=True, capture_output=True, text=True)
            except subprocess.CalledProcessError as exc:
                self.fail(exc.stderr)
            subprocess.run([str(executable)], check=True, capture_output=True, text=True)


if __name__ == "__main__":
    unittest.main()
