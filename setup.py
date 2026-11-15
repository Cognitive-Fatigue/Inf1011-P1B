"""Install TinyKey's laptop dependencies, enroll a USB watch, and start its controller."""
import argparse
from pathlib import Path
import subprocess
import sys


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--usb", action="store_true", help="run the controller over USB instead of BLE")
    args = parser.parse_args(argv)
    root = Path(__file__).resolve().parent

    try:
        result = subprocess.run(
            [sys.executable, "-m", "pip", "install", "pyserial", "bleak", "pynput"], cwd=root
        )
        if result.returncode:
            print("Setup stopped: dependency installation failed; fix the pip error and rerun setup.", file=sys.stderr)
            return result.returncode

        result = subprocess.run(
            [sys.executable, str(root / "laptop" / "link.py"), "--setup"], cwd=root
        )
        if result.returncode:
            print("Setup stopped: USB enrollment failed; connect one updated watch and rerun setup.", file=sys.stderr)
            return result.returncode

        command = [sys.executable, str(root / "laptop" / "main.py")]
        if args.usb:
            command.append("--usb")
        return subprocess.run(command, cwd=root).returncode
    except KeyboardInterrupt:
        print("\nTinyKey setup cancelled.", file=sys.stderr)
        return 130


if __name__ == "__main__":
    sys.exit(main())
